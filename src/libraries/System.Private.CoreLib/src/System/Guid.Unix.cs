// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Runtime.CompilerServices;

namespace System
{
    public partial struct Guid
    {
        // This will create a new random guid based on the https://www.ietf.org/rfc/rfc4122.txt
        public static unsafe Guid NewGuid()
        {
            Guid g;
#if !TARGET_WASI
            // Guid.NewGuid is often used as a cheap source of random data that are sometimes used for security purposes.
            // Windows implementation uses secure RNG to implement it. We use secure RNG for Unix too to avoid subtle security
            // vulnerabilities in applications that depend on it. See https://github.com/dotnet/runtime/issues/42752 for details.
            Interop.GetCryptographicallySecureRandomBytes((byte*)&g, sizeof(Guid));
#else
            // TODOWASI: crypto secure random bytes
            Interop.GetRandomBytes((byte*)&g, sizeof(Guid));
#endif

            // Modify bits indicating the type of the GUID

            unchecked
            {
                // time_hi_and_version
                Unsafe.AsRef(in g._c) = (short)((g._c & ~VersionMask) | Version4Value);
                // clock_seq_hi_and_reserved
                Unsafe.AsRef(in g._d) = (byte)((g._d & ~Variant10xxMask) | Variant10xxValue);
            }

            return g;
        }
    }
}
