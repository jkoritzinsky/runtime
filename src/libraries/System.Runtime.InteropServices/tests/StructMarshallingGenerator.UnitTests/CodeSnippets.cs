// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Runtime.InteropServices;

namespace StructMarshallingGenerator.UnitTests
{
    internal static class CodeSnippets
    {
        /// <summary>
        /// Trivial usage of GeneratedMarshalling
        /// </summary>
        public static readonly string TrivialStructDeclaration = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
partial struct Empty
{
}
";
        /// <summary>
        /// Usage of GeneratedMarshalling on a blittable struct
        /// </summary>
        public static readonly string BlittableStructDeclarations = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Blittable
{
    public int i;
}
";

        /// <summary>
        /// Usage of GeneratedMarshalling on a non-blittable struct
        /// </summary>
        public static readonly string NonBlittableStructDeclaration = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct NonBlittable
{
    [MarshalAs(UnmanagedType.LPWStr)]
    public string s;
}
";

        public static readonly string BlittableFixedBufferDeclaration = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct NonBlittable
{
    public unsafe fixed int Buffer[4];
}
";

        public static readonly string NonBlittableFixedBufferDeclaration = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct NonBlittable
{
    [MarshalUsing(typeof(WrappedCBool), ElementIndirectionLevel = 1)]
    public unsafe fixed bool Buffer[4];
}

struct WrappedCBool
{
    private byte value;

    public WrappedCBool(bool b) { value = b ? (byte)1 : (byte)0; }

    public bool ToManaged() => value != 0;
};
";
    }
}
