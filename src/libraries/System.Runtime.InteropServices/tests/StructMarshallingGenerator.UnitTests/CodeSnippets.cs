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
        public static readonly string BlittableField = @"
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
        public static readonly string NonBlittableMarshalAsStringField = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct NonBlittable
{
    [MarshalAs(UnmanagedType.LPWStr)]
    public string s;
}
";

        public static readonly string BlittableFixedBufferField = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct NonBlittable
{
    public unsafe fixed int Buffer[4];
}
";

        public static readonly string NonBlittableFixedBufferField = @"
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

        public static readonly string SimpleGeneratedStructField = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Outer
{
    public Inner b;
}

[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Inner
{
    public bool b;
}
";

        public static readonly string GeneratedStructWithFreeNativeField = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Outer
{
    public Inner i;
}

[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Inner
{
    [MarshalAs(UnmanagedType.LPWStr)]
    public string s;
}
";

        public static readonly string CustomStructMarshallingField = @"
using System.Runtime.InteropServices;

[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Generate
{
    public S s;
}

[NativeMarshalling(typeof(Native))]
struct S
{
    public bool b;
}

struct Native
{
    private int i;
    public Native(S s)
    {
        i = s.b ? 1 : 0;
    }

    public S ToManaged() => new S { b = i != 0 };
}";

        public static readonly string CustomStructMarshallingManagedToNativeOnlyField = @"
using System.Runtime.InteropServices;

[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Generate
{
    public S s;
}

[NativeMarshalling(typeof(Native))]
[StructLayout(LayoutKind.Sequential)]
struct S
{
    public bool b;
}

struct Native
{
    private int i;
    public Native(S s)
    {
        i = s.b ? 1 : 0;
    }
}";

        public static readonly string CustomStructMarshallingNativeToManagedOnlyField = @"
using System.Runtime.InteropServices;

[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Generate
{
    public S s;
}

[NativeMarshalling(typeof(Native))]
[StructLayout(LayoutKind.Sequential)]
struct S
{
    public bool b;
}

[StructLayout(LayoutKind.Sequential)]
struct Native
{
    public int i;

    public S ToManaged() => new S { b = i != 0 };
}";

        public static readonly string CustomStructMarshallingWithValueProperty = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct Outer
{
    public S b;
}

[NativeMarshalling(typeof(Native))]
struct S
{
    public bool b;
}

struct Native
{
    public Native(S s)
    {
        Value = s.b ? 1 : 0;
    }

    public S ToManaged() => new S { b = Value != 0 };

    public int Value { get; set; }
}
";

        public static readonly string BlittableConstSizeArrayField = @"
using System.Runtime.InteropServices;
[GeneratedMarshalling]
[StructLayout(LayoutKind.Sequential)]
partial struct BlittableConstSizeArray
{
    [MarshalUsing(ConstantElementCount = 10)]
    public int[] i;
}
";
    }
}
