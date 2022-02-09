// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Microsoft.CodeAnalysis.CSharp.SyntaxFactory;

namespace Microsoft.Interop.Generators
{
    internal sealed class NonBlittableFixedBufferGenerator : IMarshallingGenerator, ICustomNestedTypeGenerator
    {
        private const string IndexerIdentifier = "_i";
        private readonly IMarshallingGenerator _elementMarshaller;

        public NonBlittableFixedBufferGenerator(IMarshallingGenerator elementMarshaller)
        {
            _elementMarshaller = elementMarshaller;
        }

        public ArgumentSyntax AsArgument(TypePositionInfo info, StubCodeContext context) => throw new NotImplementedException();
        public TypeSyntax AsNativeType(TypePositionInfo info) => ParseTypeName(GetFixedBufferTypeName(info));
        public ParameterSyntax AsParameter(TypePositionInfo info) => throw new NotImplementedException();
        public IEnumerable<StatementSyntax> Generate(TypePositionInfo info, StubCodeContext context)
        {
            var marshallingInfo = (FixedBufferMarshallingInfo)info.MarshallingAttributeInfo;
            var (_, native) = context.GetIdentifiers(info);
            var nativePinned = context.GetAdditionalIdentifier(info, "pinned");
            var fixedBufferContext = new FixedBufferElementMarshallingCodeContext(context.CurrentStage, $"(({GetElementNativeType(info, marshallingInfo)}*){nativePinned})", IndexerIdentifier, context);
            var elementTypeInfo = new TypePositionInfo(marshallingInfo.ElementType, marshallingInfo.ElementMarshallingInfo)
            {
                InstanceIdentifier = info.InstanceIdentifier,
                RefKind = RefKind.Ref
            };
            LiteralExpressionSyntax loopIterationCountExpression = LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size));
            switch (context.CurrentStage)
            {
                case StubCodeContext.Stage.Marshal:
                case StubCodeContext.Stage.Unmarshal:
                    yield return
                        // fixed (void* <nativePinned> = &<native>)
                        FixedStatement(VariableDeclaration(PointerType(PredefinedType(Token(SyntaxKind.VoidKeyword))),
                            SingletonSeparatedList(
                                VariableDeclarator(Identifier(nativePinned)).WithInitializer(
                                    EqualsValueClause(PrefixUnaryExpression(SyntaxKind.AddressOfExpression, IdentifierName(native)))))),
                            MarshallerHelpers.GetForLoop(loopIterationCountExpression, IndexerIdentifier).WithStatement(
                                Block(_elementMarshaller.Generate(elementTypeInfo, fixedBufferContext))));
                    break;
                default:
                    break;
            }
        }

        public IEnumerable<TypeDeclarationSyntax> GetCustomNestedTypeDeclarations(TypePositionInfo info)
        {
            var marshallingInfo = (FixedBufferMarshallingInfo)info.MarshallingAttributeInfo;
            yield return StructDeclaration(GetFixedBufferTypeName(info))
                .WithMembers(SingletonList<MemberDeclarationSyntax>(
                    FieldDeclaration(
                        VariableDeclaration(
                            GetElementNativeType(info, marshallingInfo),
                            SeparatedList(Enumerable.Range(0, marshallingInfo.Size).Select(i => VariableDeclarator($"element{i}")))))))
                .WithModifiers(TokenList(Token(SyntaxKind.PublicKeyword)));
        }

        private TypeSyntax GetElementNativeType(TypePositionInfo info, FixedBufferMarshallingInfo marshallingInfo)
        {
            var elementTypeInfo = new TypePositionInfo(marshallingInfo.ElementType, marshallingInfo.ElementMarshallingInfo)
            {
                InstanceIdentifier = info.InstanceIdentifier,
                RefKind = RefKind.Ref
            };
            TypeSyntax nativeType = _elementMarshaller.AsNativeType(elementTypeInfo);
            return nativeType;
        }

        private static string GetFixedBufferTypeName(TypePositionInfo info) => $"{info.InstanceIdentifier}__Buffer";

        public bool SupportsByValueMarshalKind(ByValueContentsMarshalKind marshalKind, StubCodeContext context) => false;
        public bool UsesNativeIdentifier(TypePositionInfo info, StubCodeContext context) => true;

        public bool IsSupported(TargetFramework target, Version version)
        {
            return target is TargetFramework.Net && version.Major >= 6 && _elementMarshaller.IsSupported(target, version);
        }

        private sealed class FixedBufferElementMarshallingCodeContext : StubCodeContext
        {
            public override bool SingleFrameSpansNativeContext => false;

            public override bool AdditionalTemporaryStateLivesAcrossStages => false;

            public string NativePinnedIdentifier { get; }
            public string IndexerIdentifier { get; }

            /// <summary>
            /// Create a <see cref="StubCodeContext"/> for marshalling elements of a fuxed buffer.
            /// </summary>
            /// <param name="currentStage">The current marshalling stage.</param>
            /// <param name="indexerIdentifier">The indexer in the loop to get the element to marshal from the collection.</param>
            /// <param name="managedSpanIdentifier">The identifier of the native value storage cast to the target element type.</param>
            /// <param name="parentContext">The parent context.</param>
            public FixedBufferElementMarshallingCodeContext(
                Stage currentStage,
                string nativePinnedIdentifier,
                string indexerIdentifier,
                StubCodeContext parentContext)
            {
                CurrentStage = currentStage;
                NativePinnedIdentifier = nativePinnedIdentifier;
                IndexerIdentifier = indexerIdentifier;
                ParentContext = parentContext;
            }

            /// <summary>
            /// Get managed and native instance identifiers for the <paramref name="info"/>
            /// </summary>
            /// <param name="info">Object for which to get identifiers</param>
            /// <returns>Managed and native identifiers</returns>
            public override (string managed, string native) GetIdentifiers(TypePositionInfo info)
            {
                (string managed, _) = ParentContext!.GetIdentifiers(info);
                return (
                    $"{managed}[{IndexerIdentifier}]",
                    $"{NativePinnedIdentifier}[{IndexerIdentifier}]"
                );
            }

            public override string GetAdditionalIdentifier(TypePositionInfo info, string name)
            {
                return $"{GetIdentifiers(info).managed}__{IndexerIdentifier}__{name}";
            }
        }
    }
}
