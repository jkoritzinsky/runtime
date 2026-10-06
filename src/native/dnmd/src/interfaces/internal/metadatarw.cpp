// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "metadatarw.hpp"
#include "../metadataemit.hpp"
#include "../enclog.hpp"
#include "../hcorenum.hpp"
#include "dnmd_interfaces.hpp"

#include <cstring>

InternalMetadataRW::InternalMetadataRW(
    IUnknown* controllingUnknown, mdhandle_view handle, MetadataEmit* emit)
    : TearOffBase(controllingUnknown)
    , _handle{ handle }
    , _import{ controllingUnknown, handle }
    , _emit{ emit }
{
    assert(_emit != nullptr);
}

bool InternalMetadataRW::TryGetInterfaceOnThis(REFIID riid, void** ppvObject)
{
    assert(riid != IID_IUnknown);
    if (riid == IID_IMDInternalImport)
    {
        *ppvObject = static_cast<IMDInternalImport*>(this);
        return true;
    }
    if (riid == IID_IMDInternalImportENC)
    {
        *ppvObject = static_cast<IMDInternalImportENC*>(this);
        return true;
    }
    if (riid == IID_IGetIMDInternalImport)
    {
        *ppvObject = static_cast<IGetIMDInternalImport*>(this);
        return true;
    }
    if (riid == IID_IMDInternalEmit)
    {
        *ppvObject = static_cast<IMDInternalEmit*>(this);
        return true;
    }
    return false;
}

STDMETHODIMP InternalMetadataRW::GetIMDInternalImport(IMDInternalImport** ppInternalImport)
{
    if (ppInternalImport == nullptr)
        return E_POINTER;
    *ppInternalImport = static_cast<IMDInternalImport*>(this);
    (void)AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) InternalMetadataRW::GetCountWithTokenKind(DWORD tkKind)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetCountWithTokenKind(tkKind);
}

STDMETHODIMP InternalMetadataRW::EnumTypeDefInit(HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumTypeDefInit(phEnum);
}

STDMETHODIMP InternalMetadataRW::EnumMethodImplInit(
    mdTypeDef td,
    HENUMInternal *phEnumBody,
    HENUMInternal *phEnumDecl)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumMethodImplInit(td, phEnumBody, phEnumDecl);
}

STDMETHODIMP_(ULONG) InternalMetadataRW::EnumMethodImplGetCount(
    HENUMInternal *phEnumBody,
    HENUMInternal *phEnumDecl)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumMethodImplGetCount(phEnumBody, phEnumDecl);
}

STDMETHODIMP InternalMetadataRW::EnumMethodImplNext(
    HENUMInternal *phEnumBody,
    HENUMInternal *phEnumDecl,
    mdToken *ptkBody,
    mdToken *ptkDecl)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumMethodImplNext(phEnumBody, phEnumDecl, ptkBody, ptkDecl);
}

STDMETHODIMP InternalMetadataRW::EnumGlobalFunctionsInit(HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumGlobalFunctionsInit(phEnum);
}

STDMETHODIMP InternalMetadataRW::EnumGlobalFieldsInit(HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumGlobalFieldsInit(phEnum);
}

STDMETHODIMP InternalMetadataRW::EnumInit(DWORD tkKind, mdToken tkParent, HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumInit(tkKind, tkParent, phEnum);
}

STDMETHODIMP InternalMetadataRW::EnumAllInit(DWORD tkKind, HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumAllInit(tkKind, phEnum);
}

STDMETHODIMP_(bool) InternalMetadataRW::EnumNext(HENUMInternal *phEnum, mdToken *ptk)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumNext(phEnum, ptk);
}

STDMETHODIMP_(ULONG) InternalMetadataRW::EnumGetCount(HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumGetCount(phEnum);
}

STDMETHODIMP_(void) InternalMetadataRW::EnumReset(HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumReset(phEnum);
}

STDMETHODIMP_(void) InternalMetadataRW::EnumClose(HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumClose(phEnum);
}

STDMETHODIMP InternalMetadataRW::EnumCustomAttributeByNameInit(
    mdToken tkParent,
    LPCSTR szName,
    HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumCustomAttributeByNameInit(tkParent, szName, phEnum);
}

STDMETHODIMP InternalMetadataRW::GetParentToken(mdToken tkChild, mdToken *ptkParent)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetParentToken(tkChild, ptkParent);
}

STDMETHODIMP InternalMetadataRW::GetCustomAttributeProps(mdCustomAttribute at, mdToken *ptkType)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetCustomAttributeProps(at, ptkType);
}

STDMETHODIMP InternalMetadataRW::GetCustomAttributeAsBlob(
    mdCustomAttribute cv,
    void const **ppBlob,
    ULONG *pcbSize)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetCustomAttributeAsBlob(cv, ppBlob, pcbSize);
}

STDMETHODIMP InternalMetadataRW::GetScopeProps(LPCSTR *pszName, GUID *pmvid)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetScopeProps(pszName, pmvid);
}

STDMETHODIMP InternalMetadataRW::FindMethodDef(
    mdTypeDef classdef,
    LPCSTR szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG cbSigBlob,
    mdMethodDef *pmd)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindMethodDef(classdef, szName, pvSigBlob, cbSigBlob, pmd);
}

