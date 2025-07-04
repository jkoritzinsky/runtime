// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

using Mono.Cecil;

namespace Mono.Linker.Tests.TestCasesRunner
{
    public partial class AssemblyChecker
    {
        static bool IsCompilerGeneratedMemberName(string memberName)
        {
            return memberName.Length > 0 && memberName[0] == '<';
        }

        static bool IsCompilerGeneratedMember(IMemberDefinition member)
        {
            // Top-level methods are generated with names like
            // <<Main>$>g__MethodName|0_1(). While the names are generated by
            // the compiler, don't consider the method to be compiler-generated
            // for the purpose of Kept validation, because they are attributable
            // in source like any other method.
            if (member is MethodDefinition method && method.Name.Contains("<Main>$"))
                return false;

            if (IsCompilerGeneratedMemberName(member.Name))
                return true;

            if (member.DeclaringType != null)
                return IsCompilerGeneratedMember(member.DeclaringType);

            return false;
        }

        static bool IsDelegateBackingFieldsType(TypeDefinition type) => type.Name == "<>O";

        static bool IsPrivateImplementationDetailsType(TypeDefinition type) =>
            string.IsNullOrEmpty(type.Namespace) && type.Name.StartsWith("<PrivateImplementationDetails>");
    }
}
