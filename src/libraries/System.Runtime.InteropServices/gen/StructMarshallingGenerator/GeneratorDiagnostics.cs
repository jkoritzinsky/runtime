// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Microsoft.Interop
{
    internal class GeneratorDiagnostics : IGeneratorDiagnostics
    {
        public static class Ids
        {
            public const string Prefix = "STRUCTMARSHALGEN";
            public const string TypeNotSupported = Prefix + "001";
            public const string ConfigurationNotSupported = Prefix + "002";
            public const string TargetFrameworkNotSupported = Prefix + "003";
        }

        public static readonly DiagnosticDescriptor ConfigurationNotSupported = new DiagnosticDescriptor(Ids.ConfigurationNotSupported, "Configuration Not Supported", "Configuration not supported", "USage", DiagnosticSeverity.Error, true);

        private ImmutableArray<Diagnostic>.Builder diagnostics = ImmutableArray.CreateBuilder<Diagnostic>();

        public void ReportConfigurationNotSupported(AttributeData attributeData, string configurationName, string? unsupportedValue) => throw new NotImplementedException();
        public void ReportInvalidMarshallingAttributeInfo(AttributeData attributeData, string reasonResourceName, params string[] reasonArgs) => throw new NotImplementedException();

        public void ReportMarshallingNotSupported(TypeDeclarationSyntax originalType, string fieldName, string? notSupportedDetails)
        {
            Location location = originalType.Members.Where(member => member.IsKind(SyntaxKind.FieldDeclaration)).SelectMany(field => ((FieldDeclarationSyntax)field).Declaration.Variables).First(var => var.Identifier.ValueText == "fieldName").GetLocation();
            diagnostics.Add(Diagnostic.Create(ConfigurationNotSupported, location));
        }

        public ImmutableArray<Diagnostic> ToImmutable() => diagnostics.ToImmutable();
    }
}