STDMETHODIMP InternalMetadataRW::FindParamOfMethod(mdMethodDef md, ULONG iSeq, mdParamDef *pparamdef)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindParamOfMethod(md, iSeq, pparamdef);
}

STDMETHODIMP InternalMetadataRW::GetNameOfTypeDef(mdTypeDef classdef, LPCSTR *pszname, LPCSTR *psznamespace)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameOfTypeDef(classdef, pszname, psznamespace);
}

STDMETHODIMP InternalMetadataRW::GetIsDualOfTypeDef(mdTypeDef classdef, ULONG *pDual)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetIsDualOfTypeDef(classdef, pDual);
}

STDMETHODIMP InternalMetadataRW::GetIfaceTypeOfTypeDef(mdTypeDef classdef, ULONG *pIface)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetIfaceTypeOfTypeDef(classdef, pIface);
}

STDMETHODIMP InternalMetadataRW::GetNameOfMethodDef(mdMethodDef md, LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameOfMethodDef(md, pszName);
}

STDMETHODIMP InternalMetadataRW::GetNameAndSigOfMethodDef(
    mdMethodDef methoddef,
    PCCOR_SIGNATURE *ppvSigBlob,
    ULONG *pcbSigBlob,
    LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameAndSigOfMethodDef(methoddef, ppvSigBlob, pcbSigBlob, pszName);
}

STDMETHODIMP InternalMetadataRW::GetNameOfFieldDef(mdFieldDef fd, LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameOfFieldDef(fd, pszName);
}

STDMETHODIMP InternalMetadataRW::GetNameOfTypeRef(mdTypeRef classref, LPCSTR *psznamespace, LPCSTR *pszname)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameOfTypeRef(classref, psznamespace, pszname);
}

STDMETHODIMP InternalMetadataRW::GetResolutionScopeOfTypeRef(
    mdTypeRef classref,
    mdToken *ptkResolutionScope)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetResolutionScopeOfTypeRef(classref, ptkResolutionScope);
}

STDMETHODIMP InternalMetadataRW::FindTypeRefByName(
    LPCSTR szNamespace,
    LPCSTR szName,
    mdToken tkResolutionScope,
    mdTypeRef *ptk)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindTypeRefByName(szNamespace, szName, tkResolutionScope, ptk);
}

STDMETHODIMP InternalMetadataRW::GetTypeDefProps(mdTypeDef classdef, DWORD *pdwAttr, mdToken *ptkExtends)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetTypeDefProps(classdef, pdwAttr, ptkExtends);
}

STDMETHODIMP InternalMetadataRW::GetItemGuid(mdToken tkObj, CLSID *pGuid)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetItemGuid(tkObj, pGuid);
}

STDMETHODIMP InternalMetadataRW::GetNestedClassProps(mdTypeDef tkNestedClass, mdTypeDef *ptkEnclosingClass)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNestedClassProps(tkNestedClass, ptkEnclosingClass);
}

STDMETHODIMP InternalMetadataRW::GetCountNestedClasses(
    mdTypeDef tkEnclosingClass,
    ULONG *pcNestedClassesCount)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetCountNestedClasses(tkEnclosingClass, pcNestedClassesCount);
}

STDMETHODIMP InternalMetadataRW::GetNestedClasses(
    mdTypeDef tkEnclosingClass,
    mdTypeDef *rNestedClasses,
    ULONG ulNestedClasses,
    ULONG *pcNestedClasses)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNestedClasses(tkEnclosingClass, rNestedClasses, ulNestedClasses, pcNestedClasses);
}

STDMETHODIMP InternalMetadataRW::GetModuleRefProps(mdModuleRef mur, LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetModuleRefProps(mur, pszName);
}

STDMETHODIMP InternalMetadataRW::GetSigOfMethodDef(
    mdMethodDef tkMethodDef,
    ULONG * pcbSigBlob,
    PCCOR_SIGNATURE * ppSig)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetSigOfMethodDef(tkMethodDef, pcbSigBlob, ppSig);
}

STDMETHODIMP InternalMetadataRW::GetSigOfFieldDef(
    mdFieldDef tkFieldDef,
    ULONG * pcbSigBlob,
    PCCOR_SIGNATURE * ppSig)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetSigOfFieldDef(tkFieldDef, pcbSigBlob, ppSig);
}

STDMETHODIMP InternalMetadataRW::GetSigFromToken(mdToken tk, ULONG * pcbSig, PCCOR_SIGNATURE * ppSig)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetSigFromToken(tk, pcbSig, ppSig);
}

STDMETHODIMP InternalMetadataRW::GetMethodDefProps(mdMethodDef md, DWORD *pdwFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetMethodDefProps(md, pdwFlags);
}

STDMETHODIMP InternalMetadataRW::GetMethodImplProps(mdToken tk, ULONG *pulCodeRVA, DWORD *pdwImplFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetMethodImplProps(tk, pulCodeRVA, pdwImplFlags);
}

STDMETHODIMP InternalMetadataRW::GetFieldRVA(mdFieldDef fd, ULONG *pulCodeRVA)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetFieldRVA(fd, pulCodeRVA);
}

