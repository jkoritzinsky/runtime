// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Text;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Microsoft.CodeAnalysis.CSharp.SyntaxFactory;

namespace Microsoft.Interop.Generators
{
    internal sealed class BlittableFixedBufferGenerator : IMarshallingGenerator
    {
        public ArgumentSyntax AsArgument(TypePositionInfo info, StubCodeContext context) => throw new NotImplementedException();
        public TypeSyntax AsNativeType(TypePositionInfo info) => ((FixedBufferMarshallingInfo)info.MarshallingAttributeInfo).ElementType.Syntax;
        public ParameterSyntax AsParameter(TypePositionInfo info) => throw new NotImplementedException();
        public IEnumerable<StatementSyntax> Generate(TypePositionInfo info, StubCodeContext context)
        {
            var marshallingInfo = (FixedBufferMarshallingInfo)info.MarshallingAttributeInfo;
            var (managed, native) = context.GetIdentifiers(info);
            var nativePinned = context.GetAdditionalIdentifier(info, "pinned");
            switch (context.CurrentStage)
            {
                case StubCodeContext.Stage.Marshal:
                    yield return
                        FixedStatement(VariableDeclaration(PointerType(PredefinedType(Token(SyntaxKind.VoidKeyword))),
                            SingletonSeparatedList(
                                VariableDeclarator(Identifier(nativePinned)).WithInitializer(EqualsValueClause(IdentifierName(native))))),
                            ExpressionStatement(
                                InvocationExpression(
                                    MemberAccessExpression(SyntaxKind.SimpleMemberAccessExpression,
                                        ObjectCreationExpression(
                                            GenericName(Identifier("global::System.Span"), TypeArgumentList(SingletonSeparatedList(marshallingInfo.ElementType.Syntax))),
                                            ArgumentList(SeparatedList(
                                                new[]
                                                {
                                                    Argument(IdentifierName(managed)),
                                                    Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size)))
                                                })),
                                            initializer: null),
                                    IdentifierName("CopyTo")),
                                    ArgumentList(
                                        SingletonSeparatedList(
                                            Argument(
                                                ObjectCreationExpression(
                                                    GenericName(Identifier("global::System.Span"), TypeArgumentList(SingletonSeparatedList(marshallingInfo.ElementType.Syntax))),
                                                    ArgumentList(SeparatedList(
                                                        new[]
                                                        {
                                                            Argument(IdentifierName(nativePinned)),
                                                            Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size)))
                                                        })),
                                            initializer: null)))))));
                    break;
                case StubCodeContext.Stage.Unmarshal:
                    yield return
                        FixedStatement(VariableDeclaration(PointerType(PredefinedType(Token(SyntaxKind.VoidKeyword))),
                            SingletonSeparatedList(
                                VariableDeclarator(Identifier(nativePinned)).WithInitializer(EqualsValueClause(IdentifierName(native))))),
                            ExpressionStatement(
                                InvocationExpression(
                                    MemberAccessExpression(SyntaxKind.SimpleMemberAccessExpression,
                                        ObjectCreationExpression(
                                            GenericName(Identifier("global::System.Span"), TypeArgumentList(SingletonSeparatedList(marshallingInfo.ElementType.Syntax))),
                                            ArgumentList(SeparatedList(
                                                new[]
                                                {
                                                    Argument(IdentifierName(nativePinned)),
                                                    Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size)))
                                                })),
                                            initializer: null),
                                    IdentifierName("CopyTo")),
                                    ArgumentList(
                                        SingletonSeparatedList(
                                            Argument(
                                                ObjectCreationExpression(
                                                    GenericName(Identifier("global::System.Span"), TypeArgumentList(SingletonSeparatedList(marshallingInfo.ElementType.Syntax))),
                                                    ArgumentList(SeparatedList(
                                                        new[]
                                                        {
                                                            Argument(IdentifierName(managed)),
                                                            Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size)))
                                                        })),
                                            initializer: null)))))));
                    break;
                default:
                    break;
            }
        }

        public bool SupportsByValueMarshalKind(ByValueContentsMarshalKind marshalKind, StubCodeContext context) => false;
        public bool UsesNativeIdentifier(TypePositionInfo info, StubCodeContext context) => true;

        public bool IsSupported(TargetFramework target, Version version)
        {
            return target is TargetFramework.Net && version.Major >= 6;
        }
    }
}
