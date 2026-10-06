// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#ifndef _SRC_INTERFACES_INTERNAL_METADATARW_HPP_
#define _SRC_INTERFACES_INTERNAL_METADATARW_HPP_

#include "metadataimport.hpp"

#include <mutex>

class MetadataEmit;

class InternalMetadataRW final : public TearOffBase<IMDInternalImportENC, IGetIMDInternalImport, IMDInternalEmit>
{
    mdhandle_view _handle;
    InternalMetadataImportRO _import;
    MetadataEmit* _emit;
    pal::ReadWriteLock _lock;
    minipal::com_ptr<IUnknown> _userContext;
    std::mutex _contextMutex;
    bool _borrowsLock = false;

protected:
    bool TryGetInterfaceOnThis(REFIID riid, void** ppvObject) override;

public:
    InternalMetadataRW(IUnknown* controllingUnknown, mdhandle_view handle, MetadataEmit* emit);

    pal::ReadWriteLock* GetLock() noexcept { return &_lock; }

    STDMETHOD(GetIMDInternalImport)(IMDInternalImport** ppInternalImport) override;

    STDMETHOD_(ULONG, GetCountWithTokenKind)(DWORD tkKind) override;
    STDMETHOD(EnumTypeDefInit)(HENUMInternal *phEnum) override;
    STDMETHOD(EnumMethodImplInit)(mdTypeDef td, HENUMInternal *phEnumBody, HENUMInternal *phEnumDecl) override;
    STDMETHOD_(ULONG, EnumMethodImplGetCount)(HENUMInternal *phEnumBody, HENUMInternal *phEnumDecl) override;
    STDMETHOD(EnumMethodImplNext)(
        HENUMInternal *phEnumBody,
        HENUMInternal *phEnumDecl,
        mdToken *ptkBody,
        mdToken *ptkDecl) override;
    STDMETHOD(EnumGlobalFunctionsInit)(HENUMInternal *phEnum) override;
    STDMETHOD(EnumGlobalFieldsInit)(HENUMInternal *phEnum) override;
    STDMETHOD(EnumInit)(DWORD tkKind, mdToken tkParent, HENUMInternal *phEnum) override;
    STDMETHOD(EnumAllInit)(DWORD tkKind, HENUMInternal *phEnum) override;
    STDMETHOD_(bool, EnumNext)(HENUMInternal *phEnum, mdToken *ptk) override;
    STDMETHOD_(ULONG, EnumGetCount)(HENUMInternal *phEnum) override;
    STDMETHOD_(void, EnumReset)(HENUMInternal *phEnum) override;
    STDMETHOD_(void, EnumClose)(HENUMInternal *phEnum) override;
    STDMETHOD(EnumCustomAttributeByNameInit)(mdToken tkParent, LPCSTR szName, HENUMInternal *phEnum) override;
    STDMETHOD(GetParentToken)(mdToken tkChild, mdToken *ptkParent) override;
    STDMETHOD(GetCustomAttributeProps)(mdCustomAttribute at, mdToken *ptkType) override;
    STDMETHOD(GetCustomAttributeAsBlob)(mdCustomAttribute cv, void const **ppBlob, ULONG *pcbSize) override;
    STDMETHOD(GetScopeProps)(LPCSTR *pszName, GUID *pmvid) override;
    STDMETHOD(FindMethodDef)(
        mdTypeDef classdef,
        LPCSTR szName,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG cbSigBlob,
        mdMethodDef *pmd) override;
    STDMETHOD(FindParamOfMethod)(mdMethodDef md, ULONG iSeq, mdParamDef *pparamdef) override;
    STDMETHOD(GetNameOfTypeDef)(mdTypeDef classdef, LPCSTR *pszname, LPCSTR *psznamespace) override;
    STDMETHOD(GetIsDualOfTypeDef)(mdTypeDef classdef, ULONG *pDual) override;
    STDMETHOD(GetIfaceTypeOfTypeDef)(mdTypeDef classdef, ULONG *pIface) override;
    STDMETHOD(GetNameOfMethodDef)(mdMethodDef md, LPCSTR *pszName) override;
    STDMETHOD(GetNameAndSigOfMethodDef)(
        mdMethodDef methoddef,
        PCCOR_SIGNATURE *ppvSigBlob,
        ULONG *pcbSigBlob,
        LPCSTR *pszName) override;
    STDMETHOD(GetNameOfFieldDef)(mdFieldDef fd, LPCSTR *pszName) override;
    STDMETHOD(GetNameOfTypeRef)(mdTypeRef classref, LPCSTR *psznamespace, LPCSTR *pszname) override;
    STDMETHOD(GetResolutionScopeOfTypeRef)(mdTypeRef classref, mdToken *ptkResolutionScope) override;
    STDMETHOD(FindTypeRefByName)(
        LPCSTR szNamespace,
        LPCSTR szName,
        mdToken tkResolutionScope,
        mdTypeRef *ptk) override;
    STDMETHOD(GetTypeDefProps)(mdTypeDef classdef, DWORD *pdwAttr, mdToken *ptkExtends) override;
    STDMETHOD(GetItemGuid)(mdToken tkObj, CLSID *pGuid) override;
    STDMETHOD(GetNestedClassProps)(mdTypeDef tkNestedClass, mdTypeDef *ptkEnclosingClass) override;
    STDMETHOD(GetCountNestedClasses)(mdTypeDef tkEnclosingClass, ULONG *pcNestedClassesCount) override;
    STDMETHOD(GetNestedClasses)(
        mdTypeDef tkEnclosingClass,
        mdTypeDef *rNestedClasses,
        ULONG ulNestedClasses,
        ULONG *pcNestedClasses) override;
    STDMETHOD(GetModuleRefProps)(mdModuleRef mur, LPCSTR *pszName) override;
    STDMETHOD(GetSigOfMethodDef)(
        mdMethodDef tkMethodDef,
        ULONG * pcbSigBlob,
        PCCOR_SIGNATURE * ppSig) override;
    STDMETHOD(GetSigOfFieldDef)(mdFieldDef tkFieldDef, ULONG * pcbSigBlob, PCCOR_SIGNATURE * ppSig) override;
    STDMETHOD(GetSigFromToken)(mdToken tk, ULONG * pcbSig, PCCOR_SIGNATURE * ppSig) override;
    STDMETHOD(GetMethodDefProps)(mdMethodDef md, DWORD *pdwFlags) override;
    STDMETHOD(GetMethodImplProps)(mdToken tk, ULONG *pulCodeRVA, DWORD *pdwImplFlags) override;
    STDMETHOD(GetFieldRVA)(mdFieldDef fd, ULONG *pulCodeRVA) override;
    STDMETHOD(GetFieldDefProps)(mdFieldDef fd, DWORD *pdwFlags) override;
    STDMETHOD(GetDefaultValue)(mdToken tk, MDDefaultValue *pDefaultValue) override;
    STDMETHOD(GetDispIdOfMemberDef)(mdToken tk, ULONG *pDispid) override;
    STDMETHOD(GetTypeOfInterfaceImpl)(mdInterfaceImpl iiImpl, mdToken *ptkType) override;
    STDMETHOD(FindTypeDef)(
        LPCSTR szNamespace,
        LPCSTR szName,
        mdToken tkEnclosingClass,
        mdTypeDef *ptypedef) override;
    STDMETHOD(GetNameAndSigOfMemberRef)(
        mdMemberRef memberref,
        PCCOR_SIGNATURE *ppvSigBlob,
        ULONG *pcbSigBlob,
        LPCSTR *pszName) override;
    STDMETHOD(GetParentOfMemberRef)(mdMemberRef memberref, mdToken *ptkParent) override;
    STDMETHOD(GetParamDefProps)(
        mdParamDef paramdef,
        USHORT *pusSequence,
        DWORD *pdwAttr,
        LPCSTR *pszName) override;
    STDMETHOD(GetPropertyInfoForMethodDef)(
        mdMethodDef md,
        mdProperty *ppd,
        LPCSTR *pName,
        ULONG *pSemantic) override;
    STDMETHOD(GetClassPackSize)(mdTypeDef td, ULONG *pdwPackSize) override;
    STDMETHOD(GetClassTotalSize)(mdTypeDef td, ULONG *pdwClassSize) override;
    STDMETHOD(GetClassLayoutInit)(mdTypeDef td, MD_CLASS_LAYOUT *pLayout) override;
    STDMETHOD(GetClassLayoutNext)(MD_CLASS_LAYOUT *pLayout, mdFieldDef *pfd, ULONG *pulOffset) override;
    STDMETHOD(GetFieldMarshal)(mdFieldDef fd, PCCOR_SIGNATURE *pSigNativeType, ULONG *pcbNativeType) override;
    STDMETHOD(FindProperty)(mdTypeDef td, LPCSTR szPropName, mdProperty *pProp) override;
    STDMETHOD(GetPropertyProps)(
        mdProperty prop,
        LPCSTR *szProperty,
        DWORD *pdwPropFlags,
        PCCOR_SIGNATURE *ppvSig,
        ULONG *pcbSig) override;
    STDMETHOD(FindEvent)(mdTypeDef td, LPCSTR szEventName, mdEvent *pEvent) override;
    STDMETHOD(GetEventProps)(
        mdEvent ev,
        LPCSTR *pszEvent,
        DWORD *pdwEventFlags,
        mdToken *ptkEventType) override;
    STDMETHOD(FindAssociate)(mdToken evprop, DWORD associate, mdMethodDef *pmd) override;
    STDMETHOD(EnumAssociateInit)(mdToken evprop, HENUMInternal *phEnum) override;
    STDMETHOD(GetAllAssociates)(
        HENUMInternal *phEnum,
        ASSOCIATE_RECORD *pAssociateRec,
        ULONG cAssociateRec) override;
    STDMETHOD(GetPermissionSetProps)(
        mdPermission pm,
        DWORD *pdwAction,
        void const **ppvPermission,
        ULONG *pcbPermission) override;
    STDMETHOD(GetUserString)(mdString stk, ULONG *pchString, LPCWSTR *pwszUserString) override;
    STDMETHOD(GetPinvokeMap)(
        mdToken tk,
        DWORD *pdwMappingFlags,
        LPCSTR *pszImportName,
        mdModuleRef *pmrImportDLL) override;
    STDMETHOD(ConvertTextSigToComSig)(
        BOOL fCreateTrIfNotFound,
        LPCSTR pSignature,
        CQuickBytes *pqbNewSig,
        ULONG *pcbCount) override;
    STDMETHOD(GetAssemblyProps)(
        mdAssembly mda,
        const void **ppbPublicKey,
        ULONG *pcbPublicKey,
        ULONG *pulHashAlgId,
        LPCSTR *pszName,
        AssemblyMetaDataInternal *pMetaData,
        DWORD *pdwAssemblyFlags) override;
    STDMETHOD(GetAssemblyRefProps)(
        mdAssemblyRef mdar,
        const void **ppbPublicKeyOrToken,
        ULONG *pcbPublicKeyOrToken,
        LPCSTR *pszName,
        AssemblyMetaDataInternal *pMetaData,
        const void **ppbHashValue,
        ULONG *pcbHashValue,
        DWORD *pdwAssemblyRefFlags) override;
    STDMETHOD(GetFileProps)(
        mdFile mdf,
        LPCSTR *pszName,
        const void **ppbHashValue,
        ULONG *pcbHashValue,
        DWORD *pdwFileFlags) override;
    STDMETHOD(GetExportedTypeProps)(
        mdExportedType mdct,
        LPCSTR *pszNamespace,
        LPCSTR *pszName,
        mdToken *ptkImplementation,
        mdTypeDef *ptkTypeDef,
        DWORD *pdwExportedTypeFlags) override;
    STDMETHOD(GetManifestResourceProps)(
        mdManifestResource mdmr,
        LPCSTR *pszName,
        mdToken *ptkImplementation,
        DWORD *pdwOffset,
        DWORD *pdwResourceFlags) override;
    STDMETHOD(FindExportedTypeByName)(
        LPCSTR szNamespace,
        LPCSTR szName,
        mdExportedType tkEnclosingType,
        mdExportedType *pmct) override;
    STDMETHOD(FindManifestResourceByName)(LPCSTR szName, mdManifestResource *pmmr) override;
    STDMETHOD(GetAssemblyFromScope)(mdAssembly *ptkAssembly) override;
    STDMETHOD(GetCustomAttributeByName)(
        mdToken tkObj,
        LPCSTR szName,
        const void **ppData,
        ULONG *pcbData) override;
    STDMETHOD(GetTypeSpecFromToken)(mdTypeSpec typespec, PCCOR_SIGNATURE *ppvSig, ULONG *pcbSig) override;
    STDMETHOD(SetUserContextData)(IUnknown *pIUnk) override;
    STDMETHOD_(BOOL, IsValidToken)(mdToken tk) override;
    STDMETHOD(TranslateSigWithScope)(
        IMDInternalImport *pAssemImport,
        const void *pbHashValue,
        ULONG cbHashValue,
        PCCOR_SIGNATURE pbSigBlob,
        ULONG cbSigBlob,
        IMDInternalEmit *pAssemEmit,
        IMDInternalEmit *emit,
        CQuickBytes *pqkSigEmit,
        ULONG *pcbSig) override;
    STDMETHOD_(IMetaModelCommon*, GetMetaModelCommon)() override;
    STDMETHOD_(IUnknown *, GetCachedPublicInterface)(BOOL fWithLock) override;
    STDMETHOD(SetCachedPublicInterface)(IUnknown *pUnk) override;
    STDMETHOD_(minipal_rwlock*, GetReaderWriterLock)() override;
    STDMETHOD(SetReaderWriterLock)(minipal_rwlock * pLock) override;
    STDMETHOD_(mdModule, GetModuleFromScope)() override;
    STDMETHOD(FindMethodDefUsingCompare)(
        mdTypeDef classdef,
        LPCSTR szName,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG cbSigBlob,
        PSIGCOMPARE pSignatureCompare,
        void* pSignatureArgs,
        mdMethodDef *pmd) override;
    STDMETHOD(GetFieldOffset)(mdFieldDef fd, ULONG *pulOffset) override;
    STDMETHOD(GetMethodSpecProps)(
        mdMethodSpec ms,
        mdToken *tkParent,
        PCCOR_SIGNATURE *ppvSigBlob,
        ULONG *pcbSigBlob) override;
    STDMETHOD(GetTableInfoWithIndex)(ULONG index, void **pTable, void **pTableSize) override;
    STDMETHOD(ApplyEditAndContinue)(void *pDeltaMD, ULONG cbDeltaMD, IMDInternalImport **ppv) override;
    STDMETHOD(ApplyEditAndContinue)(MDInternalRW* pDeltaMD) override;
    STDMETHOD(EnumDeltaTokensInit)(HENUMInternal* phEnum) override;
    STDMETHOD(GetGenericParamProps)(
        mdGenericParam rd,
        ULONG* pulSequence,
        DWORD* pdwAttr,
        mdToken *ptOwner,
        DWORD *reserved,
        LPCSTR *szName) override;
    STDMETHOD(GetGenericParamConstraintProps)(
        mdGenericParamConstraint rd,
        mdGenericParam *ptGenericParam,
        mdToken *ptkConstraintType) override;
    STDMETHOD(GetVersionString)(LPCSTR *pVer) override;
    STDMETHOD(GetTypeDefRefTokenInTypeSpec)(mdTypeSpec tkTypeSpec, mdToken *tkEnclosedToken) override;
    STDMETHOD_(DWORD, GetMetadataStreamVersion)() override;
    STDMETHOD(GetNameOfCustomAttribute)(
        mdCustomAttribute mdAttribute,
        LPCSTR *pszNamespace,
        LPCSTR *pszName) override;