STDMETHODIMP InternalMetadataRW::GetFieldDefProps(mdFieldDef fd, DWORD *pdwFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetFieldDefProps(fd, pdwFlags);
}

STDMETHODIMP InternalMetadataRW::GetDefaultValue(mdToken tk, MDDefaultValue *pDefaultValue)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetDefaultValue(tk, pDefaultValue);
}

STDMETHODIMP InternalMetadataRW::GetDispIdOfMemberDef(mdToken tk, ULONG *pDispid)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetDispIdOfMemberDef(tk, pDispid);
}

STDMETHODIMP InternalMetadataRW::GetTypeOfInterfaceImpl(mdInterfaceImpl iiImpl, mdToken *ptkType)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetTypeOfInterfaceImpl(iiImpl, ptkType);
}

STDMETHODIMP InternalMetadataRW::FindTypeDef(
    LPCSTR szNamespace,
    LPCSTR szName,
    mdToken tkEnclosingClass,
    mdTypeDef *ptypedef)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindTypeDef(szNamespace, szName, tkEnclosingClass, ptypedef);
}

STDMETHODIMP InternalMetadataRW::GetNameAndSigOfMemberRef(
    mdMemberRef memberref,
    PCCOR_SIGNATURE *ppvSigBlob,
    ULONG *pcbSigBlob,
    LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameAndSigOfMemberRef(memberref, ppvSigBlob, pcbSigBlob, pszName);
}

STDMETHODIMP InternalMetadataRW::GetParentOfMemberRef(mdMemberRef memberref, mdToken *ptkParent)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetParentOfMemberRef(memberref, ptkParent);
}

STDMETHODIMP InternalMetadataRW::GetParamDefProps(
    mdParamDef paramdef,
    USHORT *pusSequence,
    DWORD *pdwAttr,
    LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetParamDefProps(paramdef, pusSequence, pdwAttr, pszName);
}

STDMETHODIMP InternalMetadataRW::GetPropertyInfoForMethodDef(
    mdMethodDef md,
    mdProperty *ppd,
    LPCSTR *pName,
    ULONG *pSemantic)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetPropertyInfoForMethodDef(md, ppd, pName, pSemantic);
}

STDMETHODIMP InternalMetadataRW::GetClassPackSize(mdTypeDef td, ULONG *pdwPackSize)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetClassPackSize(td, pdwPackSize);
}

STDMETHODIMP InternalMetadataRW::GetClassTotalSize(mdTypeDef td, ULONG *pdwClassSize)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetClassTotalSize(td, pdwClassSize);
}

STDMETHODIMP InternalMetadataRW::GetClassLayoutInit(mdTypeDef td, MD_CLASS_LAYOUT *pLayout)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetClassLayoutInit(td, pLayout);
}

STDMETHODIMP InternalMetadataRW::GetClassLayoutNext(
    MD_CLASS_LAYOUT *pLayout,
    mdFieldDef *pfd,
    ULONG *pulOffset)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetClassLayoutNext(pLayout, pfd, pulOffset);
}

STDMETHODIMP InternalMetadataRW::GetFieldMarshal(
    mdFieldDef fd,
    PCCOR_SIGNATURE *pSigNativeType,
    ULONG *pcbNativeType)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetFieldMarshal(fd, pSigNativeType, pcbNativeType);
}

STDMETHODIMP InternalMetadataRW::FindProperty(mdTypeDef td, LPCSTR szPropName, mdProperty *pProp)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindProperty(td, szPropName, pProp);
}

STDMETHODIMP InternalMetadataRW::GetPropertyProps(
    mdProperty prop,
    LPCSTR *szProperty,
    DWORD *pdwPropFlags,
    PCCOR_SIGNATURE *ppvSig,
    ULONG *pcbSig)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetPropertyProps(prop, szProperty, pdwPropFlags, ppvSig, pcbSig);
}

STDMETHODIMP InternalMetadataRW::FindEvent(mdTypeDef td, LPCSTR szEventName, mdEvent *pEvent)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindEvent(td, szEventName, pEvent);
}

STDMETHODIMP InternalMetadataRW::GetEventProps(
    mdEvent ev,
    LPCSTR *pszEvent,
    DWORD *pdwEventFlags,
    mdToken *ptkEventType)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetEventProps(ev, pszEvent, pdwEventFlags, ptkEventType);
}

STDMETHODIMP InternalMetadataRW::FindAssociate(mdToken evprop, DWORD associate, mdMethodDef *pmd)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindAssociate(evprop, associate, pmd);
}

STDMETHODIMP InternalMetadataRW::EnumAssociateInit(mdToken evprop, HENUMInternal *phEnum)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.EnumAssociateInit(evprop, phEnum);
}

STDMETHODIMP InternalMetadataRW::GetAllAssociates(
    HENUMInternal *phEnum,
    ASSOCIATE_RECORD *pAssociateRec,
    ULONG cAssociateRec)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetAllAssociates(phEnum, pAssociateRec, cAssociateRec);
}

STDMETHODIMP InternalMetadataRW::GetPermissionSetProps(
    mdPermission pm,
    DWORD *pdwAction,
    void const **ppvPermission,
    ULONG *pcbPermission)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetPermissionSetProps(pm, pdwAction, ppvPermission, pcbPermission);
}

