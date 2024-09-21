#ifndef _TEST_REGTEST_BASELINE_H_
#define _TEST_REGTEST_BASELINE_H_

#include <internal/dnmd_platform.hpp>
#include <corsym.h>
#include <metadata.h>

namespace TestBaseline
{
    extern dncp::com_ptr<IMetaDataDispenser> Metadata;
    extern dncp::com_ptr<IMetaDataDispenserEx> DeltaMetadataBuilder;
    extern dncp::com_ptr<ISymUnmanagedBinder> Symbol;

    using MetaDataInternalInterfaceFactory = HRESULT(*)(void const*, uint32_t, uint32_t, const GUID&, void**);
    extern MetaDataInternalInterfaceFactory InternalMetadata;
}

#endif // !_TEST_REGTEST_BASELINE_H_