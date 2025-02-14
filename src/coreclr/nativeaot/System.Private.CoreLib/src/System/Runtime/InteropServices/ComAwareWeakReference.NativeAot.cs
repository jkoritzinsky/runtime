// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Runtime.CompilerServices;

namespace System
{
    internal sealed partial class ComAwareWeakReference
    {
        internal static unsafe object? ComWeakRefToObject(IntPtr pComWeakRef, long wrapperId)
        {
            return ComWeakRefToComWrappersObject(pComWeakRef, wrapperId);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal static unsafe bool PossiblyComObject(object target)
        {
            return PossiblyComWrappersObject(target);
        }

        internal static unsafe IntPtr ObjectToComWeakRef(object target, out long wrapperId)
        {
            return ComWrappersObjectToComWeakRef(target, out wrapperId);
        }
    }
}