STDMETHODIMP InternalMetadataRW::GetUserString(mdString stk, ULONG *pchString, LPCWSTR *pwszUserString)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetUserString(stk, pchString, pwszUserString);
}

STDMETHODIMP InternalMetadataRW::GetPinvokeMap(
    mdToken tk,
    DWORD *pdwMappingFlags,
    LPCSTR *pszImportName,
    mdModuleRef *pmrImportDLL)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetPinvokeMap(tk, pdwMappingFlags, pszImportName, pmrImportDLL);
}

STDMETHODIMP InternalMetadataRW::ConvertTextSigToComSig(
    BOOL fCreateTrIfNotFound,
    LPCSTR pSignature,
    CQuickBytes *pqbNewSig,
    ULONG *pcbCount)
{
    return _import.ConvertTextSigToComSig(fCreateTrIfNotFound, pSignature, pqbNewSig, pcbCount);
}

STDMETHODIMP InternalMetadataRW::GetAssemblyProps(
    mdAssembly mda,
    const void **ppbPublicKey,
    ULONG *pcbPublicKey,
    ULONG *pulHashAlgId,
    LPCSTR *pszName,
    AssemblyMetaDataInternal *pMetaData,
    DWORD *pdwAssemblyFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetAssemblyProps(
        mda,
        ppbPublicKey,
        pcbPublicKey,
        pulHashAlgId,
        pszName,
        pMetaData,
        pdwAssemblyFlags);
}

STDMETHODIMP InternalMetadataRW::GetAssemblyRefProps(
    mdAssemblyRef mdar,
    const void **ppbPublicKeyOrToken,
    ULONG *pcbPublicKeyOrToken,
    LPCSTR *pszName,
    AssemblyMetaDataInternal *pMetaData,
    const void **ppbHashValue,
    ULONG *pcbHashValue,
    DWORD *pdwAssemblyRefFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetAssemblyRefProps(
        mdar,
        ppbPublicKeyOrToken,
        pcbPublicKeyOrToken,
        pszName,
        pMetaData,
        ppbHashValue,
        pcbHashValue,
        pdwAssemblyRefFlags);
}

STDMETHODIMP InternalMetadataRW::GetFileProps(
    mdFile mdf,
    LPCSTR *pszName,
    const void **ppbHashValue,
    ULONG *pcbHashValue,
    DWORD *pdwFileFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetFileProps(mdf, pszName, ppbHashValue, pcbHashValue, pdwFileFlags);
}

STDMETHODIMP InternalMetadataRW::GetExportedTypeProps(
    mdExportedType mdct,
    LPCSTR *pszNamespace,
    LPCSTR *pszName,
    mdToken *ptkImplementation,
    mdTypeDef *ptkTypeDef,
    DWORD *pdwExportedTypeFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetExportedTypeProps(
        mdct,
        pszNamespace,
        pszName,
        ptkImplementation,
        ptkTypeDef,
        pdwExportedTypeFlags);
}

STDMETHODIMP InternalMetadataRW::GetManifestResourceProps(
    mdManifestResource mdmr,
    LPCSTR *pszName,
    mdToken *ptkImplementation,
    DWORD *pdwOffset,
    DWORD *pdwResourceFlags)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetManifestResourceProps(mdmr, pszName, ptkImplementation, pdwOffset, pdwResourceFlags);
}

STDMETHODIMP InternalMetadataRW::FindExportedTypeByName(
    LPCSTR szNamespace,
    LPCSTR szName,
    mdExportedType tkEnclosingType,
    mdExportedType *pmct)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindExportedTypeByName(szNamespace, szName, tkEnclosingType, pmct);
}

STDMETHODIMP InternalMetadataRW::FindManifestResourceByName(LPCSTR szName, mdManifestResource *pmmr)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindManifestResourceByName(szName, pmmr);
}

STDMETHODIMP InternalMetadataRW::GetAssemblyFromScope(mdAssembly *ptkAssembly)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetAssemblyFromScope(ptkAssembly);
}

STDMETHODIMP InternalMetadataRW::GetCustomAttributeByName(
    mdToken tkObj,
    LPCSTR szName,
    const void **ppData,
    ULONG *pcbData)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetCustomAttributeByName(tkObj, szName, ppData, pcbData);
}

STDMETHODIMP InternalMetadataRW::GetTypeSpecFromToken(
    mdTypeSpec typespec,
    PCCOR_SIGNATURE *ppvSig,
    ULONG *pcbSig)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetTypeSpecFromToken(typespec, ppvSig, pcbSig);
}

STDMETHODIMP_(BOOL) InternalMetadataRW::IsValidToken(mdToken tk)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.IsValidToken(tk);
}

STDMETHODIMP_(IMetaModelCommon*) InternalMetadataRW::GetMetaModelCommon()
{
    return _import.GetMetaModelCommon();
}

STDMETHODIMP_(IUnknown *) InternalMetadataRW::GetCachedPublicInterface(BOOL fWithLock)
{
    return _import.GetCachedPublicInterface(fWithLock);
}

STDMETHODIMP InternalMetadataRW::SetCachedPublicInterface(IUnknown *pUnk)
{
    return _import.SetCachedPublicInterface(pUnk);
}

STDMETHODIMP_(mdModule) InternalMetadataRW::GetModuleFromScope()
{
    return _import.GetModuleFromScope();
}

