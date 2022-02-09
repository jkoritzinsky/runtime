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
    internal sealed class Utf16StringFixedBufferGenerator : IMarshallingGenerator
    {

        public ArgumentSyntax AsArgument(TypePositionInfo info, StubCodeContext context) => throw new UnreachableException();
        public TypeSyntax AsNativeType(TypePositionInfo info) => PredefinedType(Token(SyntaxKind.UShortKeyword));
        public ParameterSyntax AsParameter(TypePositionInfo info) => throw new UnreachableException();
        public IEnumerable<StatementSyntax> Generate(TypePositionInfo info, StubCodeContext context)
        {
            var (managed, native) = context.GetIdentifiers(info);
            string nativePinned = context.GetAdditionalIdentifier(info, "pinned");
            string numCharsIdentifier = context.GetAdditionalIdentifier(info, "numChars");
            var marshallingInfo = (FixedBufferMarshallingInfo)info.MarshallingAttributeInfo;
            ExpressionSyntax constSizeExpression = LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size));

            switch (context.CurrentStage)
            {
                case StubCodeContext.Stage.Marshal:
                    // fixed (ushort* <nativePinned> = <native>)
                    yield return FixedStatement(VariableDeclaration(PointerType(PredefinedType(Token(SyntaxKind.VoidKeyword))),
                        SingletonSeparatedList(
                            VariableDeclarator(Identifier(nativePinned)).WithInitializer(
                                EqualsValueClause(IdentifierName(native))))),
                        Block(
                            // int <numChars> = Math.Min(<managed>.Length, <size - 1>);
                            LocalDeclarationStatement(
                                VariableDeclaration(
                                    PredefinedType(
                                        Token(SyntaxKind.IntKeyword)),
                                    SingletonSeparatedList(
                                        VariableDeclarator(
                                            Identifier(numCharsIdentifier))
                                        .WithInitializer(
                                            EqualsValueClause(
                                                InvocationExpression(
                                                    MemberAccessExpression(
                                                        SyntaxKind.SimpleMemberAccessExpression,
                                                        ParseTypeName(TypeNames.System_Math),
                                                        IdentifierName("Min")))
                                                .WithArgumentList(
                                                    ArgumentList(
                                                        SeparatedList(
                                                            new[]
                                                            {
                                                                Argument(
                                                                    MemberAccessExpression(
                                                                        SyntaxKind.SimpleMemberAccessExpression,
                                                                        IdentifierName(managed),
                                                                        IdentifierName("Length"))),
                                                                Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size - 1)))
                                                            })))))))),
                            // ((ReadOnlySpan<char>)<managed>)[..<numChars>].CopyTo(new Span<char>((char*)<nativePinned>, <numChars>));
                            ExpressionStatement(
                            InvocationExpression(
                                MemberAccessExpression(
                                    SyntaxKind.SimpleMemberAccessExpression,
                                    ElementAccessExpression(
                                        ParenthesizedExpression(
                                        CastExpression(
                                            GenericName(Identifier("System.ReadOnlySpan"),
                                                TypeArgumentList(SingletonSeparatedList<TypeSyntax>(
                                                    PredefinedType(Token(SyntaxKind.CharKeyword))))),
                                            IdentifierName(managed))),
                                        BracketedArgumentList(SingletonSeparatedList(Argument(RangeExpression().WithRightOperand(IdentifierName(numCharsIdentifier)))))),
                                    IdentifierName("CopyTo")),
                                ArgumentList(
                                    SeparatedList(
                                        new[]
                                        {
                                            Argument(
                                                ObjectCreationExpression(
                                                    GenericName(Identifier(TypeNames.System_Span),
                                                        TypeArgumentList(SingletonSeparatedList<TypeSyntax>(
                                                            PredefinedType(Token(SyntaxKind.CharKeyword))))),
                                                    ArgumentList(
                                                        SeparatedList(
                                                            new[]
                                                            {
                                                                Argument(IdentifierName(nativePinned)),
                                                                Argument(IdentifierName(numCharsIdentifier))
                                                            })),
                                                    initializer: null))
                                        })))),
                            // <nativePinned>[<size>] = 0;
                            ExpressionStatement(
                                AssignmentExpression(
                                    SyntaxKind.SimpleAssignmentExpression,
                                    ElementAccessExpression(
                                        IdentifierName(native),
                                        BracketedArgumentList(
                                            SingletonSeparatedList(
                                                Argument(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size - 1)))))),
                                    LiteralExpression(
                                        SyntaxKind.NumericLiteralExpression,
                                        Literal(0))))));
                    break;
                case StubCodeContext.Stage.Unmarshal:
                    string nativeSpanIdentifier = context.GetAdditionalIdentifier(info, "nativeSpan");
                    // fixed (ushort* <nativePinned> = <native>)
                    yield return FixedStatement(VariableDeclaration(PointerType(PredefinedType(Token(SyntaxKind.VoidKeyword))),
                        SingletonSeparatedList(
                            VariableDeclarator(Identifier(nativePinned)).WithInitializer(
                                EqualsValueClause(IdentifierName(native))))),
                        Block(
                            // Span<ushort> <nativeSpan> = new Span<char>((char*)<nativePinned>, <size>);
                            LocalDeclarationStatement(
                                VariableDeclaration(
                                    GenericName(
                                        Identifier(TypeNames.System_Span))
                                    .WithTypeArgumentList(
                                        TypeArgumentList(
                                            SingletonSeparatedList<TypeSyntax>(
                                                PredefinedType(
                                                    Token(SyntaxKind.UShortKeyword))))),
                                    SingletonSeparatedList(
                                        VariableDeclarator(Identifier(nativeSpanIdentifier))
                                        .WithInitializer(EqualsValueClause(
                                            ImplicitObjectCreationExpression()
                                            .WithArgumentList(
                                                ArgumentList(
                                                    SeparatedList(
                                                        new[]
                                                        {
                                                            Argument(IdentifierName(nativePinned)),
                                                            Argument(constSizeExpression)
                                                        })))))))),
                            LocalDeclarationStatement(
                                VariableDeclaration(
                                    PredefinedType(Token(SyntaxKind.IntKeyword)),
                                    SingletonSeparatedList(
                                        VariableDeclarator(
                                            Identifier(numCharsIdentifier))
                                        .WithInitializer(
                                            EqualsValueClause(
                                                InvocationExpression(
                                                    MemberAccessExpression(
                                                        SyntaxKind.SimpleMemberAccessExpression,
                                                        ParseTypeName(TypeNames.System_MemoryExtensions),
                                                        IdentifierName("IndexOf")))
                                                .AddArgumentListArguments(
                                                            Argument(IdentifierName(nativeSpanIdentifier)),
                                                            Argument(
                                                                CastExpression(PredefinedType(Token(SyntaxKind.UShortKeyword)),
                                                                    LiteralExpression(
                                                                        SyntaxKind.NumericLiteralExpression,
                                                                        Literal(0)))))))))),
                            // if (<numChars> == -1) <numChars> = <size>;
                            IfStatement(
                                BinaryExpression(
                                    SyntaxKind.EqualsExpression,
                                    IdentifierName(numCharsIdentifier),
                                    PrefixUnaryExpression(
                                        SyntaxKind.UnaryMinusExpression,
                                        LiteralExpression(
                                            SyntaxKind.NumericLiteralExpression,
                                            Literal(1)))),
                                ExpressionStatement(
                                            AssignmentExpression(
                                                SyntaxKind.SimpleAssignmentExpression,
                                                IdentifierName(numCharsIdentifier),
                                                constSizeExpression))),
                            // managed = new string((char*)<nativePinned>, 0, <numChars>)
                            ExpressionStatement(
                                AssignmentExpression(SyntaxKind.SimpleAssignmentExpression,
                                IdentifierName(managed),
                                ObjectCreationExpression(
                                    PredefinedType(
                                        Token(SyntaxKind.StringKeyword)))
                                .WithArgumentList(
                                    ArgumentList(
                                        SeparatedList(
                                            new[]
                                            {
                                                Argument(
                                                    CastExpression(
                                                        PointerType(
                                                            PredefinedType(
                                                                Token(SyntaxKind.CharKeyword))),
                                                        IdentifierName(nativePinned))),
                                                Argument(
                                                    LiteralExpression(
                                                        SyntaxKind.NumericLiteralExpression,
                                                        Literal(0))),
                                                Argument(
                                                    IdentifierName(numCharsIdentifier))
                                            })))))));
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
