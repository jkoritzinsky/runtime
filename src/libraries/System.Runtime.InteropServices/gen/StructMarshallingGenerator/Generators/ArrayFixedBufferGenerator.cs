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
    internal sealed class ArrayFixedBufferGenerator : IMarshallingGenerator, ICustomNestedTypeGenerator
    {
        private readonly IMarshallingGenerator _innerGenerator;

        public ArrayFixedBufferGenerator(IMarshallingGenerator innerGenerator)
        {
            _innerGenerator = innerGenerator;
        }

        public ArgumentSyntax AsArgument(TypePositionInfo info, StubCodeContext context) => _innerGenerator.AsArgument(info, context);
        public TypeSyntax AsNativeType(TypePositionInfo info) => _innerGenerator.AsNativeType(info);
        public ParameterSyntax AsParameter(TypePositionInfo info) => _innerGenerator.AsParameter(info);
        public IEnumerable<StatementSyntax> Generate(TypePositionInfo info, StubCodeContext context)
        {
            var (managed, _) = context.GetIdentifiers(info);
            var marshallingInfo = (FixedBufferMarshallingInfo)info.MarshallingAttributeInfo;
            ExpressionSyntax constSizeExpression = LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(marshallingInfo.Size));

            switch (context.CurrentStage)
            {
                case StubCodeContext.Stage.Marshal:
                    yield return IfStatement(BinaryExpression(SyntaxKind.EqualsExpression,
                        MemberAccessExpression(SyntaxKind.SimpleMemberAccessExpression,
                            IdentifierName(managed),
                            IdentifierName("Length")),
                        constSizeExpression),
                        ThrowStatement(ObjectCreationExpression(ParseTypeName(TypeNames.System_ArgumentException)).WithArgumentList(ArgumentList())));
                    break;
                case StubCodeContext.Stage.Unmarshal:
                    yield return ExpressionStatement(
                        AssignmentExpression(SyntaxKind.SimpleAssignmentExpression,
                            IdentifierName(managed),
                            ArrayCreationExpression(ArrayType(marshallingInfo.ElementType.Syntax,
                                SingletonList(ArrayRankSpecifier(
                                    SingletonSeparatedList(constSizeExpression)))))));
                    break;
                default:
                    break;
            }

            foreach (StatementSyntax statement in _innerGenerator.Generate(info, context))
            {
                yield return statement;
            }
        }

        public IEnumerable<TypeDeclarationSyntax> GetCustomNestedTypeDeclarations(TypePositionInfo info)
            => _innerGenerator is ICustomNestedTypeGenerator customNestedTypeGenerator ? customNestedTypeGenerator.GetCustomNestedTypeDeclarations(info) : Array.Empty<TypeDeclarationSyntax>();
        public bool SupportsByValueMarshalKind(ByValueContentsMarshalKind marshalKind, StubCodeContext context) => _innerGenerator.SupportsByValueMarshalKind(marshalKind, context);
        public bool UsesNativeIdentifier(TypePositionInfo info, StubCodeContext context) => _innerGenerator.UsesNativeIdentifier(info, context);
        public bool IsSupported(TargetFramework target, Version version)
        {
            return target is TargetFramework.Net && version.Major >= 6;
        }
    }
}
