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

        public static StructDeclarationSyntax GenerateStructMarshallingCode(
            StructMarshallingContext structToMarshal,
            Action<TypePositionInfo, MarshallingNotSupportedException> marshallingNotSupportedCallback,
            IMarshallingGeneratorFactory generatorFactory)
        {
            // TODO: Add support for emitting the Value property and other advanced custom marshalling features accurately.
            Debug.Assert(!structToMarshal.MarshallingFeatures.HasValueProperty);

            StructDeclarationSyntax nativeStruct = StructDeclaration("__Native")
                .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword), Token(SyntaxKind.UnsafeKeyword)));

            StructMarshallingStubCodeContext codeContext = new StructMarshallingStubCodeContext().WithStage(StubCodeContext.Stage.Setup);

            ImmutableArray<BoundGenerator> boundGenerators = ImmutableArray.CreateRange(structToMarshal.Fields.Select(CreateGenerator));

            nativeStruct = nativeStruct.AddMembers(CreateFields(boundGenerators));

            Dictionary<TypePositionInfo, int> typePositionInfoToIndex = structToMarshal.Fields.Select(static (field, index) => (field, index)).ToDictionary(value => value.field, value => value.index);

            IEnumerable<BoundGenerator> dependencySortedFields = MarshallerHelpers.GetTopologicallySortedElements(
                boundGenerators,
                m => typePositionInfoToIndex[m.TypeInfo],
                m => GetInfoDependencies(m.TypeInfo))
                .ToList();

            StatementSyntax[] setupStatements = boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, codeContext)).ToArray();

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.ManagedToNative))
            {
                StubCodeContext marshalCodeContext = codeContext.WithStage(StubCodeContext.Stage.Marshal);

                ConstructorDeclarationSyntax managedToNativeConstructor = ConstructorDeclaration("__Native")
                    .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)))
                    .AddParameterListParameters(Parameter(Identifier("managed")).WithType(IdentifierName(structToMarshal.Name)));

                managedToNativeConstructor = managedToNativeConstructor.AddBodyStatements(setupStatements).AddBodyStatements(boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, marshalCodeContext)).ToArray());

                nativeStruct = nativeStruct.AddMembers(managedToNativeConstructor);
            }

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.NativeToManaged))
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
                    .AddBodyStatements(boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, unmarshalCodeContext)).ToArray())
                    .AddBodyStatements(ReturnStatement(IdentifierName("managed")));

                nativeStruct = nativeStruct.AddMembers(toManagedMethod);
            }

            StubCodeContext freeNativeCodeContext = codeContext.WithStage(StubCodeContext.Stage.Cleanup);

            if (structToMarshal.MarshallingFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.FreeNativeResources))
            {
                StatementSyntax[] freeStatements = boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, freeNativeCodeContext)).ToArray();

                MethodDeclarationSyntax freeNativeMethod = MethodDeclaration(PredefinedType(Token(SyntaxKind.VoidKeyword)), "FreeNative")
                        .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));

                if (setupStatements.Length > 0)
                {
                    // We might need a local based on the managed value if we have setup statements.
                    // Re-declare a dummy managed value.
                    freeNativeMethod = freeNativeMethod.AddBodyStatements(setupStatements);
                }

                freeNativeMethod = freeNativeMethod
                    .AddBodyStatements(setupStatements)
                    .AddBodyStatements(freeStatements);
                nativeStruct = nativeStruct.AddMembers(freeNativeMethod);
            }
            else
            {
                // We shouldn't have any cleanup statements if we decided eariler that we didn't need the FreeNative method.
                Debug.Assert(!boundGenerators.SelectMany(gen => gen.Generator.Generate(gen.TypeInfo, freeNativeCodeContext)).Any());
            }

            return nativeStruct;

            IEnumerable<int> GetInfoDependencies(TypePositionInfo info)
            {
                return MarshallerHelpers.GetDependentElementsOfMarshallingInfo(info.MarshallingAttributeInfo)
                    .Select(info => typePositionInfoToIndex[info]).ToList();
            }

            BoundGenerator CreateGenerator(TypePositionInfo p)
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
                           .WithModifiers(TokenList(Token(SyntaxKind.FixedKeyword))));
                }
                else
                {
                    fieldDeclarationSyntaxes.Add(
                        FieldDeclaration(
                            VariableDeclaration(gen.Generator.AsNativeType(gen.TypeInfo),
                                SingletonSeparatedList(
                                    VariableDeclarator(Identifier(gen.TypeInfo.InstanceIdentifier))))));
                }
            }
            return fieldDeclarationSyntaxes.ToArray();
        }
    }
}
