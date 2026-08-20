// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.IO;
using System.Reflection.Metadata;
using System.Threading;

namespace ILAssembler;

internal static class OutputFile
{
    private const int ErrorLockViolation = 33;
    private const int ErrorSharingViolation = 32;
    private const int MaxOpenAttempts = 10;
    private const int RetryDelayMilliseconds = 100;

    internal static void Write(string path, BlobBuilder content)
    {
        using FileStream outputStream = Open(path);
        content.WriteContentTo(outputStream);
    }

    private static FileStream Open(string path)
    {
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                return File.Create(path);
            }
            catch (IOException exception) when (attempt < MaxOpenAttempts && IsSharingViolation(exception))
            {
                // A just-exited process can leave an executable image temporarily unavailable on Windows.
                Thread.Sleep(RetryDelayMilliseconds);
            }
        }
    }

    private static bool IsSharingViolation(IOException exception)
    {
        if (!OperatingSystem.IsWindows())
        {
            return false;
        }

        int errorCode = exception.HResult & 0xFFFF;
        return errorCode is ErrorSharingViolation or ErrorLockViolation;
    }
}
