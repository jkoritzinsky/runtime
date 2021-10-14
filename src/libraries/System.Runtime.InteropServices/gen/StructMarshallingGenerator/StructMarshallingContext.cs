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

            ImmutableArray<ISymbol> members = type.GetMembers();
            for (int i = 0; i < members.Length; i++)
            {
                ISymbol member = members[i];
                if (member is not IFieldSymbol field)
                {
                    continue;
                }

                if (field.IsStatic || field.IsConst)
                {
                    continue;
                }

                fieldsBuilder.Add(
                    TypePositionInfo.CreateForField(
                        field,
                        parser.ParseMarshallingInfo(field, field.Type, field.GetAttributes()),
                        compilation) with
                    { ManagedIndex = i });
            }

            bool found = generatedStructMarshallingCache.TryGetGeneratedStructMarshallingFeatures(type, out GeneratedStructMarshallingFeatures marshallingFeatures);
            Debug.Assert(found);

            return new StructMarshallingContext(type.ContainingNamespace?.Name, type.Name, marshallingFeatures, fieldsBuilder.ToImmutable(), diagnostics.ToImmutable());
        }
    }
}
