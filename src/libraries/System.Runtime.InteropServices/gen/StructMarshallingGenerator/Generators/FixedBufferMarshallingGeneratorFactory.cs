// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Microsoft.Interop.Generators
{
    internal class FixedBufferMarshallingGeneratorFactory : IMarshallingGeneratorFactory
    {
        private readonly IMarshallingGeneratorFactory _innerFactory;
        private readonly List<StructDeclarationSyntax> _generatedNestedFixedBufferTypes = new();

        public FixedBufferMarshallingGeneratorFactory(IMarshallingGeneratorFactory innerFactory)
        {
            _innerFactory = innerFactory;
            ElementMarshallingGeneratorFactory = this;
        }

        public IMarshallingGeneratorFactory ElementMarshallingGeneratorFactory { get; set; }

        public IMarshallingGenerator Create(TypePositionInfo info, StubCodeContext context)
        {
            if (info.MarshallingAttributeInfo is not FixedBufferMarshallingInfo fixedBufferMarshalling)
            {
                return _innerFactory.Create(info, context);
            }

            IMarshallingGenerator elementMarshaller = ElementMarshallingGeneratorFactory.Create(new TypePositionInfo(fixedBufferMarshalling.ElementType, fixedBufferMarshalling.ElementMarshallingInfo), context);

            return (elementMarshaller, info.ManagedType) switch
            {
                (_, SzArrayType) => new ArrayFixedBufferGenerator(new NonBlittableFixedBufferGenerator(elementMarshaller)),
                (Utf16CharMarshaller, SpecialTypeInfo(_, _, SpecialType.System_String)) => new Utf16StringFixedBufferGenerator(),
                (BlittableMarshaller, _) => new BlittableFixedBufferGenerator(),
                (_, PointerTypeInfo) => new NonBlittableFixedBufferGenerator(elementMarshaller),
                _ => throw new MarshallingNotSupportedException(info, context)
            };
        }

        public IEnumerable<StructDeclarationSyntax> GeneratedNestedFixedBufferTypes => _generatedNestedFixedBufferTypes;
    }
}
