#include "emit.hpp"
#include <array>
#include <gmock/gmock.h>

TEST(MethodDef, Define)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdMethodDef methodDef;

    // TypeDef,1 is the <Module> type.
    std::array<uint8_t, 3> sig = { (uint8_t)IMAGE_CEE_CS_CALLCONV_DEFAULT, (uint8_t)0, (uint8_t)ELEMENT_TYPE_VOID };
    ULONG rva = 0x424242;
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Foo"), mdStatic, sig.data(), (ULONG)sig.size(), rva, 0, &methodDef));
    ASSERT_EQ(1, RidFromToken(methodDef));
    ASSERT_EQ(mdtMethodDef, TypeFromToken(methodDef));
    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    
    mdTypeDef type;
    WSTR_string readName;
    readName.resize(3);
    ULONG readNameLength;

    DWORD attr;
    PCCOR_SIGNATURE sigBlob;
    ULONG sigBlobLength;
    ULONG codeRVA;
    DWORD implFlags;
    ASSERT_EQ(S_OK, import->GetMethodProps(methodDef, &type, &readName[0], (ULONG)readName.capacity(), &readNameLength, &attr, &sigBlob, &sigBlobLength, &codeRVA, &implFlags));
    EXPECT_EQ(W("Foo"), readName.substr(0, readNameLength - 1));
    EXPECT_EQ(mdStatic, attr);
    EXPECT_EQ(rva, codeRVA);
    EXPECT_EQ(0, implFlags);
    EXPECT_THAT(std::vector<uint8_t>(sigBlob, sigBlob + sigBlobLength), testing::ContainerEq(std::vector<uint8_t>(sig.begin(), sig.end())));
}

TEST(MethodDef, RuntimeSpecialNames)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdTypeDef type;
    ASSERT_EQ(S_OK, emit->DefineTypeDef(W("Example"), tdPublic, mdTypeDefNil, nullptr, &type));

    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    struct MethodCase
    {
        LPCWSTR name;
        DWORD attributes;
        uint8_t callingConvention;
    };
    constexpr MethodCase cases[] =
    {
        { W(".ctor"), mdPublic, IMAGE_CEE_CS_CALLCONV_DEFAULT_HASTHIS },
        { W(".cctor"), mdPrivate | mdStatic, IMAGE_CEE_CS_CALLCONV_DEFAULT },
        { W("_VtblGap1_1"), mdPublic | mdVirtual | mdAbstract, IMAGE_CEE_CS_CALLCONV_DEFAULT_HASTHIS },
    };

    for (MethodCase testCase : cases)
    {
        std::array<uint8_t, 3> sig = { testCase.callingConvention, 0, ELEMENT_TYPE_VOID };
        mdMethodDef method;
        ASSERT_EQ(S_OK, emit->DefineMethod(type, testCase.name, testCase.attributes,
            sig.data(), (ULONG)sig.size(), 0, 0, &method));

        WSTR_string readName(32, 0);
        ULONG nameLength, sigLength, rva;
        DWORD flags, implFlags;
        mdTypeDef owner;
        PCCOR_SIGNATURE sigBlob;
        ASSERT_EQ(S_OK, import->GetMethodProps(method, &owner, &readName[0], (ULONG)readName.size(),
            &nameLength, &flags, &sigBlob, &sigLength, &rva, &implFlags));
        EXPECT_EQ(testCase.attributes | mdSpecialName | mdRTSpecialName, flags);

        ASSERT_EQ(S_OK, emit->SetMethodProps(method, testCase.attributes | mdSpecialName, 0, 0));
        ASSERT_EQ(S_OK, import->GetMethodProps(method, &owner, &readName[0], (ULONG)readName.size(),
            &nameLength, &flags, &sigBlob, &sigLength, &rva, &implFlags));
        EXPECT_EQ(testCase.attributes | mdSpecialName | mdRTSpecialName, flags);
    }
}

TEST(MethodDef, GlobalParent)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));

    struct GlobalMethod
    {
        mdTypeDef parent;
        LPCWSTR name;
    };
    constexpr GlobalMethod methods[] =
    {
        { mdTypeDefNil, W("First") },
        { mdTokenNil, W("Second") },
    };
    std::array<uint8_t, 3> sig = { IMAGE_CEE_CS_CALLCONV_DEFAULT, 0, ELEMENT_TYPE_VOID };
    for (GlobalMethod global : methods)
    {
        mdMethodDef method;
        ASSERT_EQ(S_OK, emit->DefineMethod(global.parent, global.name, mdPublic | mdStatic,
            sig.data(), (ULONG)sig.size(), 0, 0, &method));

        mdTypeDef owner;
        WSTR_string readName(16, 0);
        ULONG nameLength, sigLength, rva;
        DWORD flags, implFlags;
        PCCOR_SIGNATURE sigBlob;
        ASSERT_EQ(S_OK, import->GetMethodProps(method, &owner, &readName[0], (ULONG)readName.size(),
            &nameLength, &flags, &sigBlob, &sigLength, &rva, &implFlags));
        EXPECT_EQ(TokenFromRid(1, mdtTypeDef), owner);
        EXPECT_EQ(mdPublic | mdStatic, flags);
    }
}

