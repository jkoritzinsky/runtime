// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using System;
using System.Collections.Generic;
using System.Text;

namespace Microsoft.Interop
{
    /// <summary>
    /// A discriminated union that contains enough info about a managed type to determine a marshalling generator and generate code.
    /// </summary>
    public abstract record ManagedTypeInfo(string FullTypeName, string DiagnosticFormattedName)
    {
        public TypeSyntax Syntax { get; } = SyntaxFactory.ParseTypeName(FullTypeName);

        public static ManagedTypeInfo CreateTypeInfoForTypeSymbol(ITypeSymbol type)
        {
            string typeName = type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
            string diagnosticFormattedName = type.ToDisplayString();
            if (type.SpecialType != SpecialType.None)
            {
                return new SpecialTypeInfo(typeName, diagnosticFormattedName, type.SpecialType);
            }
            if (type.TypeKind == TypeKind.Enum)
            {
                return new EnumTypeInfo(typeName, diagnosticFormattedName, ((INamedTypeSymbol)type).EnumUnderlyingType!.SpecialType);
            }
            if (type.TypeKind == TypeKind.Pointer)
            {
                return new PointerTypeInfo(typeName, diagnosticFormattedName, IsFunctionPointer: false);
            }
            if (type.TypeKind == TypeKind.FunctionPointer)
            {
                return new PointerTypeInfo(typeName, diagnosticFormattedName, IsFunctionPointer: true);
            }
            if (type.TypeKind == TypeKind.Array && type is IArrayTypeSymbol { IsSZArray: true } arraySymbol)
            {
                return new SzArrayType(CreateTypeInfoForTypeSymbol(arraySymbol.ElementType));
            }
            if (type.TypeKind == TypeKind.Delegate)
            {
                return new DelegateTypeInfo(typeName, diagnosticFormattedName);
            }
            if (type.IsRefLikeType)
            {
                return new RefLikeTypeInfo(typeName, diagnosticFormattedName);
            }
            return new SimpleManagedTypeInfo(typeName, diagnosticFormattedName);
        }
    }

    public sealed record SpecialTypeInfo(string FullTypeName, string DiagnosticFormattedName, SpecialType SpecialType) : ManagedTypeInfo(FullTypeName, DiagnosticFormattedName)
    {
        public static readonly SpecialTypeInfo Int32 = new("int", "int", SpecialType.System_Int32);
        public static readonly SpecialTypeInfo Void = new("void", "void", SpecialType.System_Void);

        public bool Equals(SpecialTypeInfo? other)
        {
            return other is not null && SpecialType == other.SpecialType;
        }

        public override int GetHashCode()
        {
            return (int)SpecialType;
        }
    }

    public sealed record EnumTypeInfo(string FullTypeName, string DiagnosticFormattedName, SpecialType UnderlyingType) : ManagedTypeInfo(FullTypeName, DiagnosticFormattedName);

    public sealed record PointerTypeInfo(string FullTypeName, string DiagnosticFormattedName, bool IsFunctionPointer) : ManagedTypeInfo(FullTypeName, DiagnosticFormattedName);

    public sealed record SzArrayType(ManagedTypeInfo ElementTypeInfo) : ManagedTypeInfo($"{ElementTypeInfo.FullTypeName}[]", $"{ElementTypeInfo.DiagnosticFormattedName}[]");

    public sealed record DelegateTypeInfo(string FullTypeName, string DiagnosticFormattedName) : ManagedTypeInfo(FullTypeName, DiagnosticFormattedName);

    public sealed record SimpleManagedTypeInfo(string FullTypeName, string DiagnosticFormattedName) : ManagedTypeInfo(FullTypeName, DiagnosticFormattedName);

    public sealed record RefLikeTypeInfo(string FullTypeName, string DiagnosticFormattedName) : ManagedTypeInfo(FullTypeName, DiagnosticFormattedName);

}
