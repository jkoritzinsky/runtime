// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Text;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Microsoft.CodeAnalysis.CSharp.SyntaxFactory;
using Microsoft.CodeAnalysis.CSharp;
using System.Collections.Immutable;
using System.Linq;
using System.Diagnostics;
using Microsoft.CodeAnalysis;
using Microsoft.Interop.Generators;

namespace Microsoft.Interop
{
    internal static class StructMarshallingImplementationGenerator
    {
        private record struct BoundGenerator(TypePositionInfo TypeInfo, IMarshallingGenerator Generator);

        private class StructMarshallingStubCodeContext : StubCodeContext
        {
            public override bool SingleFrameSpansNativeContext => false;

            public override bool AdditionalTemporaryStateLivesAcrossStages => false;

            public override (string managed, string native) GetIdentifiers(TypePositionInfo info)
            {
                return ($"managed.{info.InstanceIdentifier}", info.InstanceIdentifier);
            }

            public StructMarshallingStubCodeContext WithStage(Stage stage)
            {
                return new StructMarshallingStubCodeContext { CurrentStage = stage };
            }
        }

        private class ValuePropertyStubCodeContext : StubCodeContext
        {
            public override bool SingleFrameSpansNativeContext => false;

            public override bool AdditionalTemporaryStateLivesAcrossStages => false;

            public override (string managed, string native) GetIdentifiers(TypePositionInfo info)
            {
                return (info.InstanceIdentifier, $"value.{info.InstanceIdentifier}");
            }

            public override string GetAdditionalIdentifier(TypePositionInfo info, string name) => $"{info.InstanceIdentifier}__{name}";

            public ValuePropertyStubCodeContext WithStage(Stage stage)
            {
                return new ValuePropertyStubCodeContext { CurrentStage = stage };
            }
        }