STDMETHODIMP InternalMetadataRW::FindMethodDefUsingCompare(
    mdTypeDef classdef,
    LPCSTR szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG cbSigBlob,
    PSIGCOMPARE pSignatureCompare,
    void* pSignatureArgs,
    mdMethodDef *pmd)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.FindMethodDefUsingCompare(
        classdef,
        szName,
        pvSigBlob,
        cbSigBlob,
        pSignatureCompare,
        pSignatureArgs,
        pmd);
}

STDMETHODIMP InternalMetadataRW::GetFieldOffset(mdFieldDef fd, ULONG *pulOffset)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetFieldOffset(fd, pulOffset);
}

STDMETHODIMP InternalMetadataRW::GetMethodSpecProps(
    mdMethodSpec ms,
    mdToken *tkParent,
    PCCOR_SIGNATURE *ppvSigBlob,
    ULONG *pcbSigBlob)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetMethodSpecProps(ms, tkParent, ppvSigBlob, pcbSigBlob);
}

STDMETHODIMP InternalMetadataRW::GetTableInfoWithIndex(ULONG index, void **pTable, void **pTableSize)
{
    return _import.GetTableInfoWithIndex(index, pTable, pTableSize);
}

STDMETHODIMP InternalMetadataRW::ApplyEditAndContinue(MDInternalRW* pDeltaMD)
{
    return _import.ApplyEditAndContinue(pDeltaMD);
}

STDMETHODIMP InternalMetadataRW::GetGenericParamProps(
    mdGenericParam rd,
    ULONG* pulSequence,
    DWORD* pdwAttr,
    mdToken *ptOwner,
    DWORD *reserved,
    LPCSTR *szName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetGenericParamProps(rd, pulSequence, pdwAttr, ptOwner, reserved, szName);
}

STDMETHODIMP InternalMetadataRW::GetGenericParamConstraintProps(
    mdGenericParamConstraint rd,
    mdGenericParam *ptGenericParam,
    mdToken *ptkConstraintType)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetGenericParamConstraintProps(rd, ptGenericParam, ptkConstraintType);
}

STDMETHODIMP InternalMetadataRW::GetVersionString(LPCSTR *pVer)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetVersionString(pVer);
}

STDMETHODIMP InternalMetadataRW::GetTypeDefRefTokenInTypeSpec(
    mdTypeSpec tkTypeSpec,
    mdToken *tkEnclosedToken)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetTypeDefRefTokenInTypeSpec(tkTypeSpec, tkEnclosedToken);
}

STDMETHODIMP_(DWORD) InternalMetadataRW::GetMetadataStreamVersion()
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetMetadataStreamVersion();
}

STDMETHODIMP InternalMetadataRW::GetNameOfCustomAttribute(
    mdCustomAttribute mdAttribute,
    LPCSTR *pszNamespace,
    LPCSTR *pszName)
{
    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.GetNameOfCustomAttribute(mdAttribute, pszNamespace, pszName);
}

STDMETHODIMP InternalMetadataRW::SetModuleProps(LPCWSTR szName)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetModuleProps(szName);
}

STDMETHODIMP InternalMetadataRW::GetSaveSize(CorSaveSize fSave, DWORD *pdwSaveSize)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->GetSaveSize(fSave, pdwSaveSize);
}

STDMETHODIMP InternalMetadataRW::SaveToMemory(void *pbData, ULONG cbData)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SaveToMemory(pbData, cbData);
}

STDMETHODIMP InternalMetadataRW::DefineTypeDef(
    LPCWSTR szTypeDef,
    DWORD dwTypeDefFlags,
    mdToken tkExtends,
    mdToken rtkImplements[],
    mdTypeDef *ptd)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineTypeDef(szTypeDef, dwTypeDefFlags, tkExtends, rtkImplements, ptd);
}

STDMETHODIMP InternalMetadataRW::DefineNestedType(
    LPCWSTR szTypeDef,
    DWORD dwTypeDefFlags,
    mdToken tkExtends,
    mdToken rtkImplements[],
    mdTypeDef tdEncloser,
    mdTypeDef *ptd)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineNestedType(szTypeDef, dwTypeDefFlags, tkExtends, rtkImplements, tdEncloser, ptd);
}

STDMETHODIMP InternalMetadataRW::DefineMethod(
    mdTypeDef td,
    LPCWSTR szName,
    DWORD dwMethodFlags,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG cbSigBlob,
    ULONG ulCodeRVA,
    DWORD dwImplFlags,
    mdMethodDef *pmd)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineMethod(
        td,
        szName,
        dwMethodFlags,
        pvSigBlob,
        cbSigBlob,
        ulCodeRVA,
        dwImplFlags,
        pmd);
}

STDMETHODIMP InternalMetadataRW::DefineMethodImpl(mdTypeDef td, mdToken tkBody, mdToken tkDecl)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineMethodImpl(td, tkBody, tkDecl);
}

STDMETHODIMP InternalMetadataRW::DefineTypeRefByName(
    mdToken tkResolutionScope,
    LPCWSTR szName,
    mdTypeRef *ptr)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineTypeRefByName(tkResolutionScope, szName, ptr);
}

