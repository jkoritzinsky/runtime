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
            FixedBufferMarshallingGeneratorFactory generatorFactory)
        {
            // TODO: Add support for emitting other advanced custom marshalling features accurately.

            AttributedMarshallingModelGeneratorFactory managedToMarshalerGeneratorFactory = new(
                generatorFactory,
                generatorFactory.ElementMarshallingGeneratorFactory,
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

            nativeStruct = nativeStruct.AddMembers(CreateFields(boundGenerators)).AddMembers(CreateNestedTypes(boundGenerators));

            ImmutableArray<BoundGenerator> dependencySortedFields = MarshallerHelpers.GetTopologicallySortedElements(
                boundGenerators,
                m => m.TypeInfo.ManagedIndex,
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

            StructMarshallingStubCodeContext freeNativeCodeContext = codeContext.WithStage(StubCodeContext.Stage.Cleanup);

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

            static IEnumerable<int> GetInfoDependencies(TypePositionInfo info)
            {
                return MarshallerHelpers.GetDependentElementsOfMarshallingInfo(info.MarshallingAttributeInfo)
                    .Select(info => info.ManagedIndex).ToList();
            }
        }

        private static IEnumerable<MemberDeclarationSyntax> CreateValuePropertyAndType(
            StructMarshallingContext structToMarshal,
            FixedBufferMarshallingGeneratorFactory generatorFactory,
            Action<TypePositionInfo, MarshallingNotSupportedException> marshallingNotSupportedCallback)
        {
            AttributedMarshallingModelGeneratorFactory marshallerToValuePropertyGeneratorFactory = new(
                generatorFactory,
                generatorFactory.ElementMarshallingGeneratorFactory,
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

        private static MethodDeclarationSyntax GenerateFreeNativeMethod(
            StructMarshallingContext structToMarshal,
            ImmutableArray<BoundGenerator> orderedElements,
            StatementSyntax[] setupStatements,
            StructMarshallingStubCodeContext freeNativeCodeContext)
        {
            MethodDeclarationSyntax freeNativeMethod = MethodDeclaration(PredefinedType(Token(SyntaxKind.VoidKeyword)), "FreeNative")
                    .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));
            List<StatementSyntax> freeStatements = new();

            // re-unmarshal any dependent values so they can be available for the cleanup of the depending nodes if there are any dependencies.
            HashSet<int> dependentFieldMetadataIndices = new();
            foreach (BoundGenerator gen in orderedElements)
            {
                foreach (TypePositionInfo dependent in MarshallerHelpers.GetDependentElementsOfMarshallingInfo(gen.TypeInfo.MarshallingAttributeInfo))
                {
                    dependentFieldMetadataIndices.Add(dependent.ManagedIndex);
                }
            }

            // If we have no nodes that have dependencies, then we don't need to redo any unmarshalling.
            if (dependentFieldMetadataIndices.Count != 0)
            {
                // Declare the managed value local again.
                StatementSyntax declareManagedStatement = LocalDeclarationStatement(
                        VariableDeclaration(IdentifierName(structToMarshal.Name),
                            SingletonSeparatedList(VariableDeclarator(Identifier("managed"))
                                .WithInitializer(EqualsValueClause(LiteralExpression(SyntaxKind.DefaultLiteralExpression))))));
                freeNativeMethod = freeNativeMethod
                    .AddBodyStatements(declareManagedStatement);

                var unmarshalCodeContext = freeNativeCodeContext.WithStage(StubCodeContext.Stage.Unmarshal);
                for (int i = 0; i < orderedElements.Length; i++)
                {
                    // Don't unmarshal any elements that no other nodes depend on.
                    if (dependentFieldMetadataIndices.Contains(orderedElements[i].TypeInfo.ManagedIndex))
                    {
                        freeStatements.AddRange(orderedElements[i].Generator.Generate(orderedElements[i].TypeInfo, unmarshalCodeContext));
                    }
                }
            }

            // Loop through the ordered elements in reverse to ensure that we free dependent elements after all elements that depend on it
            for (int i = orderedElements.Length - 1; i >= 0; i--)
            {
                freeStatements.AddRange(orderedElements[i].Generator.Generate(orderedElements[i].TypeInfo, freeNativeCodeContext));
            }

            freeNativeMethod = freeNativeMethod
                .AddBodyStatements(setupStatements)
                .AddBodyStatements(freeStatements.ToArray());
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
            var fieldDeclarationSyntaxes = new FieldDeclarationSyntax[boundGenerators.Length];
            for (int i = 0; i < boundGenerators.Length; i++)
            {
                BoundGenerator gen = boundGenerators[i];
                // The fields is a fixed buffer and we aren't going to generate a new type for the native representation,
                // then generate a fixed buffer field.
                if (gen.TypeInfo.MarshallingAttributeInfo is FixedBufferMarshallingInfo fixedBuffer && gen.Generator is not ICustomNestedTypeGenerator)
                {
                    fieldDeclarationSyntaxes[i] = FieldDeclaration(
                            VariableDeclaration(gen.Generator.AsNativeType(gen.TypeInfo),
                                SingletonSeparatedList(
                                    VariableDeclarator(Identifier(gen.TypeInfo.InstanceIdentifier))
                                    .WithArgumentList(BracketedArgumentList(SingletonSeparatedList(Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(fixedBuffer.Size)))))))))
                        .WithModifiers(TokenList(Token(SyntaxKind.InternalKeyword), Token(SyntaxKind.FixedKeyword)));
                }
                else
                {
                    fieldDeclarationSyntaxes[i] = FieldDeclaration(
                        VariableDeclaration(gen.Generator.AsNativeType(gen.TypeInfo),
                            SingletonSeparatedList(
                                VariableDeclarator(Identifier(gen.TypeInfo.InstanceIdentifier)))))
                    .WithModifiers(TokenList(Token(SyntaxKind.InternalKeyword)));
                }
            }
            return fieldDeclarationSyntaxes;
        }

        private static TypeDeclarationSyntax[] CreateNestedTypes(ImmutableArray<BoundGenerator> boundGenerators)
        {
            List<TypeDeclarationSyntax> nestedTypes = new();

            foreach (BoundGenerator gen in boundGenerators)
            {
                if (gen.Generator is ICustomNestedTypeGenerator nestedTypeGen)
                {
                    nestedTypes.AddRange(nestedTypeGen.GetCustomNestedTypeDeclarations(gen.TypeInfo));
                }
            }
            return nestedTypes.ToArray();
        }

    }
}
