// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

public static class Program
{
    [DllImport("__Internal")]
    public static extern void mono_ios_set_summary(string value);

    public static async Task<int> Main(string[] args)
    {
        mono_ios_set_summary($"Starting functional test");
        int result = RunInterpreter();
        Console.WriteLine("Done!");
        await Task.Delay(5000);

        return result;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public unsafe static int RunInterpreter()
    {
        return 42;
    }
}