STDMETHODIMP InternalMetadataRW::DefineMemberRef(
    mdToken tkImport,
    LPCWSTR szName,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG cbSigBlob,
    mdMemberRef *pmr)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineMemberRef(tkImport, szName, pvSigBlob, cbSigBlob, pmr);
}

STDMETHODIMP InternalMetadataRW::SetClassLayout(
    mdTypeDef td,
    DWORD dwPackSize,
    COR_FIELD_OFFSET rFieldOffsets[],
    ULONG ulClassSize)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetClassLayout(td, dwPackSize, rFieldOffsets, ulClassSize);
}

STDMETHODIMP InternalMetadataRW::GetTokenFromSig(PCCOR_SIGNATURE pvSig, ULONG cbSig, mdSignature *pmsig)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->GetTokenFromSig(pvSig, cbSig, pmsig);
}

STDMETHODIMP InternalMetadataRW::DefineModuleRef(LPCWSTR szName, mdModuleRef *pmur)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineModuleRef(szName, pmur);
}

STDMETHODIMP InternalMetadataRW::GetTokenFromTypeSpec(
    PCCOR_SIGNATURE pvSig,
    ULONG cbSig,
    mdTypeSpec *ptypespec)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->GetTokenFromTypeSpec(pvSig, cbSig, ptypespec);
}

STDMETHODIMP InternalMetadataRW::DefineUserString(LPCWSTR szString, ULONG cchString, mdString *pstk)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineUserString(szString, cchString, pstk);
}

STDMETHODIMP InternalMetadataRW::SetMethodProps(
    mdMethodDef md,
    DWORD dwMethodFlags,
    ULONG ulCodeRVA,
    DWORD dwImplFlags)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetMethodProps(md, dwMethodFlags, ulCodeRVA, dwImplFlags);
}

STDMETHODIMP InternalMetadataRW::DefinePinvokeMap(
    mdToken tk,
    DWORD dwMappingFlags,
    LPCWSTR szImportName,
    mdModuleRef mrImportDLL)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefinePinvokeMap(tk, dwMappingFlags, szImportName, mrImportDLL);
}

STDMETHODIMP InternalMetadataRW::DefineCustomAttribute(
    mdToken tkOwner,
    mdToken tkCtor,
    void const *pCustomAttribute,
    ULONG cbCustomAttribute,
    mdCustomAttribute *pcv)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineCustomAttribute(tkOwner, tkCtor, pCustomAttribute, cbCustomAttribute, pcv);
}

STDMETHODIMP InternalMetadataRW::DefineField(
    mdTypeDef td,
    LPCWSTR szName,
    DWORD dwFieldFlags,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG cbSigBlob,
    DWORD dwCPlusTypeFlag,
    void const *pValue,
    ULONG cchValue,
    mdFieldDef *pmd)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineField(
        td,
        szName,
        dwFieldFlags,
        pvSigBlob,
        cbSigBlob,
        dwCPlusTypeFlag,
        pValue,
        cchValue,
        pmd);
}

STDMETHODIMP InternalMetadataRW::DefineProperty(
    mdTypeDef td,
    LPCWSTR szProperty,
    DWORD dwPropFlags,
    PCCOR_SIGNATURE pvSig,
    ULONG cbSig,
    DWORD dwCPlusTypeFlag,
    void const *pValue,
    ULONG cchValue,
    mdMethodDef mdSetter,
    mdMethodDef mdGetter,
    mdMethodDef rmdOtherMethods[],
    mdProperty *pmdProp)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineProperty(
        td,
        szProperty,
        dwPropFlags,
        pvSig,
        cbSig,
        dwCPlusTypeFlag,
        pValue,
        cchValue,
        mdSetter,
        mdGetter,
        rmdOtherMethods,
        pmdProp);
}

STDMETHODIMP InternalMetadataRW::DefineParam(
    mdMethodDef md,
    ULONG ulParamSeq,
    LPCWSTR szName,
    DWORD dwParamFlags,
    DWORD dwCPlusTypeFlag,
    void const *pValue,
    ULONG cchValue,
    mdParamDef *ppd)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineParam(md, ulParamSeq, szName, dwParamFlags, dwCPlusTypeFlag, pValue, cchValue, ppd);
}

STDMETHODIMP InternalMetadataRW::SetFieldProps(
    mdFieldDef fd,
    DWORD dwFieldFlags,
    DWORD dwCPlusTypeFlag,
    void const *pValue,
    ULONG cchValue)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetFieldProps(fd, dwFieldFlags, dwCPlusTypeFlag, pValue, cchValue);
}

STDMETHODIMP InternalMetadataRW::SetPropertyProps(
    mdProperty pr,
    DWORD dwPropFlags,
    DWORD dwCPlusTypeFlag,
    void const *pValue,
    ULONG cchValue,
    mdMethodDef mdSetter,
    mdMethodDef mdGetter,
    mdMethodDef rmdOtherMethods[])
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetPropertyProps(
        pr,
        dwPropFlags,
        dwCPlusTypeFlag,
        pValue,
        cchValue,
        mdSetter,
        mdGetter,
        rmdOtherMethods);
}

