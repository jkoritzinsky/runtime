// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

namespace Mono.Linker.Tests.Cases.Extensibility.Dependencies
{
    public interface InterfaceType
    {
#if INCLUDE_ABSTRACT_METHOD
        public abstract void AbstractMethod();
#endif

        public static void UseInstance(InterfaceType instance)
        {
#if INCLUDE_ABSTRACT_METHOD
            instance.AbstractMethod();
#endif
        }
    }
}
