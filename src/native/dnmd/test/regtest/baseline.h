#ifndef _TEST_REGTEST_BASELINE_H_
#define _TEST_REGTEST_BASELINE_H_

class CQuickBytes;
class IMetaModelCommon;

#include <internal/dnmd_platform.hpp>
#include <corsym.h>
#include <metadata.h>

namespace TestBaseline
{
    extern minipal::com_ptr<IMetaDataDispenser> Metadata;
    extern minipal::com_ptr<IMetaDataDispenserEx> DeltaMetadataBuilder;
    extern minipal::com_ptr<ISymUnmanagedBinder> Symbol;

    using MetaDataInternalInterfaceFactory = HRESULT(*)(void const*, uint32_t, uint32_t, const GUID&, void**);
    extern MetaDataInternalInterfaceFactory InternalMetadata;
}

#endif // !_TEST_REGTEST_BASELINE_H_
