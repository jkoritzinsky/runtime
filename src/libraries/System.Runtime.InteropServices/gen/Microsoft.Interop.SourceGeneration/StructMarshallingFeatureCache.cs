// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Microsoft.Interop
{
    public readonly record struct GeneratedStructMarshallingFeatures(CustomMarshallingFeatures MarshallingFeatures, bool HasValueProperty, bool MarshallerMustBeRefStruct, bool IsBlittable);

    public sealed class StructMarshallingFeatureCache : IEquatable<StructMarshallingFeatureCache>
    {
        private enum StructAttribute
        {
            None,
            GeneratedMarshalling,
            NativeMarshalling,
            Blittable,
        }

        private readonly ImmutableDictionary<INamedTypeSymbol, GeneratedStructMarshallingFeatures> _cache;

        private readonly Compilation _compilation = null!;
        private readonly INamedTypeSymbol _structLayoutAttributeType = null!;

        internal StructMarshallingFeatureCache()
        {
            _cache = ImmutableDictionary<INamedTypeSymbol, GeneratedStructMarshallingFeatures>.Empty;
        }

        internal StructMarshallingFeatureCache(ImmutableArray<INamedTypeSymbol> generatedStructTypes, Compilation compilation)
        {
            _compilation = compilation;
            _structLayoutAttributeType = compilation.ObjectType.ContainingAssembly.GetTypeByMetadataName(TypeNames.System_Runtime_InteropServices_StructLayoutAttribute)!;

            var cacheBuilder = ImmutableDictionary.CreateBuilder<INamedTypeSymbol, GeneratedStructMarshallingFeatures>(SymbolEqualityComparer.Default);

            var typeToIndexMap = generatedStructTypes.Select((s, i) => (s, i)).ToImmutableDictionary(pair => pair.s, pair => pair.i, SymbolEqualityComparer.Default);

            ImmutableArray<INamedTypeSymbol> sortedTypes = ImmutableArray.CreateRange(MarshallerHelpers.GetTopologicallySortedElements(
                generatedStructTypes,
                generatedStructType => typeToIndexMap[generatedStructType],
                GetStructTypeDependencies));

            foreach (INamedTypeSymbol type in sortedTypes)
            {
                StructAttribute attributeKind = GetStructMarshallingAttributeKind(type);
                if (attributeKind == StructAttribute.GeneratedMarshalling)
                {
                    cacheBuilder.Add(type, GenerateMarshallingInfoForGeneratedMarshallingType(compilation, cacheBuilder, type));
                }
                else if (attributeKind == StructAttribute.None)
                {
                    cacheBuilder.Add(type, GenerateMarshallingInfoForPossiblyBlittableType(compilation, cacheBuilder, type, allowExposureOutsideOfCompilation: false));
                }
                else if (attributeKind == StructAttribute.Blittable)
                {
                    if (type.IsGenericType && !SymbolEqualityComparer.Default.Equals(type, type.ConstructedFrom))
                    {
                        // For generic types, we will recalculate if the blittable type is actually blittable
                        cacheBuilder.Add(type, GenerateMarshallingInfoForPossiblyBlittableType(compilation, cacheBuilder, type, allowExposureOutsideOfCompilation: true));
                    }
                    else
                    {
                        // If the type is not generic, assume that the type is blittable.
                        cacheBuilder.Add(type, new GeneratedStructMarshallingFeatures(
                            CustomMarshallingFeatures.ManagedToNative | CustomMarshallingFeatures.NativeToManaged,
                            HasValueProperty: false,
                            MarshallerMustBeRefStruct: false,
                            IsBlittable: true));
                    }
                }
            }

            _cache = cacheBuilder.ToImmutable();

            IEnumerable<int> GetStructTypeDependencies(INamedTypeSymbol generatedStructType)
            {
                if (generatedStructType.SpecialType.IsSpecialTypePrimitive() || generatedStructType.TypeKind == TypeKind.Enum)
                {
                    yield break;
                }

                foreach (ISymbol member in generatedStructType.GetMembers())
                {
                    if (member is not IFieldSymbol { IsStatic: false } field)
                    {
                        continue;
                    }
                    if (typeToIndexMap.TryGetValue(field.Type, out int index))
                    {
                        yield return index;
                    }
                }
            }
        }

        /// <summary>
        /// Checks if the type is blittable without considering any attributes on the current type.
        /// Requires the cache to already contain all type information required to determine if the fields are blittable, otherwise returns false.
        /// </summary>
        /// <param name="namedType">The type to check if blittable.</param>
        /// <param name="allowExposureOutsideOfCompilation">Allow the type to be blittable if it is exposed outside of the assembly.</param>
        /// <returns>If the type is blittable, true. Otherwise, false.</returns>
        public bool SpeculativeTypeIsBlittable(INamedTypeSymbol namedType, bool allowExposureOutsideOfCompilation)
        {
            return namedType.TypeKind is TypeKind.Struct or TypeKind.Enum
                ? GenerateMarshallingInfoForPossiblyBlittableType(_compilation, _cache, namedType, allowExposureOutsideOfCompilation).IsBlittable
                : false;
        }

        private static StructAttribute GetStructMarshallingAttributeKind(INamedTypeSymbol type)
        {
            foreach (AttributeData attr in type.GetAttributes())
            {
                if (attr.AttributeClass.ToDisplayString() == TypeNames.GeneratedMarshallingAttribute)
                {
                    return StructAttribute.GeneratedMarshalling;
                }
                else if (attr.AttributeClass.ToDisplayString() == TypeNames.NativeMarshallingAttribute)
                {
                    return StructAttribute.NativeMarshalling;
                }
                else if (attr.AttributeClass.ToDisplayString() == TypeNames.BlittableTypeAttribute)
                {
                    return StructAttribute.Blittable;
                }
            }

            return StructAttribute.None;
        }

        private GeneratedStructMarshallingFeatures GenerateMarshallingInfoForPossiblyBlittableType(
            Compilation compilation,
            IDictionary<INamedTypeSymbol, GeneratedStructMarshallingFeatures> cache,
            INamedTypeSymbol type,
            bool allowExposureOutsideOfCompilation)
        {
            if (type.SpecialType.IsSpecialTypeBlittable())
            {
                return new GeneratedStructMarshallingFeatures(
                    CustomMarshallingFeatures.ManagedToNative | CustomMarshallingFeatures.NativeToManaged,
                    HasValueProperty: false,
                    MarshallerMustBeRefStruct: false,
                    IsBlittable: true);
            }

            if (type is INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: { SpecialType: SpecialType enumUnderlyingType } } && enumUnderlyingType.IsSpecialTypeBlittable())
            {
                return new GeneratedStructMarshallingFeatures(
                    CustomMarshallingFeatures.ManagedToNative | CustomMarshallingFeatures.NativeToManaged,
                    HasValueProperty: false,
                    MarshallerMustBeRefStruct: false,
                    IsBlittable: true);
            }

            if (!allowExposureOutsideOfCompilation && type.IsExposedOutsideOfCurrentCompilation())
            {
                return new GeneratedStructMarshallingFeatures(CustomMarshallingFeatures.None, HasValueProperty: false, MarshallerMustBeRefStruct: false, IsBlittable: false);
            }


            if (type.IsAutoLayout(_structLayoutAttributeType))
            {
                return new GeneratedStructMarshallingFeatures(CustomMarshallingFeatures.None, HasValueProperty: false, MarshallerMustBeRefStruct: false, IsBlittable: false);
            }

            // We use an empty cache here as all cases that use the cache should be caught early by the topological sort.
            MarshallingAttributeInfoParser parser = new MarshallingAttributeInfoParser(compilation, new NullDiagnosticsSink(), new StructMarshallingFeatureCache(), new DefaultMarshallingInfo(CharEncoding.Undefined), type);
            bool isBlittable = true;
            foreach (ISymbol member in type.GetMembers())
            {
                if (member is not IFieldSymbol { IsStatic: false, IsConst: false } field)
                {
                    continue;
                }
                if (field.Type is INamedTypeSymbol namedType && cache.TryGetValue(namedType, out GeneratedStructMarshallingFeatures generatedFeatures))
                {
                    isBlittable &= generatedFeatures.IsBlittable;
                }
                else
                {
                    MarshallingInfo marshallingInfo = parser.ParseMarshallingInfo(field, field.Type, field.GetAttributes());
                    if (marshallingInfo is NoMarshallingInfo)
                    {
                        isBlittable &= field.Type.IsUnattributedReferencedTypeBlittable();
                    }
                    else if (marshallingInfo is NativeMarshallingAttributeInfo)
                    {
                        isBlittable = false;
                    }
                    else if (marshallingInfo is MarshalAsInfo(UnmanagedType unmanagedType, _)
                        && field.Type is { SpecialType: not SpecialType.None } specialType
                        && specialType.SpecialType.IsSpecialTypeBlittable())
                    {
                        isBlittable &= specialType.SpecialType.SpecialTypeWithMarshalAsIsBlittable(unmanagedType);
                    }
                    else if (field.Type.TypeKind == TypeKind.FunctionPointer && marshallingInfo is MarshalAsInfo(not UnmanagedType.FunctionPtr, _))
                    {
                        isBlittable = false;
                    }
                }


                if (!isBlittable)
                {
                    break;
                }
            }

            return new GeneratedStructMarshallingFeatures(CustomMarshallingFeatures.ManagedToNative | CustomMarshallingFeatures.NativeToManaged, HasValueProperty: false, MarshallerMustBeRefStruct: false, IsBlittable: isBlittable);
        }


        private static GeneratedStructMarshallingFeatures GenerateMarshallingInfoForGeneratedMarshallingType(
            Compilation compilation,
            IDictionary<INamedTypeSymbol, GeneratedStructMarshallingFeatures> cache,
            INamedTypeSymbol type)
        {
            // TODO: If we decide to support StructLayoutAttribute, handle CharSet here.
            // We use an empty cache here for the generated struct field case as those cases should always be previously by the topological sort.
            MarshallingAttributeInfoParser parser = new MarshallingAttributeInfoParser(compilation, new NullDiagnosticsSink(), new StructMarshallingFeatureCache(), new DefaultMarshallingInfo(CharEncoding.Undefined), type);
            CustomMarshallingFeatures marshallingFeatures = CustomMarshallingFeatures.ManagedToNative | CustomMarshallingFeatures.NativeToManaged;
            bool needsFreeNative = false;
            bool needsValueProperty = false;
            bool marshallerMustBeRefStruct = false;
            bool isBlittable = true;
            foreach (ISymbol member in type.GetMembers())
            {
                if (member is not IFieldSymbol { IsStatic: false, IsConst: false } field)
                {
                    continue;
                }
                if (field.Type is INamedTypeSymbol namedType && cache.TryGetValue(namedType, out GeneratedStructMarshallingFeatures generatedFeatures))
                {
                    marshallingFeatures &= generatedFeatures.MarshallingFeatures;
                    needsValueProperty |= generatedFeatures.HasValueProperty;
                    marshallerMustBeRefStruct |= generatedFeatures.MarshallerMustBeRefStruct;
                    isBlittable &= generatedFeatures.IsBlittable;
                    if (generatedFeatures.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.FreeNativeResources))
                    {
                        needsFreeNative = true;
                    }
                }
                else
                {
                    MarshallingInfo marshallingInfo = parser.ParseMarshallingInfo(field, field.Type, field.GetAttributes());
                    if (marshallingInfo is NativeMarshallingAttributeInfo nativeMarshallingInfo)
                    {
                        marshallerMustBeRefStruct |= nativeMarshallingInfo.NativeMarshallingType is RefLikeTypeInfo;
                        needsValueProperty |= nativeMarshallingInfo.ValuePropertyType is not null;
                        marshallingFeatures &= nativeMarshallingInfo.MarshallingFeatures;
                        isBlittable = false;
                        if (nativeMarshallingInfo.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.FreeNativeResources))
                        {
                            needsFreeNative = true;
                        }
                    }
                    else if (marshallingInfo is SafeHandleMarshallingInfo)
                    {
                        // SafeHandles do some checks in the FreeNative method as that runs in the finalizer.
                        needsFreeNative = true;
                        isBlittable = false;
                    }
                    else if (marshallingInfo is MarshalAsInfo(UnmanagedType.LPWStr or UnmanagedType.LPStr or UnmanagedType.LPTStr or (UnmanagedType)0x30, _) // 0x30 is LPUTF8Str
                            or MarshallingInfoStringSupport
                            && field.Type.SpecialType == SpecialType.System_String)
                    {
                        // In this case, we will successfully marshal strings
                        // TODO: If we ever move string marshalling to the "custom native marshalling" model, we can remove this case.
                        // All other cases where FreeNative is needed (i.e. arrays) are handled by the custom native marshalling system.
                        needsFreeNative = true;
                        isBlittable = false;
                    }
                }
            }

            var features = new GeneratedStructMarshallingFeatures(
                marshallingFeatures | (needsFreeNative ? CustomMarshallingFeatures.FreeNativeResources : CustomMarshallingFeatures.None),
                needsValueProperty,
                marshallerMustBeRefStruct,
                isBlittable);
            return features;
        }

        public bool TryGetGeneratedStructMarshallingFeatures(INamedTypeSymbol type, out GeneratedStructMarshallingFeatures structMarshallingFeatures)
        {
            return _cache.TryGetValue(type, out structMarshallingFeatures);
        }

        public bool Equals(StructMarshallingFeatureCache other)
        {
            return _cache.SequenceEqual(other._cache);
        }

        public override int GetHashCode() => throw new InvalidOperationException();

        public override bool Equals(object obj) => obj is StructMarshallingFeatureCache other && Equals(other);

        private class NullDiagnosticsSink : IGeneratorDiagnostics
        {
            public void ReportConfigurationNotSupported(AttributeData attributeData, string configurationName, string? unsupportedValue) { }
            public void ReportInvalidMarshallingAttributeInfo(AttributeData attributeData, string reasonResourceName, params string[] reasonArgs) { }
        }
    }

    public static class GeneratedStructMarshallingFeatureCacheHelpers
    {
        public static IncrementalValueProvider<StructMarshallingFeatureCache> CreateStructMarshallingFeatureCacheProvider(this IncrementalGeneratorInitializationContext context)
            => context.CompilationProvider.Select((compilation, ct) => compilation.CreateStructMarshallingFeatureCache());

        public static StructMarshallingFeatureCache CreateStructMarshallingFeatureCache(this Compilation compilation)
        {
            AllStructTypesVisitor visitor = new();
            visitor.Visit(compilation.Assembly);
            try
            {
                return new StructMarshallingFeatureCache(visitor.StructTypes.ToImmutable(), compilation);
            }
            catch (InvalidOperationException) // Cyclic/recursive struct declarations will result in an InvalidOperationException during cache creation.
            {
                // We're already going to be in a bad state and C# compiation of the user code will fail anyway, so just return an empty cache.
                return new StructMarshallingFeatureCache();
            }
        }

        private sealed class AllStructTypesVisitor : SymbolVisitor
        {
#pragma warning disable RS1024 // Compare symbols correctly. We are comparing them correctly.
            private readonly HashSet<INamedTypeSymbol> _seenTypes = new(SymbolEqualityComparer.Default);
#pragma warning restore RS1024 // Compare symbols correctly

            public ImmutableArray<INamedTypeSymbol>.Builder StructTypes { get; } = ImmutableArray.CreateBuilder<INamedTypeSymbol>();

            public override void VisitAssembly(IAssemblySymbol symbol)
            {
                Visit(symbol.GlobalNamespace);
            }

            public override void VisitNamespace(INamespaceSymbol symbol)
            {
                foreach (var member in symbol.GetMembers())
                {
                    Visit(member);
                }
            }

            public override void VisitNamedType(INamedTypeSymbol symbol)
            {
                // If we haven't already visited this type, record it and visit its members.
                if (_seenTypes.Add(symbol.OriginalDefinition))
                {
                    if (symbol.IsValueType)
                    {
                        StructTypes.Add(symbol);
                    }
                    foreach (ISymbol nested in symbol.GetMembers())
                    {
                        Visit(nested);
                    }
                }
            }

            public override void VisitField(IFieldSymbol symbol)
            {
                // Only visit generics as the only interesting non-generic types are covered in VisitNamedType
                if (symbol is { IsStatic: false, Type: INamedTypeSymbol { IsValueType: true, IsGenericType: true }})
                {
                    Visit(symbol.Type);
                }
            }

            public override void VisitMethod(IMethodSymbol symbol)
            {
                // Only visit generics as the only interesting non-generic types are covered in VisitNamedType
                if (symbol.ReturnType is INamedTypeSymbol { IsValueType: true, IsGenericType: true })
                {
                    Visit(symbol.ReturnType);
                }
                foreach (IParameterSymbol param in symbol.Parameters)
                {
                    if (param.Type is INamedTypeSymbol { IsValueType: true, IsGenericType: true })
                    {
                        Visit(param.Type);
                    }
                }
            }
        }
    }
}
