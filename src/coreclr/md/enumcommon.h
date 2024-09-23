#ifndef MD_ENUMCOMMON_H
#define MD_ENUMCOMMON_H
#include "metadata.h"

class TOKENLIST : public CDynArray<mdToken>
{
};

class HENUMInternalManipulator
{
public:
    ULONG EnumMethodImplGetCount(
        HENUMInternal   *phEnumBody,        // [IN] MethodBody enumerator.
        HENUMInternal   *phEnumDecl)        // [IN] MethodDecl enumerator.
    {
        return phEnumBody->m_ulCount;
    }
    bool EnumNext(
        HENUMInternal *phEnum,              // [IN] the enumerator to retrieve information
        mdToken     *ptk)                   // [OUT] token to scope the search
    {
        _ASSERTE(phEnum && ptk);
        if (phEnum->u.m_ulCur >= phEnum->u.m_ulEnd)
            return false;

        if ( phEnum->m_EnumType == MDSimpleEnum )
        {
            *ptk = phEnum->u.m_ulCur | phEnum->m_tkKind;
            phEnum->u.m_ulCur++;
        }
        else
        {
            TOKENLIST       *pdalist = (TOKENLIST *)&(phEnum->m_cursor);

            _ASSERTE( phEnum->m_EnumType == MDDynamicArrayEnum );
            *ptk = *( pdalist->Get(phEnum->u.m_ulCur++) );
        }
        return true;
    }
    ULONG EnumGetCount(
        HENUMInternal *phEnum)        // [IN] the enumerator to retrieve information
    {
        _ASSERTE(phEnum);
        return phEnum->m_ulCount;
    }
    void EnumReset(
        HENUMInternal *phEnum)        // [IN] the enumerator to be reset
    {
        _ASSERTE(phEnum);
        _ASSERTE( phEnum->m_EnumType == MDSimpleEnum || phEnum->m_EnumType == MDDynamicArrayEnum);

        phEnum->u.m_ulCur = phEnum->u.m_ulStart;
    } // MDInternalRW::EnumReset
    void EnumClose(
        HENUMInternal *phEnum)        // [IN] the enumerator to be closed
    {
        _ASSERTE( phEnum->m_EnumType == MDSimpleEnum ||
            phEnum->m_EnumType == MDDynamicArrayEnum);
        if (phEnum->m_EnumType == MDDynamicArrayEnum)
            HENUMInternal::ClearEnum(phEnum);
    }
};

#endif // MD_ENUMCOMMON_H