TEST(MethodDef, DefineWithInvalidType)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdMethodDef methodDef;

    std::array<uint8_t, 3> sig = { (uint8_t)IMAGE_CEE_CS_CALLCONV_DEFAULT, (uint8_t)0, (uint8_t)ELEMENT_TYPE_VOID };
    ULONG rva = 0x424242;
    ASSERT_EQ(CLDB_E_FILE_CORRUPT, emit->DefineMethod(TokenFromRid(2, mdtTypeDef), W("Foo"), mdStatic, sig.data(), (ULONG)sig.size(), rva, 0, &methodDef));
}

TEST(MethodDef, SetRva)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdMethodDef methodDef;

    std::array<uint8_t, 3> sig = { (uint8_t)IMAGE_CEE_CS_CALLCONV_DEFAULT, (uint8_t)0, (uint8_t)ELEMENT_TYPE_VOID };
    ULONG rva = 0x424242;
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Foo"), mdStatic, sig.data(), (ULONG)sig.size(), rva, 0, &methodDef));
    ASSERT_EQ(1, RidFromToken(methodDef));
    ASSERT_EQ(mdtMethodDef, TypeFromToken(methodDef));
    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    
    ULONG newRva = 0x123456;
    ASSERT_EQ(S_OK, emit->SetRVA(methodDef, newRva));
    
    mdTypeDef type;
    WSTR_string readName;
    readName.resize(3);
    ULONG readNameLength;

    DWORD attr;
    PCCOR_SIGNATURE sigBlob;
    ULONG sigBlobLength;
    ULONG codeRVA;
    DWORD implFlags;
    ASSERT_EQ(S_OK, import->GetMethodProps(methodDef, &type, &readName[0], (ULONG)readName.capacity(), &readNameLength, &attr, &sigBlob, &sigBlobLength, &codeRVA, &implFlags));
    EXPECT_EQ(W("Foo"), readName.substr(0, readNameLength - 1));
    EXPECT_EQ(mdStatic, attr);
    EXPECT_EQ(newRva, codeRVA);
    EXPECT_EQ(0, implFlags);
    EXPECT_THAT(std::vector<uint8_t>(sigBlob, sigBlob + sigBlobLength), testing::ContainerEq(std::vector<uint8_t>(sig.begin(), sig.end())));
}

TEST(MethodDef, SetProps)
{
    minipal::com_ptr<IMetaDataEmit> emit;
    ASSERT_NO_FATAL_FAILURE(CreateEmit(emit));
    mdMethodDef methodDef;

    std::array<uint8_t, 3> sig = { (uint8_t)IMAGE_CEE_CS_CALLCONV_DEFAULT, (uint8_t)0, (uint8_t)ELEMENT_TYPE_VOID };
    ULONG rva = 0x424242;
    ASSERT_EQ(S_OK, emit->DefineMethod(TokenFromRid(1, mdtTypeDef), W("Foo"), mdStatic, sig.data(), (ULONG)sig.size(), rva, 0, &methodDef));
    ASSERT_EQ(1, RidFromToken(methodDef));
    ASSERT_EQ(mdtMethodDef, TypeFromToken(methodDef));
    minipal::com_ptr<IMetaDataImport> import;
    ASSERT_EQ(S_OK, emit->QueryInterface(IID_IMetaDataImport, (void**)&import));
    
    ULONG newRva = 0x123456;
    ASSERT_EQ(S_OK, emit->SetMethodProps(methodDef, mdPublic, newRva, miForwardRef));
    
    mdTypeDef type;
    WSTR_string readName;
    readName.resize(3);
    ULONG readNameLength;

    DWORD attr;
    PCCOR_SIGNATURE sigBlob;
    ULONG sigBlobLength;
    ULONG codeRVA;
    DWORD implFlags;
    ASSERT_EQ(S_OK, import->GetMethodProps(methodDef, &type, &readName[0], (ULONG)readName.capacity(), &readNameLength, &attr, &sigBlob, &sigBlobLength, &codeRVA, &implFlags));
    EXPECT_EQ(W("Foo"), readName.substr(0, readNameLength - 1));
    EXPECT_EQ(mdPublic, attr);
    EXPECT_EQ(newRva, codeRVA);
    EXPECT_EQ(miForwardRef, implFlags);
    EXPECT_THAT(std::vector<uint8_t>(sigBlob, sigBlob + sigBlobLength), testing::ContainerEq(std::vector<uint8_t>(sig.begin(), sig.end())));
}