STDMETHODIMP InternalMetadataRW::SetParamProps(
    mdParamDef pd,
    LPCWSTR szName,
    DWORD dwParamFlags,
    DWORD dwCPlusTypeFlag,
    void const *pValue,
    ULONG cchValue)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetParamProps(pd, szName, dwParamFlags, dwCPlusTypeFlag, pValue, cchValue);
}

STDMETHODIMP InternalMetadataRW::SetMethodImplFlags(mdMethodDef md, DWORD dwImplFlags)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetMethodImplFlags(md, dwImplFlags);
}

STDMETHODIMP InternalMetadataRW::SetFieldRVA(mdFieldDef fd, ULONG ulRVA)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetFieldRVA(fd, ulRVA);
}

STDMETHODIMP InternalMetadataRW::DefineMethodSpec(
    mdToken tkParent,
    PCCOR_SIGNATURE pvSigBlob,
    ULONG cbSigBlob,
    mdMethodSpec *pmi)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineMethodSpec(tkParent, pvSigBlob, cbSigBlob, pmi);
}

STDMETHODIMP InternalMetadataRW::DefineGenericParam(
    mdToken tk,
    ULONG ulParamSeq,
    DWORD dwParamFlags,
    LPCWSTR szName,
    DWORD reserved,
    mdToken rtkConstraints[],
    mdGenericParam *pgp)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineGenericParam(tk, ulParamSeq, dwParamFlags, szName, reserved, rtkConstraints, pgp);
}

STDMETHODIMP InternalMetadataRW::DefineAssembly(
    const void *pbPublicKey,
    ULONG cbPublicKey,
    ULONG ulHashAlgId,
    LPCWSTR szName,
    const ASSEMBLYMETADATA *pMetaData,
    DWORD dwAssemblyFlags,
    mdAssembly *pma)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineAssembly(
        pbPublicKey,
        cbPublicKey,
        ulHashAlgId,
        szName,
        pMetaData,
        dwAssemblyFlags,
        pma);
}

STDMETHODIMP InternalMetadataRW::DefineAssemblyRef(
    const void *pbPublicKeyOrToken,
    ULONG cbPublicKeyOrToken,
    LPCWSTR szName,
    const ASSEMBLYMETADATA *pMetaData,
    const void *pbHashValue,
    ULONG cbHashValue,
    DWORD dwAssemblyRefFlags,
    mdAssemblyRef *pmdar)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineAssemblyRef(
        pbPublicKeyOrToken,
        cbPublicKeyOrToken,
        szName,
        pMetaData,
        pbHashValue,
        cbHashValue,
        dwAssemblyRefFlags,
        pmdar);
}

STDMETHODIMP InternalMetadataRW::DefineMethodSemanticsHelper(
    mdToken tkAssociation,
    DWORD dwFlags,
    mdMethodDef md)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineMethodSemanticsHelper(tkAssociation, dwFlags, md);
}

STDMETHODIMP InternalMetadataRW::SetFieldLayoutHelper(mdFieldDef fd, ULONG ulOffset)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetFieldLayoutHelper(fd, ulOffset);
}

STDMETHODIMP InternalMetadataRW::DefineEventHelper(
    mdTypeDef td,
    LPCWSTR szEvent,
    DWORD dwEventFlags,
    mdToken tkEventType,
    mdEvent *pmdEvent)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->DefineEventHelper(td, szEvent, dwEventFlags, tkEventType, pmdEvent);
}

STDMETHODIMP InternalMetadataRW::SetTypeParent(mdTypeDef td, mdToken tkExtends)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->SetTypeParent(td, tkExtends);
}

STDMETHODIMP InternalMetadataRW::AddInterfaceImpl(mdTypeDef td, mdToken tkInterface)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    return _emit->AddInterfaceImpl(td, tkInterface);
}

STDMETHODIMP InternalMetadataRW::TranslateSigWithScope(
    IMDInternalImport* pAssemImport,
    const void* pbHashValue,
    ULONG cbHashValue,
    PCCOR_SIGNATURE pbSigBlob,
    ULONG cbSigBlob,
    IMDInternalEmit* pAssemEmit,
    IMDInternalEmit* emit,
    CQuickBytes* pqkSigEmit,
    ULONG* pcbSig)
{
    if (emit == nullptr || pqkSigEmit == nullptr || pcbSig == nullptr ||
        pbSigBlob == nullptr || cbSigBlob == 0 ||
        (pbHashValue == nullptr && cbHashValue != 0))
        return E_INVALIDARG;

    minipal::com_ptr<IUnknown> sourceIdentity, destinationIdentity;
    HRESULT hr = QueryInterface(IID_IUnknown, (void**)&sourceIdentity);
    if (FAILED(hr))
        return hr;
    hr = emit->QueryInterface(IID_IUnknown, (void**)&destinationIdentity);
    if (FAILED(hr))
        return hr;

    // The destination wrapper holds the write lock when translating into this scope.
    if (sourceIdentity.p == destinationIdentity.p)
        return _import.TranslateSigWithScope(pAssemImport, pbHashValue, cbHashValue,
            pbSigBlob, cbSigBlob, pAssemEmit, emit, pqkSigEmit, pcbSig);

    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    return _import.TranslateSigWithScope(pAssemImport, pbHashValue, cbHashValue,
        pbSigBlob, cbSigBlob, pAssemEmit, emit, pqkSigEmit, pcbSig);
}

