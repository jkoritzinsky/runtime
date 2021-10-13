// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;

namespace Microsoft.Interop
{
    internal sealed record StructMarshallingContext(
        string? Namespace,
        string Name,
        GeneratedStructMarshallingFeatures MarshallingFeatures,
        ImmutableArray<TypePositionInfo> Fields,
        ImmutableArray<Diagnostic> Diagnostics)
    {
        public override int GetHashCode() => throw new UnreachableException();

        public bool Equals(StructMarshallingContext other)
        {
            return MarshallingFeatures == other.MarshallingFeatures
                && Fields.SequenceEqual(other.Fields);
        }

        public static StructMarshallingContext Create(INamedTypeSymbol type, Compilation compilation, GeneratedStructMarshallingFeatureCache generatedStructMarshallingCache)
        {
            GeneratorDiagnostics diagnostics = new();

            MarshallingAttributeInfoParser parser = new MarshallingAttributeInfoParser(compilation, diagnostics, generatedStructMarshallingCache, new DefaultMarshallingInfo(CharEncoding.Undefined), type);

            ImmutableArray<TypePositionInfo>.Builder fieldsBuilder = ImmutableArray.CreateBuilder<TypePositionInfo>();

            foreach (ISymbol member in type.GetMembers())
            {
                if (member is not IFieldSymbol field)
                {
                    continue;
                }

                if (field.IsStatic || field.IsConst)
                {
                    continue;
                }

                ImmutableArray<AttributeData> attributes = field.GetAttributes();
                TypePositionInfo info = new TypePositionInfo(ManagedTypeInfo.CreateTypeInfoForTypeSymbol(field.Type), parser.ParseMarshallingInfo(field, field.Type, attributes))
                {
                    InstanceIdentifier = field.Name,
                    RefKind = RefKind.Ref
                };

                AttributeData? fieldOffset = attributes.FirstOrDefault(attr => attr.AttributeClass.ToDisplayString() == TypeNames.System_Runtime_InteropServices_FieldOffsetAttribute);

                if (fieldOffset is not null)
                {
                    int offset = (int)fieldOffset.ConstructorArguments[0].Value;
                    info = info with { ManagedIndex = offset, NativeIndex = offset };
                }

                fieldsBuilder.Add(info);
            }

            bool found = generatedStructMarshallingCache.TryGetGeneratedStructMarshallingFeatures(type, out GeneratedStructMarshallingFeatures marshallingFeatures);
            Debug.Assert(found);

            return new StructMarshallingContext(type.ContainingNamespace?.Name, type.Name, marshallingFeatures, fieldsBuilder.ToImmutable(), diagnostics.ToImmutable());
        }
    }
}
