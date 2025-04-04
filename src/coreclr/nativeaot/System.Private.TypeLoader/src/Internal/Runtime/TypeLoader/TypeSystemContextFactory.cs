// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.


using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

using Internal.TypeSystem;

namespace Internal.Runtime.TypeLoader
{
    public static class TypeSystemContextFactory
    {
        // Cache the most recent instance of TypeSystemContext in a weak handle, and reuse it if possible
        // This allows us to avoid recreating the type resolution context again and again, but still allows it to go away once the types are no longer being built
        private static WeakGCHandle<TypeSystemContext?> s_cachedContext = new WeakGCHandle<TypeSystemContext?>(null);

        private static readonly Lock s_lock = new Lock(useTrivialWaits: true);

        public static TypeSystemContext Create()
        {
            using (s_lock.EnterScope())
            {
                if (s_cachedContext.TryGetTarget(out TypeSystemContext? context))
                {
                    s_cachedContext.SetTarget(null);
                    return context;
                }
            }
            return new TypeLoaderTypeSystemContext(new TargetDetails(
#if TARGET_ARM
            TargetArchitecture.ARM,
#elif TARGET_ARM64
            TargetArchitecture.ARM64,
#elif TARGET_X86
            TargetArchitecture.X86,
#elif TARGET_AMD64
            TargetArchitecture.X64,
#elif TARGET_WASM
            TargetArchitecture.Wasm32,
#elif TARGET_LOONGARCH64
            TargetArchitecture.LoongArch64,
#elif TARGET_RISCV64
            TargetArchitecture.RiscV64,
#else
#error Unknown architecture
#endif
            TargetOS.Windows,
            TargetAbi.Unknown));
        }

        public static void Recycle(TypeSystemContext context)
        {
            // Only cache a reasonably small context that is still in Gen0
            if (context.LoadFactor > 200 || GC.GetGeneration(context) > 0)
                return;

            // Flush the type system context from all types being recycled
            context.FlushTypeBuilderStates();

            // No lock needed here - the reference assignment is atomic
            s_cachedContext.SetTarget(context);
        }
    }
}
