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
    public class CompileFails
    {
        public static IEnumerable<object[]> CodeSnippetsToCompile()
        {
            yield break;
        }

        private static object[] CreateTestCase(string source, int expectedGeneratorErrors, int expectedCompilerErrors, [CallerArgumentExpression("source")] string snippetName = "")
        {
            return new object[] { snippetName, source, expectedGeneratorErrors, expectedCompilerErrors };
        }

#pragma warning disable xUnit1004 // Test methods should not be skipped. There are no current scenarios to test here.
        [Theory(Skip = "No current scenarios to test.")]
#pragma warning restore xUnit1004 // Test methods should not be skipped
        [MemberData(nameof(CodeSnippetsToCompile))]
#pragma warning disable xUnit1026 // Theory methods should use all of their parameters. The _ parameter is used to get a better IDE test discovery/selection experience
        public async Task ValidateSnippets(string _, string source, int expectedGeneratorErrors, int expectedCompilerErrors)
#pragma warning restore xUnit1026 // Theory methods should use all of their parameters
        {
            Compilation comp = await TestUtils.CreateCompilation(source);
            TestUtils.AssertPreSourceGeneratorCompilation(comp);

            var newComp = TestUtils.RunGenerators(comp, out var generatorDiags, new Microsoft.Interop.StructMarshallingGenerator());

            // Verify the compilation failed with errors.
            int generatorErrors = generatorDiags.Count(d => d.Severity == DiagnosticSeverity.Error);
            Assert.Equal(expectedGeneratorErrors, generatorErrors);

            int compilerErrors = newComp.GetDiagnostics().Count(d => d.Severity == DiagnosticSeverity.Error);
            Assert.Equal(expectedCompilerErrors, compilerErrors);
        }
    }
}