STDMETHODIMP_(minipal_rwlock*) InternalMetadataRW::GetReaderWriterLock()
{
    return _lock.NativeHandle();
}

STDMETHODIMP InternalMetadataRW::SetReaderWriterLock(minipal_rwlock* lock)
{
    if (lock == nullptr)
        return E_INVALIDARG;
    if (lock == GetReaderWriterLock())
        return S_OK;
    if (_borrowsLock)
        return E_UNEXPECTED;

    _lock.Borrow(lock);
    _borrowsLock = true;
    return S_OK;
}

STDMETHODIMP InternalMetadataRW::ApplyEditAndContinue(
    void* pDeltaMD, ULONG cbDeltaMD, IMDInternalImport** ppv)
{
    if (ppv == nullptr)
        return E_INVALIDARG;
    *ppv = nullptr;
    if (pDeltaMD == nullptr || cbDeltaMD == 0)
        return E_INVALIDARG;

    minipal::com_ptr<IMetaDataDispenser> dispenser;
    HRESULT hr = GetDispenser(IID_IMetaDataDispenser, (void**)&dispenser);
    if (FAILED(hr))
        return hr;

    minipal::com_ptr<IMetaDataImport2> delta;
    hr = dispenser->OpenScopeOnMemory(pDeltaMD, cbDeltaMD, ofReadOnly | ofCopyMemory,
        IID_IMetaDataImport2, (IUnknown**)&delta);
    if (FAILED(hr))
        return hr;

    minipal::com_ptr<IMetaDataEmit2> emitter;
    hr = QueryInterface(IID_IMetaDataEmit2, (void**)&emitter);
    if (FAILED(hr))
        return hr;
    hr = emitter->ApplyEditAndContinue(delta.p);
    if (FAILED(hr))
        return hr;

    // As in the legacy importer, the result is the same non-owning interface.
    *ppv = this;
    return S_OK;
}

STDMETHODIMP InternalMetadataRW::EnumDeltaTokensInit(HENUMInternal* phEnum)
{
    if (phEnum == nullptr)
        return E_INVALIDARG;

    std::lock_guard<pal::ReadLock> lock{ _lock.GetReadLock() };
    HCORENUMImpl* impl = reinterpret_cast<HCORENUMImpl*>(phEnum);
    HCORENUMImpl::CreateDynamicEnumInAllocatedMemory(impl);
    HCORENUMImplInPlace_ptr cleanup{ impl };

    mdcursor_t log;
    uint32_t count;
    if (md_create_cursor(_handle.get(), mdtid_ENCLog, &log, &count))
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t token, operation;
            if (!md_get_column_value_as_constant(log, mdtENCLog_Token, &token)
                || !md_get_column_value_as_constant(log, mdtENCLog_Op, &operation))
                return CLDB_E_FILE_CORRUPT;

            if ((token & 0x80000000u) == 0 && operation == 0)
            {
                HRESULT hr = HCORENUMImpl::AddToDynamicEnum(*impl, token);
                if (FAILED(hr))
                    return hr;
            }
            if (i + 1 < count && !md_cursor_next(&log))
                return CLDB_E_FILE_CORRUPT;
        }
    }

    cleanup.release();
    return S_OK;
}

STDMETHODIMP InternalMetadataRW::SetUserContextData(IUnknown* context)
{
    if (context == nullptr)
        return E_INVALIDARG;

    std::lock_guard<std::mutex> lock{ _contextMutex };
    if (_userContext.p != nullptr)
        return E_UNEXPECTED;

    minipal::com_ptr<IUnknown> current, proposed;
    if (FAILED(QueryInterface(IID_IUnknown, (void**)&current)) ||
        FAILED(context->QueryInterface(IID_IUnknown, (void**)&proposed)) ||
        current.p == proposed.p)
        return E_INVALIDARG;

    // PEAssembly transfers its old importer reference to the new importer on a successful swap.
    _userContext.Attach(context);
    return S_OK;
}

STDMETHODIMP InternalMetadataRW::ChangeMvid(REFGUID newMvid)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    mdcursor_t module;
    if (!md_token_to_cursor(_handle.get(), TokenFromRid(1, mdtModule), &module))
        return CLDB_E_FILE_CORRUPT;

    mdguid_t mvid;
    static_assert(sizeof(mvid) == sizeof(newMvid), "Metadata and COM GUIDs must have the same size");
    std::memcpy(&mvid, &newMvid, sizeof(mvid));
    return md_set_column_value_as_guid(module, mdtModule_Mvid, mvid)
        ? enc_log::LogToken(_handle, TokenFromRid(1, mdtModule))
        : E_FAIL;
}

STDMETHODIMP InternalMetadataRW::SetMDUpdateMode(ULONG updateMode, ULONG* previousUpdateMode)
{
    std::lock_guard<pal::WriteLock> lock{ _lock.GetWriteLock() };
    ULONG originalMode = _handle.UpdateMode();
    HRESULT hr = _handle.SetUpdateMode(updateMode);
    if (FAILED(hr))
        return hr;

    if (previousUpdateMode != nullptr)
        *previousUpdateMode = originalMode;
    return S_OK;
}
