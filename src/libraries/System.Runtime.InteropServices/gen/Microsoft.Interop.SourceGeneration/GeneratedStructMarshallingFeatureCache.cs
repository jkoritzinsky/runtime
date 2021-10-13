// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Microsoft.Interop
{
    public readonly record struct GeneratedStructMarshallingFeatures(CustomMarshallingFeatures MarshallingFeatures, bool HasValueProperty);

    public sealed class GeneratedStructMarshallingFeatureCache : IEquatable<GeneratedStructMarshallingFeatureCache>
    {
        private readonly ImmutableDictionary<INamedTypeSymbol, GeneratedStructMarshallingFeatures> _cache;

        internal GeneratedStructMarshallingFeatureCache()
        {
            _cache = ImmutableDictionary<INamedTypeSymbol, GeneratedStructMarshallingFeatures>.Empty;
        }

        internal GeneratedStructMarshallingFeatureCache(ImmutableArray<INamedTypeSymbol> generatedStructTypes, Compilation compilation)
        {
            var cacheBuilder = ImmutableDictionary.CreateBuilder<INamedTypeSymbol, GeneratedStructMarshallingFeatures>(SymbolEqualityComparer.Default);

            var typeToIndexMap = generatedStructTypes.Select((s, i) => (s, i)).ToImmutableDictionary(pair => pair.s, pair => pair.i, SymbolEqualityComparer.Default);

            ImmutableArray<INamedTypeSymbol> sortedTypes = ImmutableArray.CreateRange(MarshallerHelpers.GetTopologicallySortedElements(
                generatedStructTypes,
                generatedStructType => typeToIndexMap[generatedStructType],
                GetStructTypeDependencies));

            foreach (INamedTypeSymbol type in sortedTypes)
            {
                // TODO: If we decide to support StructLayoutAttribute, handle CharSet here.
                // We use an empty cache here for the generated struct field case as those cases should always be previously by the topological sort.
                MarshallingAttributeInfoParser parser = new MarshallingAttributeInfoParser(compilation, new NullDiagnosticsSink(), new GeneratedStructMarshallingFeatureCache(), new DefaultMarshallingInfo(CharEncoding.Undefined), type);
                CustomMarshallingFeatures marshallingFeatures = CustomMarshallingFeatures.ManagedToNative | CustomMarshallingFeatures.NativeToManaged;
                bool needsFreeNative = false;
                bool needsValueProperty = false;
                foreach (ISymbol member in type.GetMembers())
                {
                    if (member is not IFieldSymbol { IsStatic: false, IsConst: false } field)
                    {
                        continue;
                    }
                    if (field.Type is INamedTypeSymbol namedType && cacheBuilder.TryGetValue(namedType, out GeneratedStructMarshallingFeatures generatedFeatures))
                    {
                        marshallingFeatures &= generatedFeatures.MarshallingFeatures;
                        needsValueProperty |= generatedFeatures.HasValueProperty;
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
                            needsValueProperty |= nativeMarshallingInfo.ValuePropertyType is not null;
                            marshallingFeatures &= nativeMarshallingInfo.MarshallingFeatures;
                            if (nativeMarshallingInfo.MarshallingFeatures.HasFlag(CustomMarshallingFeatures.FreeNativeResources))
                            {
                                needsFreeNative = true;
                            }
                        }
                        else if (marshallingInfo is SafeHandleMarshallingInfo)
                        {
                            // SafeHandles do some checks in the FreeNative method as that runs in the finalizer.
                            needsFreeNative = true;
                        }
                        else if (marshallingInfo is MarshalAsInfo(UnmanagedType.LPWStr or UnmanagedType.LPStr or UnmanagedType.LPTStr or (UnmanagedType)0x30, _) // 0x30 is LPUTF8Str
                                or MarshallingInfoStringSupport
                                && field.Type.SpecialType == SpecialType.System_String)
                        {
                            // In this case, we will successfully marshal strings
                            // TODO: If we ever move string marshalling to the "custom native marshalling" model, we can remove this case.
                            // All other cases where FreeNative is needed (i.e. arrays) are handled by the custom native marshalling system.
                            needsFreeNative = true;
                        }
                    }
                }

                cacheBuilder.Add(type, new GeneratedStructMarshallingFeatures(marshallingFeatures | (needsFreeNative ? CustomMarshallingFeatures.FreeNativeResources : CustomMarshallingFeatures.None), needsValueProperty));
            }

            _cache = cacheBuilder.ToImmutable();

            IEnumerable<int> GetStructTypeDependencies(INamedTypeSymbol generatedStructType)
            {
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

        public bool TryGetGeneratedStructMarshallingFeatures(INamedTypeSymbol type, out GeneratedStructMarshallingFeatures structMarshallingFeatures)
        {
            return _cache.TryGetValue(type, out structMarshallingFeatures);
        }

        public bool Equals(GeneratedStructMarshallingFeatureCache other)
        {
            return _cache.SequenceEqual(other._cache);
        }

        public override int GetHashCode() => throw new InvalidOperationException();

        public override bool Equals(object obj) => obj is GeneratedStructMarshallingFeatureCache other && Equals(other);

        private class NullDiagnosticsSink : IGeneratorDiagnostics
        {
            public void ReportConfigurationNotSupported(AttributeData attributeData, string configurationName, string? unsupportedValue) { }
            public void ReportInvalidMarshallingAttributeInfo(AttributeData attributeData, string reasonResourceName, params string[] reasonArgs) { }
        }
    }

    public static class GeneratedStructMarshallingFeatureCacheHelpers
    {
        public static IncrementalValueProvider<GeneratedStructMarshallingFeatureCache> CreateGeneratedStructMarshallingFeatureCacheProvider(this IncrementalGeneratorInitializationContext context)
        {
            return context.SyntaxProvider.CreateSyntaxProvider(ShouldVisitNode, (context, ct) => (INamedTypeSymbol)context.SemanticModel.GetDeclaredSymbol(context.Node, ct))
                    .Where(symbol => HasTriggerAttribute(symbol))
                    .Collect()
                    .Combine(context.CompilationProvider)
                    .Select((data, ct) =>
                    {
                        try
                        {
                            return new GeneratedStructMarshallingFeatureCache(data.Left, data.Right);
                        }
                        catch (InvalidOperationException) // Cyclic/recursive struct declarations will result in an InvalidOperationException during cache creation.
                        {
                            // We're already going to be in a bad state and C# compiation of the user code will fail anyway, so just return an empty cache.
                            return new GeneratedStructMarshallingFeatureCache();
                        }
                    });

            static bool HasTriggerAttribute(INamedTypeSymbol symbol)
            => symbol.GetAttributes().Any(static attribute => attribute.AttributeClass?.ToDisplayString() == TypeNames.GeneratedMarshallingAttribute);

            static bool ShouldVisitNode(SyntaxNode syntaxNode, CancellationToken cancellationToken)
            {
                // We only support C# declarations.
                if (syntaxNode.Language != LanguageNames.CSharp
                    || !(syntaxNode.IsKind(SyntaxKind.StructDeclaration) || syntaxNode.IsKind(SyntaxKind.RecordStructDeclaration)))
                {
                    return false;
                }

                var declSyntax = (StructDeclarationSyntax)syntaxNode;

                // Verify the type is not generic and is partial
                if (declSyntax.TypeParameterList is not null
                    || !declSyntax.Modifiers.Any(SyntaxKind.PartialKeyword))
                {
                    return false;
                }

                // Verify that the types the type is declared in are marked partial.
                for (SyntaxNode? parentNode = declSyntax.Parent; parentNode is TypeDeclarationSyntax typeDecl; parentNode = parentNode.Parent)
                {
                    if (!typeDecl.Modifiers.Any(SyntaxKind.PartialKeyword))
                    {
                        return false;
                    }
                }

                // Filter out types with no attributes early.
                if (declSyntax.AttributeLists.Count == 0)
                {
                    return false;
                }

                return true;
            }
        }
    }
}