        public static StructDeclarationSyntax GenerateStructMarshallingCode(
            StructMarshallingContext structToMarshal,
            Action<TypePositionInfo, MarshallingNotSupportedException> marshallingNotSupportedCallback,
            IMarshallingGeneratorFactory generatorFactory)
        {
            // TODO: Add support for emitting other advanced custom marshalling features accurately.

            AttributedMarshallingModelGeneratorFactory managedToMarshalerGeneratorFactory = new(
                generatorFactory,
                new AttributedMarshallingModelGeneratorFactoryOptions(
                    false,
                    false,
                    ValidateScenarioSupport: false,
                    structToMarshal.MarshallingFeatures.HasValueProperty
                    ? AttributedMarshallingModelGenerationPhases.ManagedToMarshallerType
                    : AttributedMarshallingModelGenerationPhases.All));

            StructDeclarationSyntax nativeStruct = StructDeclaration(MarshallerHelpers.GeneratedNativeStructName)
                .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword), Token(SyntaxKind.UnsafeKeyword)));

            if (structToMarshal.MarshallingFeatures.MarshallerMustBeRefStruct)
            {
                nativeStruct = nativeStruct.AddModifiers(Token(SyntaxKind.RefKeyword));
            }

            StructMarshallingStubCodeContext codeContext = new StructMarshallingStubCodeContext().WithStage(StubCodeContext.Stage.Setup);

            ImmutableArray<BoundGenerator> boundGenerators = ImmutableArray.CreateRange(structToMarshal.Fields.Select(p => CreateGenerator(p, marshallingNotSupportedCallback, managedToMarshalerGeneratorFactory, codeContext)));

            nativeStruct = nativeStruct.AddMembers(CreateFields(boundGenerators));

            Dictionary<TypePositionInfo, int> typePositionInfoToIndex = structToMarshal.Fields.Select(static (field, index) => (field, index)).ToDictionary(value => value.field, value => value.index);

            ImmutableArray<BoundGenerator> dependencySortedFields = MarshallerHelpers.GetTopologicallySortedElements(
                boundGenerators,
                m => typePositionInfoToIndex[m.TypeInfo],
                m => GetInfoDependencies(m.TypeInfo))
                .ToImmutableArray();

            StatementSyntax[] setupStatements = boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, codeContext)).ToArray();

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.ManagedToNative))
            {
                ConstructorDeclarationSyntax managedToNativeConstructor = GenerateMarshallerConstructor(
                    structToMarshal,
                    codeContext,
                    dependencySortedFields,
                    setupStatements);

                nativeStruct = nativeStruct.AddMembers(managedToNativeConstructor);
            }

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.NativeToManaged))
            {
                MethodDeclarationSyntax toManagedMethod = GenerateToManagedMethod(
                    structToMarshal,
                    codeContext,
                    dependencySortedFields,
                    setupStatements);

                nativeStruct = nativeStruct.AddMembers(toManagedMethod);
            }

            StubCodeContext freeNativeCodeContext = codeContext.WithStage(StubCodeContext.Stage.Cleanup);

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.FreeNativeResources))
            {
                MethodDeclarationSyntax freeNativeMethod = GenerateFreeNativeMethod(
                    structToMarshal,
                    dependencySortedFields,
                    setupStatements,
                    freeNativeCodeContext);
                nativeStruct = nativeStruct.AddMembers(freeNativeMethod);
            }
            else
            {
                // We shouldn't have any cleanup statements if we decided eariler that we didn't need the FreeNative method.
                Debug.Assert(!boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, freeNativeCodeContext)).Any());
            }

            if (structToMarshal.MarshallingFeatures.HasValueProperty)
            {
                nativeStruct = nativeStruct.AddMembers(CreateValuePropertyAndType(structToMarshal, generatorFactory, marshallingNotSupportedCallback).ToArray());
            }

            return nativeStruct;

            IEnumerable<int> GetInfoDependencies(TypePositionInfo info)
            {
                return MarshallerHelpers.GetDependentElementsOfMarshallingInfo(info.MarshallingAttributeInfo)
                    .Select(info => typePositionInfoToIndex[info]).ToList();
            }
        }

        private static IEnumerable<MemberDeclarationSyntax> CreateValuePropertyAndType(
            StructMarshallingContext structToMarshal,
            IMarshallingGeneratorFactory generatorFactory,
            Action<TypePositionInfo, MarshallingNotSupportedException> marshallingNotSupportedCallback)
        {
            AttributedMarshallingModelGeneratorFactory marshallerToValuePropertyGeneratorFactory = new(
                generatorFactory,
                new AttributedMarshallingModelGeneratorFactoryOptions(
                    false,
                    false,
                    ValidateScenarioSupport: false,
                    structToMarshal.MarshallingFeatures.HasValueProperty
                    ? AttributedMarshallingModelGenerationPhases.MarshallerTypeToValueProperty
                    : AttributedMarshallingModelGenerationPhases.All));

            ValuePropertyStubCodeContext codeContext = new ValuePropertyStubCodeContext().WithStage(StubCodeContext.Stage.Setup);

            ImmutableArray<BoundGenerator> boundGenerators = ImmutableArray.CreateRange(structToMarshal.Fields.Select(p => CreateGenerator(p, marshallingNotSupportedCallback, marshallerToValuePropertyGeneratorFactory, codeContext)));

            StructDeclarationSyntax valueStruct = StructDeclaration(MarshallerHelpers.GeneratedNativeStructValuePropertyTypeName).WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));

            valueStruct = valueStruct.AddMembers(CreateFields(boundGenerators));

            yield return valueStruct;

            PropertyDeclarationSyntax valueProperty = PropertyDeclaration(IdentifierName(MarshallerHelpers.GeneratedNativeStructValuePropertyTypeName), ManualTypeMarshallingHelper.ValuePropertyName)
                .WithAccessorList(AccessorList())
                .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));

            StatementSyntax[] setupStatements = boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, codeContext)).ToArray();

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.ManagedToNative))
            {
                ValuePropertyStubCodeContext marshalCodeContext = codeContext.WithStage(StubCodeContext.Stage.Marshal);

                StatementSyntax declareValueStatement = LocalDeclarationStatement(
                        VariableDeclaration(IdentifierName(MarshallerHelpers.GeneratedNativeStructValuePropertyTypeName),
                            SingletonSeparatedList(VariableDeclarator(Identifier("value"))
                                .WithInitializer(EqualsValueClause(LiteralExpression(SyntaxKind.DefaultLiteralExpression))))));

                AccessorDeclarationSyntax getAccessor = AccessorDeclaration(SyntaxKind.GetAccessorDeclaration)
                    .AddBodyStatements(declareValueStatement)
                    .AddBodyStatements(setupStatements)
                    .AddBodyStatements(boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, marshalCodeContext)).ToArray())
                    .AddBodyStatements(ReturnStatement(IdentifierName("value")));
                valueProperty = valueProperty.AddAccessorListAccessors(getAccessor);
            }
            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.NativeToManaged))
            {
                ValuePropertyStubCodeContext unmarshalCodeContext = codeContext.WithStage(StubCodeContext.Stage.Unmarshal);

                AccessorDeclarationSyntax setAccessor = AccessorDeclaration(SyntaxKind.SetAccessorDeclaration)
                    .AddBodyStatements(setupStatements)
                    .AddBodyStatements(boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, unmarshalCodeContext)).ToArray());
                valueProperty = valueProperty.AddAccessorListAccessors(setAccessor);
            }

            yield return valueProperty;
        }

        private static MethodDeclarationSyntax GenerateFreeNativeMethod(StructMarshallingContext structToMarshal, ImmutableArray<BoundGenerator> boundGenerators, StatementSyntax[] setupStatements, StubCodeContext freeNativeCodeContext)
        {
            StatementSyntax[] freeStatements = boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, freeNativeCodeContext)).ToArray();

            MethodDeclarationSyntax freeNativeMethod = MethodDeclaration(PredefinedType(Token(SyntaxKind.VoidKeyword)), "FreeNative")
                    .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));

            if (setupStatements.Length > 0)
            {
                StatementSyntax declareManagedStatement = LocalDeclarationStatement(
                        VariableDeclaration(IdentifierName(structToMarshal.Name),
                            SingletonSeparatedList(VariableDeclarator(Identifier("managed"))
                                .WithInitializer(EqualsValueClause(LiteralExpression(SyntaxKind.DefaultLiteralExpression))))));
                // We might need a local based on the managed value if we have setup statements.
                // Re-declare a dummy managed value.
                freeNativeMethod = freeNativeMethod.AddBodyStatements(declareManagedStatement);
            }

            freeNativeMethod = freeNativeMethod
                .AddBodyStatements(setupStatements)
                .AddBodyStatements(freeStatements);
            return freeNativeMethod;
        }

        private static MethodDeclarationSyntax GenerateToManagedMethod(StructMarshallingContext structToMarshal, StructMarshallingStubCodeContext codeContext, ImmutableArray<BoundGenerator> dependencySortedFields, StatementSyntax[] setupStatements)
        {
            MethodDeclarationSyntax toManagedMethod = MethodDeclaration(IdentifierName(structToMarshal.Name), "ToManaged")
                .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));

            StubCodeContext unmarshalCodeContext = codeContext.WithStage(StubCodeContext.Stage.Unmarshal);

            StatementSyntax declareManagedStatement = LocalDeclarationStatement(
                    VariableDeclaration(IdentifierName(structToMarshal.Name),
                        SingletonSeparatedList(VariableDeclarator(Identifier("managed"))
                            .WithInitializer(EqualsValueClause(LiteralExpression(SyntaxKind.DefaultLiteralExpression))))));

            toManagedMethod = toManagedMethod
                .AddBodyStatements(declareManagedStatement)
                .AddBodyStatements(setupStatements)
                .AddBodyStatements(dependencySortedFields.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, unmarshalCodeContext)).ToArray())
                .AddBodyStatements(ReturnStatement(IdentifierName("managed")));
            return toManagedMethod;
        }

        private static ConstructorDeclarationSyntax GenerateMarshallerConstructor(StructMarshallingContext structToMarshal, StructMarshallingStubCodeContext codeContext, ImmutableArray<BoundGenerator> dependencySortedFields, StatementSyntax[] setupStatements)
        {
            StubCodeContext marshalCodeContext = codeContext.WithStage(StubCodeContext.Stage.Marshal);

            ConstructorDeclarationSyntax managedToNativeConstructor = ConstructorDeclaration("__Native")
                .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)))
                .AddParameterListParameters(Parameter(Identifier("managed")).WithType(IdentifierName(structToMarshal.Name)));

            managedToNativeConstructor = managedToNativeConstructor
                .AddBodyStatements(setupStatements)
                .AddBodyStatements(dependencySortedFields.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, marshalCodeContext)).ToArray());
            return managedToNativeConstructor;
        }

        private static BoundGenerator CreateGenerator(
            TypePositionInfo p,
            Action<TypePositionInfo, MarshallingNotSupportedException> marshallingNotSupportedCallback,
            IMarshallingGeneratorFactory generatorFactory,
            StubCodeContext codeContext)
        {
            try
            {
                return new BoundGenerator(p, generatorFactory.Create(p, codeContext));
            }
            catch (MarshallingNotSupportedException e)
            {
                marshallingNotSupportedCallback(p, e);
                return new BoundGenerator(p, new Forwarder());
            }
        }

        private static FieldDeclarationSyntax[] CreateFields(ImmutableArray<BoundGenerator> boundGenerators)
        {
            List<FieldDeclarationSyntax> fieldDeclarationSyntaxes = new();
            foreach (BoundGenerator gen in boundGenerators)
            {
                if (gen.TypeInfo.MarshallingAttributeInfo is FixedBufferMarshallingInfo fixedBuffer)
                {
                    fieldDeclarationSyntaxes.Add(
                           FieldDeclaration(
                               VariableDeclaration(gen.Generator.AsNativeType(gen.TypeInfo),
                                   SingletonSeparatedList(
                                       VariableDeclarator(Identifier(gen.TypeInfo.InstanceIdentifier))
                                        .WithArgumentList(BracketedArgumentList(SingletonSeparatedList(Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(fixedBuffer.Size)))))))))
                           .WithModifiers(TokenList(Token(SyntaxKind.InternalKeyword), Token(SyntaxKind.FixedKeyword))));
                }
                else
                {
                    fieldDeclarationSyntaxes.Add(
                        FieldDeclaration(
                            VariableDeclaration(gen.Generator.AsNativeType(gen.TypeInfo),
                                SingletonSeparatedList(
                                    VariableDeclarator(Identifier(gen.TypeInfo.InstanceIdentifier)))))
                        .WithModifiers(TokenList(Token(SyntaxKind.InternalKeyword))));
                }
            }
            return fieldDeclarationSyntaxes.ToArray();
        }
    }
}
