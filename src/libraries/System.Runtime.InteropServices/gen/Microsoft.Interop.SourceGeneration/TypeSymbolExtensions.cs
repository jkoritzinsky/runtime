// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Immutable;
using System.Linq;
using System.Runtime.InteropServices;

using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Microsoft.Interop
{
    public static class TypeSymbolExtensions
    {
        public static bool IsTypeConsideredBlittable(this ITypeSymbol type, StructMarshallingFeatureCache structFeatureCache, bool typeDefinedInSource)
        {
            if (typeDefinedInSource
                && type is INamedTypeSymbol namedTypeInSource)
            {
                return structFeatureCache.TryGetGeneratedStructMarshallingFeatures(namedTypeInSource, out GeneratedStructMarshallingFeatures marshallingFeatures)
                    && marshallingFeatures.IsBlittable;
            }
            else if (type.GetAttributes().Any(attr => attr.AttributeClass.ToDisplayString() == TypeNames.BlittableTypeAttribute))
            {
                return true;
            }
            return type.IsUnattributedReferencedTypeBlittable();
        }

        public static bool IsUnattributedReferencedTypeBlittable(this ITypeSymbol type)
        {
            return type switch
            {
                { IsReferenceType: true } => false,
                { TypeKind: TypeKind.Pointer or TypeKind.FunctionPointer } => true,
                { SpecialType: not SpecialType.None } => type.SpecialType.IsSpecialTypeBlittable(),
                // Assume that type parameters that can be blittable are blittable.
                // We'll re-evaluate blittability for generic fields of generic types at instantation time.
                { TypeKind: TypeKind.TypeParameter } => true,
                { IsValueType: false } => false,
                INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: { SpecialType: SpecialType enumUnderlyingType } } => enumUnderlyingType.IsSpecialTypeBlittable(),
                _ => false
            };
        }

        public static bool IsSpecialTypeBlittable(this SpecialType specialType)
            => specialType switch
            {
                SpecialType.System_Void
                or SpecialType.System_SByte
                or SpecialType.System_Byte
                or SpecialType.System_Int16
                or SpecialType.System_UInt16
                or SpecialType.System_Int32
                or SpecialType.System_UInt32
                or SpecialType.System_Int64
                or SpecialType.System_UInt64
                or SpecialType.System_Single
                or SpecialType.System_Double
                or SpecialType.System_IntPtr
                or SpecialType.System_UIntPtr => true,
                _ => false
            };

        public static bool IsSpecialTypePrimitive(this SpecialType specialType)
            => specialType switch
            {
                SpecialType.System_Void
                or SpecialType.System_Boolean
                or SpecialType.System_Char
                or SpecialType.System_SByte
                or SpecialType.System_Byte
                or SpecialType.System_Int16
                or SpecialType.System_UInt16
                or SpecialType.System_Int32
                or SpecialType.System_UInt32
                or SpecialType.System_Int64
                or SpecialType.System_UInt64
                or SpecialType.System_Single
                or SpecialType.System_Double
                or SpecialType.System_IntPtr
                or SpecialType.System_UIntPtr => true,
                _ => false
            };

        public static bool SpecialTypeWithMarshalAsIsBlittable(this SpecialType specialType, UnmanagedType unmanagedType)
            => (specialType, unmanagedType) is (SpecialType.System_SByte, UnmanagedType.I1)
                or (SpecialType.System_Byte, UnmanagedType.U1)
                or (SpecialType.System_Int16, UnmanagedType.I2)
                or (SpecialType.System_UInt16, UnmanagedType.U2)
                or (SpecialType.System_Int32, UnmanagedType.I4)
                or (SpecialType.System_UInt32, UnmanagedType.U4)
                or (SpecialType.System_Int64, UnmanagedType.I8)
                or (SpecialType.System_UInt64, UnmanagedType.U8)
                or (SpecialType.System_IntPtr, UnmanagedType.SysInt)
                or (SpecialType.System_UIntPtr, UnmanagedType.SysUInt)
                or (SpecialType.System_Single, UnmanagedType.R4)
                or (SpecialType.System_Double, UnmanagedType.R8);

        public static bool IsAutoLayout(this INamedTypeSymbol type, ITypeSymbol structLayoutAttributeType)
        {
            foreach (AttributeData attr in type.GetAttributes())
            {
                if (SymbolEqualityComparer.Default.Equals(structLayoutAttributeType, attr.AttributeClass))
                {
                    return (LayoutKind)(int)attr.ConstructorArguments[0].Value! == LayoutKind.Auto;
                }
            }
            return type.IsReferenceType;
        }

        public static TypeSyntax AsTypeSyntax(this ITypeSymbol type)
        {
            return SyntaxFactory.ParseTypeName(type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat));
        }

        public static bool IsIntegralType(this SpecialType type)
        {
            return type switch
            {
                SpecialType.System_SByte
                or SpecialType.System_Byte
                or SpecialType.System_Int16
                or SpecialType.System_UInt16
                or SpecialType.System_Int32
                or SpecialType.System_UInt32
                or SpecialType.System_Int64
                or SpecialType.System_UInt64
                or SpecialType.System_IntPtr
                or SpecialType.System_UIntPtr => true,
                _ => false
            };
        }

        public static bool IsExposedOutsideOfCurrentCompilation(this INamedTypeSymbol type)
        {
            for (; type is not null; type = type.ContainingType)
            {
                Accessibility accessibility = type.DeclaredAccessibility;

                if (accessibility is Accessibility.Internal or Accessibility.ProtectedAndInternal or Accessibility.Private or Accessibility.Friend or Accessibility.ProtectedAndFriend)
                {
                    return false;
                }
            }
            return true;
        }
    }
}
