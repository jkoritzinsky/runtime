#include "emit.hpp"
#include <metadata.h>

static void TestTypeHierarchyHelper(bool threadSafe)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    if (threadSafe)
    {
        ASSERT_NO_FATAL_FAILURE(CreateThreadSafeEmit(emit));
    }
    else
    {
        ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    }

    minipal::com_ptr<IMDInternalEmit> internal;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMDInternalEmit, (void**)&internal));

    mdToken noInterfaces = mdTokenNil;
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Derived"), tdPublic, mdTypeDefNil, &noInterfaces, &type));

    mdTypeRef base, firstInterface, secondInterface;
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(mdTokenNil, W("Base"), &base));
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(mdTokenNil, W("IFirst"), &firstInterface));
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(mdTokenNil, W("ISecond"), &secondInterface));
    ASSERT_EQ(S_OK, internal->SetTypeParent(type, base));
    ASSERT_EQ(S_OK, internal->AddInterfaceImpl(type, firstInterface));
    ASSERT_EQ(S_OK, internal->AddInterfaceImpl(type, firstInterface));
    ASSERT_EQ(S_OK, internal->AddInterfaceImpl(type, secondInterface));

    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    WCHAR name[32];
    ULONG nameLength;
    DWORD flags;
    mdToken extends;
    ASSERT_EQ(S_OK, import->GetTypeDefProps(type, name, 32, &nameLength, &flags, &extends));
    EXPECT_EQ(base, extends);
    EXPECT_EQ(tdPublic, flags);

    HCORENUM hEnum = nullptr;
    mdInterfaceImpl implementations[3] = {};
    ULONG count;
    ASSERT_EQ(S_OK, import->EnumInterfaceImpls(&hEnum, type, implementations, 3, &count));
    ASSERT_EQ(2u, count);
    for (ULONG i = 0; i < count; ++i)
    {
        mdTypeDef owner;
        mdToken interfaceToken;
        ASSERT_EQ(S_OK, import->GetInterfaceImplProps(implementations[i], &owner, &interfaceToken));
        EXPECT_EQ(type, owner);
        EXPECT_EQ(i == 0 ? firstInterface : secondInterface, interfaceToken);
    }
    import->CloseEnum(hEnum);
}

TEST(InternalEmit, TypeHierarchy)
{
    TestTypeHierarchyHelper(false);
}

TEST(InternalEmit, ThreadSafeTypeHierarchy)
{
    TestTypeHierarchyHelper(true);
}

TEST(InternalEmit, EventAndMethodSemantics)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    minipal::com_ptr<IMDInternalEmit> internal;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMDInternalEmit, (void**)&internal));

    mdToken noInterfaces = mdTokenNil;
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Publisher"), tdPublic, mdTypeDefNil, &noInterfaces, &type));
    mdTypeRef eventType;
    ASSERT_EQ(S_OK, emit->DefineTypeRefByName(mdTokenNil, W("Handler"), &eventType));

    mdEvent eventToken;
    ASSERT_EQ(S_OK, internal->DefineEventHelper(type, W("Changed"), 0, eventType, &eventToken));
    EXPECT_EQ(mdtEvent, TypeFromToken(eventToken));

    BYTE signature[] = { IMAGE_CEE_CS_CALLCONV_DEFAULT, 0, ELEMENT_TYPE_VOID };
    mdMethodDef addMethod, removeMethod;
    ASSERT_EQ(S_OK, emit->DefineMethod(type, W("add_Changed"), mdPublic, signature, sizeof(signature), 0, 0, &addMethod));
    ASSERT_EQ(S_OK, emit->DefineMethod(type, W("remove_Changed"), mdPublic, signature, sizeof(signature), 0, 0, &removeMethod));
    EXPECT_EQ(E_INVALIDARG, internal->DefineMethodSemanticsHelper(eventToken, msAddOn, mdMethodDefNil));
    ASSERT_EQ(S_OK, internal->DefineMethodSemanticsHelper(eventToken, msAddOn, addMethod));
    ASSERT_EQ(S_OK, internal->DefineMethodSemanticsHelper(eventToken, msRemoveOn, removeMethod));

    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    DWORD semantics;
    ASSERT_EQ(S_OK, import->GetMethodSemantics(addMethod, eventToken, &semantics));
    EXPECT_EQ(msAddOn, semantics);
    ASSERT_EQ(S_OK, import->GetMethodSemantics(removeMethod, eventToken, &semantics));
    EXPECT_EQ(msRemoveOn, semantics);
}

TEST(InternalEmit, FieldLayout)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    minipal::com_ptr<IMDInternalEmit> internal;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMDInternalEmit, (void**)&internal));

    mdToken noInterfaces = mdTokenNil;
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Layout"), tdPublic | tdExplicitLayout, mdTypeDefNil, &noInterfaces, &type));
    ASSERT_EQ(S_OK, emit->SetClassLayout(type, 4, nullptr, 16));

    BYTE signature[] = { IMAGE_CEE_CS_CALLCONV_FIELD, ELEMENT_TYPE_I4 };
    mdFieldDef field;
    ASSERT_EQ(S_OK, emit->DefineField(type, W("Value"), fdPublic, signature, sizeof(signature), 0, nullptr, 0, &field));
    EXPECT_EQ(E_INVALIDARG, internal->SetFieldLayoutHelper(field, UINT32_MAX));
    ASSERT_EQ(S_OK, internal->SetFieldLayoutHelper(field, 8));

    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    COR_FIELD_OFFSET offsets[2] = {};
    DWORD packing;
    ULONG count, classSize;
    ASSERT_EQ(S_OK, import->GetClassLayout(type, &packing, offsets, 2, &count, &classSize));
    EXPECT_EQ(4u, packing);
    EXPECT_EQ(16u, classSize);
    ASSERT_EQ(1u, count);
    EXPECT_EQ(field, offsets[0].ridOfField);
    EXPECT_EQ(8u, offsets[0].ulOffset);
}