    STDMETHOD(ChangeMvid)(REFGUID newMvid) override;
    STDMETHOD(SetMDUpdateMode)(ULONG updateMode, ULONG *pPreviousUpdateMode) override;
    STDMETHOD(SetModuleProps)(LPCWSTR szName) override;
    STDMETHOD(GetSaveSize)(CorSaveSize fSave, DWORD *pdwSaveSize) override;
    STDMETHOD(SaveToMemory)(void *pbData, ULONG cbData) override;
    STDMETHOD(DefineTypeDef)(
        LPCWSTR szTypeDef,
        DWORD dwTypeDefFlags,
        mdToken tkExtends,
        mdToken rtkImplements[],
        mdTypeDef *ptd) override;
    STDMETHOD(DefineNestedType)(
        LPCWSTR szTypeDef,
        DWORD dwTypeDefFlags,
        mdToken tkExtends,
        mdToken rtkImplements[],
        mdTypeDef tdEncloser,
        mdTypeDef *ptd) override;
    STDMETHOD(DefineMethod)(
        mdTypeDef td,
        LPCWSTR szName,
        DWORD dwMethodFlags,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG cbSigBlob,
        ULONG ulCodeRVA,
        DWORD dwImplFlags,
        mdMethodDef *pmd) override;
    STDMETHOD(DefineMethodImpl)(mdTypeDef td, mdToken tkBody, mdToken tkDecl) override;
    STDMETHOD(DefineTypeRefByName)(mdToken tkResolutionScope, LPCWSTR szName, mdTypeRef *ptr) override;
    STDMETHOD(DefineMemberRef)(
        mdToken tkImport,
        LPCWSTR szName,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG cbSigBlob,
        mdMemberRef *pmr) override;
    STDMETHOD(SetClassLayout)(
        mdTypeDef td,
        DWORD dwPackSize,
        COR_FIELD_OFFSET rFieldOffsets[],
        ULONG ulClassSize) override;
    STDMETHOD(GetTokenFromSig)(PCCOR_SIGNATURE pvSig, ULONG cbSig, mdSignature *pmsig) override;
    STDMETHOD(DefineModuleRef)(LPCWSTR szName, mdModuleRef *pmur) override;
    STDMETHOD(GetTokenFromTypeSpec)(PCCOR_SIGNATURE pvSig, ULONG cbSig, mdTypeSpec *ptypespec) override;
    STDMETHOD(DefineUserString)(LPCWSTR szString, ULONG cchString, mdString *pstk) override;
    STDMETHOD(SetMethodProps)(
        mdMethodDef md,
        DWORD dwMethodFlags,
        ULONG ulCodeRVA,
        DWORD dwImplFlags) override;
    STDMETHOD(DefinePinvokeMap)(
        mdToken tk,
        DWORD dwMappingFlags,
        LPCWSTR szImportName,
        mdModuleRef mrImportDLL) override;
    STDMETHOD(DefineCustomAttribute)(
        mdToken tkOwner,
        mdToken tkCtor,
        void const *pCustomAttribute,
        ULONG cbCustomAttribute,
        mdCustomAttribute *pcv) override;
    STDMETHOD(DefineField)(
        mdTypeDef td,
        LPCWSTR szName,
        DWORD dwFieldFlags,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG cbSigBlob,
        DWORD dwCPlusTypeFlag,
        void const *pValue,
        ULONG cchValue,
        mdFieldDef *pmd) override;
    STDMETHOD(DefineProperty)(
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
        mdProperty *pmdProp) override;
    STDMETHOD(DefineParam)(
        mdMethodDef md,
        ULONG ulParamSeq,
        LPCWSTR szName,
        DWORD dwParamFlags,
        DWORD dwCPlusTypeFlag,
        void const *pValue,
        ULONG cchValue,
        mdParamDef *ppd) override;
    STDMETHOD(SetFieldProps)(
        mdFieldDef fd,
        DWORD dwFieldFlags,
        DWORD dwCPlusTypeFlag,
        void const *pValue,
        ULONG cchValue) override;
    STDMETHOD(SetPropertyProps)(
        mdProperty pr,
        DWORD dwPropFlags,
        DWORD dwCPlusTypeFlag,
        void const *pValue,
        ULONG cchValue,
        mdMethodDef mdSetter,
        mdMethodDef mdGetter,
        mdMethodDef rmdOtherMethods[]) override;
    STDMETHOD(SetParamProps)(
        mdParamDef pd,
        LPCWSTR szName,
        DWORD dwParamFlags,
        DWORD dwCPlusTypeFlag,
        void const *pValue,
        ULONG cchValue) override;
    STDMETHOD(SetMethodImplFlags)(mdMethodDef md, DWORD dwImplFlags) override;
    STDMETHOD(SetFieldRVA)(mdFieldDef fd, ULONG ulRVA) override;
    STDMETHOD(DefineMethodSpec)(
        mdToken tkParent,
        PCCOR_SIGNATURE pvSigBlob,
        ULONG cbSigBlob,
        mdMethodSpec *pmi) override;
    STDMETHOD(DefineGenericParam)(
        mdToken tk,
        ULONG ulParamSeq,
        DWORD dwParamFlags,
        LPCWSTR szName,
        DWORD reserved,
        mdToken rtkConstraints[],
        mdGenericParam *pgp) override;
    STDMETHOD(DefineAssembly)(
        const void *pbPublicKey,
        ULONG cbPublicKey,
        ULONG ulHashAlgId,
        LPCWSTR szName,
        const ASSEMBLYMETADATA *pMetaData,
        DWORD dwAssemblyFlags,
        mdAssembly *pma) override;
    STDMETHOD(DefineAssemblyRef)(
        const void *pbPublicKeyOrToken,
        ULONG cbPublicKeyOrToken,
        LPCWSTR szName,
        const ASSEMBLYMETADATA *pMetaData,
        const void *pbHashValue,
        ULONG cbHashValue,
        DWORD dwAssemblyRefFlags,
        mdAssemblyRef *pmdar) override;
    STDMETHOD(DefineMethodSemanticsHelper)(mdToken tkAssociation, DWORD dwFlags, mdMethodDef md) override;
    STDMETHOD(SetFieldLayoutHelper)(mdFieldDef fd, ULONG ulOffset) override;
    STDMETHOD(DefineEventHelper)(
        mdTypeDef td,
        LPCWSTR szEvent,
        DWORD dwEventFlags,
        mdToken tkEventType,
        mdEvent *pmdEvent) override;
    STDMETHOD(SetTypeParent)(mdTypeDef td, mdToken tkExtends) override;
    STDMETHOD(AddInterfaceImpl)(mdTypeDef td, mdToken tkInterface) override;
};

#endif // _SRC_INTERFACES_INTERNAL_METADATARW_HPP_
