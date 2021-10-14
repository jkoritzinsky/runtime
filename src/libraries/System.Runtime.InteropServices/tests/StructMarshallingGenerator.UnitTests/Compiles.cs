// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using Microsoft.CodeAnalysis;
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Linq;
using System.Threading.Tasks;
using Xunit;

namespace StructMarshallingGenerator.UnitTests
{
    public class Compiles
    {
        public static IEnumerable<object[]> CodeSnippetsToCompile()
        {
            yield return CreateTestCase(CodeSnippets.TrivialStructDeclaration);
            yield return CreateTestCase(CodeSnippets.BlittableField);
            yield return CreateTestCase(CodeSnippets.NonBlittableMarshalAsStringField);
            yield return CreateTestCase(CodeSnippets.BlittableFixedBufferField);
            yield return CreateTestCase(CodeSnippets.SimpleGeneratedStructField);
            yield return CreateTestCase(CodeSnippets.GeneratedStructWithFreeNativeField);
            yield return CreateTestCase(CodeSnippets.CustomStructMarshallingWithValueProperty);
            yield return CreateTestCase(CodeSnippets.CustomStructMarshallingField);
            yield return CreateTestCase(CodeSnippets.CustomStructMarshallingManagedToNativeOnlyField);
            yield return CreateTestCase(CodeSnippets.CustomStructMarshallingNativeToManagedOnlyField);
            yield return CreateTestCase(CodeSnippets.BlittableConstSizeArrayField);
            yield return CreateTestCase(CodeSnippets.BlittableElementSizeArrayField);
            yield return CreateTestCase(CodeSnippets.NonBlittableConstSizeArrayField);
            yield return CreateTestCase(CodeSnippets.NonBlittableElementSizeArrayField);
            yield return CreateTestCase(CodeSnippets.ArrayOfArrayField);
        }

        private static object[] CreateTestCase(string source, [CallerArgumentExpression("source")] string snippetName = "")
        {
            return new object[] { snippetName, source };
        }

        [Theory]
        [MemberData(nameof(CodeSnippetsToCompile))]
#pragma warning disable xUnit1026 // Theory methods should use all of their parameters. The _ parameter is used to get a better IDE test discovery/selection experience
        public async Task ValidateSnippets(string _, string source)
#pragma warning restore xUnit1026 // Theory methods should use all of their parameters
        {
            Compilation comp = await TestUtils.CreateCompilation(source);
            TestUtils.AssertPreSourceGeneratorCompilation(comp);

            var newComp = TestUtils.RunGenerators(comp, out var generatorDiags, new Microsoft.Interop.StructMarshallingGenerator());
            Assert.Empty(generatorDiags);

            var newCompDiags = newComp.GetDiagnostics();
            Assert.Empty(newCompDiags);
        }
    }
}
