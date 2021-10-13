// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Microsoft.CodeAnalysis.CSharp.SyntaxFactory;

namespace Microsoft.Interop.Generators
{
    internal class NonBlittableFixedBufferGenerator : IMarshallingGenerator
    {
        private const string IndexerIdentifier = "_i";
        private readonly IMarshallingGenerator _elementMarshaller;

        public NonBlittableFixedBufferGenerator(IMarshallingGenerator elementMarshaller)
        {
            _elementMarshaller = elementMarshaller;
        }

        public ArgumentSyntax AsArgument(TypePositionInfo info, StubCodeContext context) => throw new NotImplementedException();
        public TypeSyntax AsNativeType(TypePositionInfo info) => _elementMarshaller.AsNativeType(new TypePositionInfo(((FixedBufferMarshallingInfo)info.MarshallingAttributeInfo).ElementType, ((FixedBufferMarshallingInfo)info.MarshallingAttributeInfo).ElementMarshallingInfo));
        public ParameterSyntax AsParameter(TypePositionInfo info) => throw new NotImplementedException();
        public IEnumerable<StatementSyntax> Generate(TypePositionInfo info, StubCodeContext context)
        {
            var marshallingInfo = (FixedBufferMarshallingInfo)info.MarshallingAttributeInfo;
            var (managed, native) = context.GetIdentifiers(info);
            var nativePinned = context.GetAdditionalIdentifier(info, "pinned");
            var managedSpan = context.GetAdditionalIdentifier(info, "span");
            var fixedBufferContext = new FixedBufferElementMarshallingCodeContext(context.CurrentStage, managedSpan, IndexerIdentifier, context);
            var elementTypeInfo = new TypePositionInfo(marshallingInfo.ElementType, marshallingInfo.ElementMarshallingInfo)
            {
                InstanceIdentifier = info.InstanceIdentifier,
                RefKind = RefKind.Ref
            };
            switch (context.CurrentStage)
            {
                case StubCodeContext.Stage.Setup:
                    yield return LocalDeclarationStatement(
                        VariableDeclaration(
                            GenericName(Identifier("global::System.Span"), TypeArgumentList(SingletonSeparatedList(marshallingInfo.ElementType.Syntax))),
                            SingletonSeparatedList(VariableDeclarator(Identifier(managedSpan))
                                .WithInitializer(EqualsValueClause(
                                    ImplicitObjectCreationExpression(ArgumentList(SeparatedList(
                                        new[]
                                        {
                                            Argument(IdentifierName(managed)),
                                            Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size)))
                                        })),
                                        initializer: null))))));
                    break;
                case StubCodeContext.Stage.Marshal:
                case StubCodeContext.Stage.Unmarshal:
                    yield return
                        FixedStatement(VariableDeclaration(PointerType(PredefinedType(Token(SyntaxKind.VoidKeyword))),
                            SingletonSeparatedList(
                                VariableDeclarator(Identifier(nativePinned)).WithInitializer(EqualsValueClause(IdentifierName(native))))),
                            MarshallerHelpers.GetForLoop(managedSpan, IndexerIdentifier).WithStatement(
                                Block(_elementMarshaller.Generate(elementTypeInfo, fixedBufferContext))));
                    break;
                default:
                    break;
            }
        }

        public bool SupportsByValueMarshalKind(ByValueContentsMarshalKind marshalKind, StubCodeContext context) => false;
        public bool UsesNativeIdentifier(TypePositionInfo info, StubCodeContext context) => true;

        private sealed class FixedBufferElementMarshallingCodeContext : StubCodeContext
        {
            private readonly string _managedSpanIdentifier;

            public override bool SingleFrameSpansNativeContext => false;

            public override bool AdditionalTemporaryStateLivesAcrossStages => false;

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
                string managedSpanIdentifier,
                string indexerIdentifier,
                StubCodeContext parentContext)
            {
                CurrentStage = currentStage;
                _managedSpanIdentifier = managedSpanIdentifier;
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
                (string _, string native) = ParentContext!.GetIdentifiers(info);
                return (
                    $"{_managedSpanIdentifier}[{IndexerIdentifier}]",
                    $"{native}[{IndexerIdentifier}]"
                );
            }

            public override string GetAdditionalIdentifier(TypePositionInfo info, string name)
            {
                return $"{_managedSpanIdentifier}__{IndexerIdentifier}__{name}";
            }
        }
    }
}
