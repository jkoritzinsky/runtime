// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

//
// This class contains all the data & functionality for code generation
// of a method, except for the target-specific elements, which are
// primarily in the Target class.
//

#ifndef _CODEGEN_H_
#define _CODEGEN_H_
#include "codegeninterface.h"
#include "compiler.h" // temporary??
#include "regset.h"
#include "jitgcinfo.h"

class CodeGen final : public CodeGenInterface
{
    friend class emitter;
    friend class DisAssembler;

public:
    // This could use further abstraction
    CodeGen(Compiler* theCompiler);

    virtual void genGenerateCode(void** codePtr, uint32_t* nativeSizeOfCode);

    void genGenerateMachineCode();
    void genEmitMachineCode();
    void genEmitUnwindDebugGCandEH();

    // TODO-Cleanup: Abstract out the part of this that finds the addressing mode, and
    // move it to Lower
    virtual bool genCreateAddrMode(GenTree*  addr,
                                   bool      fold,
                                   unsigned  naturalMul,
                                   bool*     revPtr,
                                   GenTree** rv1Ptr,
                                   GenTree** rv2Ptr,
                                   unsigned* mulPtr,
                                   ssize_t*  cnsPtr);

#ifdef LATE_DISASM
    virtual const char* siStackVarName(size_t offs, size_t size, unsigned reg, unsigned stkOffs);
    virtual const char* siRegVarName(size_t offs, size_t size, unsigned reg);
#endif // LATE_DISASM

private:
#if defined(TARGET_XARCH)
    // Generates SSE2 code for the given tree as "Operand BitWiseOp BitMask"
    void genSSE2BitwiseOp(GenTree* treeNode);

    // Generates SSE42 code for the given tree as a round operation
    void genSSE42RoundOp(GenTreeOp* treeNode);

    instruction simdAlignedMovIns()
    {
        // We use movaps when non-VEX because it is a smaller instruction;
        // however the VEX version vmovaps would be used which is the same size as vmovdqa;
        // also vmovdqa has more available CPU ports on older processors so we switch to that
        return compiler->canUseVexEncoding() ? INS_movdqa32 : INS_movaps;
    }
    instruction simdUnalignedMovIns()
    {
        // We use movups when non-VEX because it is a smaller instruction;
        // however the VEX version vmovups would be used which is the same size as vmovdqu;
        // but vmovdqu has more available CPU ports on older processors so we switch to that
        return compiler->canUseVexEncoding() ? INS_movdqu32 : INS_movups;
    }
#endif // defined(TARGET_XARCH)

    void genPrepForCompiler();

    void genMarkLabelsForCodegen();

    regNumber genFramePointerReg()
    {
        if (isFramePointerUsed())
        {
            return REG_FPBASE;
        }
        else
        {
            return REG_SPBASE;
        }
    }

#if defined(TARGET_ARM64)
    regNumber getNextSIMDRegWithWraparound(regNumber reg)
    {
        regNumber nextReg = REG_NEXT(reg);

        // Wraparound if necessary, REG_V0 comes next after REG_V31.
        return (nextReg > REG_V31) ? REG_V0 : nextReg;
    }
#endif // defined(TARGET_ARM64)

    static GenTreeIndir    indirForm(var_types type, GenTree* base);
    static GenTreeStoreInd storeIndirForm(var_types type, GenTree* base, GenTree* data);

    GenTreeIntCon intForm(var_types type, ssize_t value);

    void genRangeCheck(GenTree* node);

    void genLockedInstructions(GenTreeOp* node);
#ifdef TARGET_XARCH
    void genCodeForLockAdd(GenTreeOp* node);
#endif

#ifdef REG_OPT_RSVD
    // On some targets such as the ARM we may need to have an extra reserved register
    //  that is used when addressing stack based locals and stack based temps.
    //  This method returns the regNumber that should be used when an extra register
    //  is needed to access the stack based locals and stack based temps.
    //
    regNumber rsGetRsvdReg()
    {
        // We should have already added this register to the mask
        //  of reserved registers in regSet.rdMaskResvd
        noway_assert((regSet.rsMaskResvd & RBM_OPT_RSVD) != 0);

        return REG_OPT_RSVD;
    }
#endif // REG_OPT_RSVD

    //-------------------------------------------------------------------------

    bool     genUseBlockInit;  // true if we plan to block-initialize the local stack frame
    unsigned genInitStkLclCnt; // The count of local variables that we need to zero init

    void SubtractStackLevel(unsigned adjustment)
    {
        assert(genStackLevel >= adjustment);
        unsigned newStackLevel = genStackLevel - adjustment;
        if (genStackLevel != newStackLevel)
        {
            JITDUMP("Adjusting stack level from %d to %d\n", genStackLevel, newStackLevel);
        }
        genStackLevel = newStackLevel;
    }

    void AddStackLevel(unsigned adjustment)
    {
        unsigned newStackLevel = genStackLevel + adjustment;
        if (genStackLevel != newStackLevel)
        {
            JITDUMP("Adjusting stack level from %d to %d\n", genStackLevel, newStackLevel);
        }
        genStackLevel = newStackLevel;
    }

    void SetStackLevel(unsigned newStackLevel)
    {
        if (genStackLevel != newStackLevel)
        {
            JITDUMP("Setting stack level from %d to %d\n", genStackLevel, newStackLevel);
        }
        genStackLevel = newStackLevel;
    }

    //-------------------------------------------------------------------------

    void genReportEH();

    // Allocates storage for the GC info, writes the GC info into that storage, records the address of the
    // GC info of the method with the EE, and returns a pointer to the "info" portion (just post-header) of
    // the GC info.  Requires "codeSize" to be the size of the generated code, "prologSize" and "epilogSize"
    // to be the sizes of the prolog and epilog, respectively.  In DEBUG, makes a check involving the
    // "codePtr", assumed to be a pointer to the start of the generated code.

#ifdef JIT32_GCENCODER
    void* genCreateAndStoreGCInfo(unsigned codeSize, unsigned prologSize, unsigned epilogSize DEBUGARG(void* codePtr));
    void* genCreateAndStoreGCInfoJIT32(unsigned            codeSize,
                                       unsigned            prologSize,
                                       unsigned epilogSize DEBUGARG(void* codePtr));
#else  // !JIT32_GCENCODER
    void genCreateAndStoreGCInfo(unsigned codeSize, unsigned prologSize, unsigned epilogSize DEBUGARG(void* codePtr));
    void genCreateAndStoreGCInfoX64(unsigned codeSize, unsigned prologSize DEBUGARG(void* codePtr));
#endif // !JIT32_GCENCODER

    /**************************************************************************
     *                          PROTECTED
     *************************************************************************/

protected:
    // the current (pending) label ref, a label which has been referenced but not yet seen
    BasicBlock* genPendingCallLabel;

    void**    codePtr;
    void*     codePtrRW;
    uint32_t* nativeSizeOfCode;
    unsigned  codeSize;
    void*     coldCodePtr;
    void*     coldCodePtrRW;
    void*     consPtr;
    void*     consPtrRW;

    // Last instr we have displayed for dspInstrs
    unsigned genCurDispOffset;

    static const char* genInsName(instruction ins);
    const char*        genInsDisplayName(emitter::instrDesc* id);

    static const char* genSizeStr(emitAttr size);

    void genInitialize();

    void genInitializeRegisterState();

    void genCodeForBBlist();

public:
    void genSpillVar(GenTree* tree);

    void genEmitCallWithCurrentGC(EmitCallParams& callParams);

protected:
    void genEmitHelperCall(unsigned helper, int argSize, emitAttr retSize, regNumber callTarget = REG_NA);

    void genGCWriteBarrier(GenTreeStoreInd* store, GCInfo::WriteBarrierForm wbf);

    BasicBlock* genCreateTempLabel();

private:
    void genLogLabel(BasicBlock* bb);

protected:
    void genDefineTempLabel(BasicBlock* label);
    void genDefineInlineTempLabel(BasicBlock* label);

    void genAdjustStackLevel(BasicBlock* block);

    void genExitCode(BasicBlock* block);

#if defined(TARGET_ARM64)
    BasicBlock* genGetThrowHelper(SpecialCodeKind codeKind);

    // genEmitInlineThrow: Generate code for an inline exception.
    void genEmitInlineThrow(SpecialCodeKind codeKind)
    {
        genEmitHelperCall(compiler->acdHelper(codeKind), 0, EA_UNKNOWN);
    }

    // throwCodeFn callback follows concept -> void(*)(BasicBlock* target, bool isInline)
    //
    // For conditional jumps:
    //     If `isInline`, invert the condition for throw and fall into the exception block.
    //     Otherwise emit compare and jump with the normal throw condition.
    // For unconditional jumps:
    //     Only emit the unconditional jump when `isInline == false`.
    //     When `isInline == true` the code will fallthrough to throw without any jump added.
    //
    // Parameter `target` gives a label to jump to, which is the throw block if
    // `isInline == false`, else the continuation.
    template <typename throwCodeFn>
    void genJumpToThrowHlpBlk(SpecialCodeKind codeKind, throwCodeFn emitJumpCode, BasicBlock* throwBlock = nullptr)
    {
        if (!throwBlock)
        {
            // If caller didn't supply a target block, then try to find a helper block.
            throwBlock = genGetThrowHelper(codeKind);
        }

        if (throwBlock)
        {
            // check:
            //   if (checkPassed)
            //     goto throw;
            //   ...
            // throw:
            //   throw();
            emitJumpCode(throwBlock, false);
        }
        else
        {
            // check:
            //   if (!checkPassed)
            //     goto continue;
            //   throw();
            // continue:
            //   ...
            BasicBlock* over = genCreateTempLabel();
            emitJumpCode(over, true);
            genEmitInlineThrow(codeKind);
            genDefineTempLabel(over);
        }
    }
#endif

    void genJumpToThrowHlpBlk(emitJumpKind jumpKind, SpecialCodeKind codeKind, BasicBlock* failBlk = nullptr);

#if defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    void genJumpToThrowHlpBlk_la(SpecialCodeKind codeKind,
                                 instruction     ins,
                                 regNumber       reg1,
                                 BasicBlock*     failBlk = nullptr,
                                 regNumber       reg2    = REG_R0);
#else
    void genCheckOverflow(GenTree* tree);
#endif

    //-------------------------------------------------------------------------
    //
    // Prolog/epilog generation
    //
    //-------------------------------------------------------------------------

    unsigned prologSize;
    unsigned epilogSize;

    //
    // Prolog functions and data (there are a few exceptions for more generally used things)
    //

    void      genEstablishFramePointer(int delta, bool reportUnwindData);
    void      genHomeRegisterParams(regNumber initReg, bool* initRegStillZeroed);
    regMaskTP genGetParameterHomingTempRegisterCandidates();

    var_types genParamStackType(LclVarDsc* dsc, const ABIPassingSegment& seg);
    void      genSpillOrAddRegisterParam(
             unsigned lclNum, unsigned offset, unsigned paramLclNum, const ABIPassingSegment& seg, class RegGraph* graph);
    void genSpillOrAddNonStandardRegisterParam(unsigned lclNum, regNumber sourceReg, class RegGraph* graph);
    void genEnregisterIncomingStackArgs();
#if defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    void genEnregisterOSRArgsAndLocals(regNumber initReg, bool* pInitRegZeroed);
#else
    void genEnregisterOSRArgsAndLocals();
#endif

    void genHomeStackSegment(unsigned lclNum, const ABIPassingSegment& seg, regNumber initReg, bool* pInitRegZeroed);
    void genHomeSwiftStructStackParameters();
    void genHomeStackPartOfSplitParameter(regNumber initReg, bool* initRegStillZeroed);

    void genCheckUseBlockInit();
#if defined(UNIX_AMD64_ABI) && defined(FEATURE_SIMD)
    void genClearStackVec3ArgUpperBits();
#endif // UNIX_AMD64_ABI && FEATURE_SIMD

#if defined(TARGET_ARM64)
    bool genInstrWithConstant(instruction ins,
                              emitAttr    attr,
                              regNumber   reg1,
                              regNumber   reg2,
                              ssize_t     imm,
                              regNumber   tmpReg,
                              bool        inUnwindRegion = false);

    void genStackPointerAdjustment(ssize_t spAdjustment, regNumber tmpReg, bool* pTmpRegIsZero, bool reportUnwindData);

    void genPrologSaveRegPair(regNumber reg1,
                              regNumber reg2,
                              int       spOffset,
                              int       spDelta,
                              bool      useSaveNextPair,
                              regNumber tmpReg,
                              bool*     pTmpRegIsZero);

    void genPrologSaveReg(regNumber reg1, int spOffset, int spDelta, regNumber tmpReg, bool* pTmpRegIsZero);

    void genEpilogRestoreRegPair(regNumber reg1,
                                 regNumber reg2,
                                 int       spOffset,
                                 int       spDelta,
                                 bool      useSaveNextPair,
                                 regNumber tmpReg,
                                 bool*     pTmpRegIsZero);

    void genEpilogRestoreReg(regNumber reg1, int spOffset, int spDelta, regNumber tmpReg, bool* pTmpRegIsZero);

    // A simple struct to keep register pairs for prolog and epilog.
    struct RegPair
    {
        regNumber reg1;
        regNumber reg2;
        bool      useSaveNextPair;

        RegPair(regNumber reg1)
            : reg1(reg1)
            , reg2(REG_NA)
            , useSaveNextPair(false)
        {
        }

        RegPair(regNumber reg1, regNumber reg2)
            : reg1(reg1)
            , reg2(reg2)
            , useSaveNextPair(false)
        {
            assert(reg2 == REG_NEXT(reg1));
        }
    };

    static void genBuildRegPairsStack(regMaskTP regsMask, ArrayStack<RegPair>* regStack);
    static void genSetUseSaveNextPairs(ArrayStack<RegPair>* regStack);

    static int genGetSlotSizeForRegsInMask(regMaskTP regsMask);

    void genSaveCalleeSavedRegisterGroup(regMaskTP regsMask, int spDelta, int spOffset);
    void genRestoreCalleeSavedRegisterGroup(regMaskTP regsMask, int spDelta, int spOffset);

    void genSaveCalleeSavedRegistersHelp(regMaskTP regsToSaveMask, int lowestCalleeSavedOffset, int spDelta);
    void genRestoreCalleeSavedRegistersHelp(regMaskTP regsToRestoreMask, int lowestCalleeSavedOffset, int spDelta);

    void genPushCalleeSavedRegisters(regNumber initReg, bool* pInitRegZeroed);

#elif defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    bool genInstrWithConstant(instruction ins,
                              emitAttr    attr,
                              regNumber   reg1,
                              regNumber   reg2,
                              ssize_t     imm,
                              regNumber   tmpReg,
                              bool        inUnwindRegion = false);

    void genStackPointerAdjustment(ssize_t spAdjustment, regNumber tmpReg, bool* pTmpRegIsZero, bool reportUnwindData);

    void genSaveCalleeSavedRegistersHelp(regMaskTP regsToSaveMask, int lowestCalleeSavedOffset);
    void genRestoreCalleeSavedRegistersHelp(regMaskTP regsToRestoreMask, int lowestCalleeSavedOffset);
    void genPushCalleeSavedRegisters(regNumber initReg, bool* pInitRegZeroed);

#else
    void genPushCalleeSavedRegisters();
#endif

#if defined(TARGET_AMD64)
    void genOSRRecordTier0CalleeSavedRegistersAndFrame();
    void genOSRSaveRemainingCalleeSavedRegisters();
#endif // TARGET_AMD64

    void genAllocLclFrame(unsigned frameSize, regNumber initReg, bool* pInitRegZeroed, regMaskTP maskArgRegsLiveIn);

    void genPoisonFrame(regMaskTP bbRegLiveIn);

#if defined(TARGET_ARM)

    bool genInstrWithConstant(
        instruction ins, emitAttr attr, regNumber reg1, regNumber reg2, ssize_t imm, insFlags flags, regNumber tmpReg);

    bool genStackPointerAdjustment(ssize_t spAdjustment, regNumber tmpReg);

    void      genPushFltRegs(regMaskTP regMask);
    void      genPopFltRegs(regMaskTP regMask);
    regMaskTP genStackAllocRegisterMask(unsigned frameSize, regMaskTP maskCalleeSavedFloat);

    regMaskTP genPrespilledUnmappedRegs();

    regMaskTP genJmpCallArgMask();

    void genFreeLclFrame(unsigned           frameSize,
                         /* IN OUT */ bool* pUnwindStarted);

    void genMov32RelocatableDisplacement(BasicBlock* block, regNumber reg);
    void genMov32RelocatableDataLabel(unsigned value, regNumber reg);
    void genMov32RelocatableImmediate(emitAttr size, BYTE* addr, regNumber reg);

    bool genUsedPopToReturn; // True if we use the pop into PC to return,
                             // False if we didn't and must branch to LR to return.

    // A set of information that is used by funclet prolog and epilog generation. It is collected once, before
    // funclet prologs and epilogs are generated, and used by all funclet prologs and epilogs, which must all be the
    // same.
    struct FuncletFrameInfoDsc
    {
        regMaskTP fiSaveRegs; // Set of registers saved in the funclet prolog (includes LR)
        unsigned  fiSpDelta;  // Stack pointer delta
    };

    FuncletFrameInfoDsc genFuncletInfo;

#elif defined(TARGET_ARM64)

    // A set of information that is used by funclet prolog and epilog generation. It is collected once, before
    // funclet prologs and epilogs are generated, and used by all funclet prologs and epilogs, which must all be the
    // same.
    struct FuncletFrameInfoDsc
    {
        regMaskTP fiSaveRegs;               // Set of callee-saved registers saved in the funclet prolog (includes LR)
        int       fiSP_to_FPLR_save_delta;  // FP/LR register save offset from SP (positive)
        int       fiSP_to_CalleeSave_delta; // First callee-saved register slot offset from SP (positive)
        int       fiFrameType;              // Funclet frame types are numbered. See genFuncletProlog() for details.
        int       fiSpDelta1;               // Stack pointer delta 1 (negative)
        int       fiSpDelta2;               // Stack pointer delta 2 (negative)
    };

    FuncletFrameInfoDsc genFuncletInfo;

#elif defined(TARGET_AMD64)

    // A set of information that is used by funclet prolog and epilog generation. It is collected once, before
    // funclet prologs and epilogs are generated, and used by all funclet prologs and epilogs, which must all be the
    // same.
    struct FuncletFrameInfoDsc
    {
        unsigned fiSpDelta; // Stack pointer delta
    };

    FuncletFrameInfoDsc genFuncletInfo;

#elif defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)

    // A set of information that is used by funclet prolog and epilog generation.
    // It is collected once, before funclet prologs and epilogs are generated,
    // and used by all funclet prologs and epilogs, which must all be the same.
    struct FuncletFrameInfoDsc
    {
        regMaskTP fiSaveRegs;                // Set of callee-saved registers saved in the funclet prolog (includes RA)
        int       fiSP_to_CalleeSaved_delta; // CalleeSaved register save offset from SP (positive)
        int       fiSpDelta;                 // Stack pointer delta (negative)
    };

    FuncletFrameInfoDsc genFuncletInfo;

#endif // TARGET_ARM, TARGET_ARM64, TARGET_AMD64, TARGET_LOONGARCH64, TARGET_RISCV64

#if defined(TARGET_XARCH)

    // Save/Restore callee saved float regs to stack
    void genPreserveCalleeSavedFltRegs();
    void genRestoreCalleeSavedFltRegs();

    // Generate vzeroupper instruction to clear AVX state if necessary
    void genClearAvxStateInProlog();
    void genClearAvxStateInEpilog();

#endif // TARGET_XARCH

    void genZeroInitFltRegs(const regMaskTP& initFltRegs, const regMaskTP& initDblRegs, const regNumber& initReg);

    regNumber genGetZeroReg(regNumber initReg, bool* pInitRegZeroed);

    void genZeroInitFrame(int untrLclHi, int untrLclLo, regNumber initReg, bool* pInitRegZeroed);
    void genZeroInitFrameUsingBlockInit(int untrLclHi, int untrLclLo, regNumber initReg, bool* pInitRegZeroed);

    void genReportGenericContextArg(regNumber initReg, bool* pInitRegZeroed);

    void genSetGSSecurityCookie(regNumber initReg, bool* pInitRegZeroed);

    void genFinalizeFrame();

#ifdef PROFILING_SUPPORTED
    void genProfilingEnterCallback(regNumber initReg, bool* pInitRegZeroed);
    void genProfilingLeaveCallback(unsigned helper);
#endif // PROFILING_SUPPORTED

    //
    // Epilog functions
    //

#if defined(TARGET_ARM)
    bool genCanUsePopToReturn(regMaskTP maskPopRegsInt, bool jmpEpilog);
#endif

#if defined(TARGET_ARM64)

    void genPopCalleeSavedRegistersAndFreeLclFrame(bool jmpEpilog);

#else // !defined(TARGET_ARM64)

    void genPopCalleeSavedRegisters(bool jmpEpilog = false);

#if defined(TARGET_XARCH)
    unsigned genPopCalleeSavedRegistersFromMask(regMaskTP rsPopRegs);
#ifdef TARGET_AMD64
    void     genPushCalleeSavedRegistersFromMaskAPX(regMaskTP rsPushRegs);
    unsigned genPopCalleeSavedRegistersFromMaskAPX(regMaskTP rsPopRegs);
#endif // TARGET_AMD64
#endif // !defined(TARGET_XARCH)

#endif // !defined(TARGET_ARM64)

    //
    // Common or driving functions
    //

    void genReserveProlog(BasicBlock* block); // currently unused
    void genReserveEpilog(BasicBlock* block);
    void genFnProlog();
    void genFnEpilog(BasicBlock* block);

    void genReserveFuncletProlog(BasicBlock* block);
    void genReserveFuncletEpilog(BasicBlock* block);
    void genFuncletProlog(BasicBlock* block);
    void genFuncletEpilog();
    void genCaptureFuncletPrologEpilogInfo();

    void genUpdateCurrentFunclet(BasicBlock* block);

    void genGeneratePrologsAndEpilogs();

#if defined(DEBUG)
    void genEmitterUnitTests();

#if defined(TARGET_ARM64)
    void genArm64EmitterUnitTestsGeneral();
    void genArm64EmitterUnitTestsAdvSimd();
    void genArm64EmitterUnitTestsSve();
    void genArm64EmitterUnitTestsPac();
#endif

#if defined(TARGET_AMD64)
    void genAmd64EmitterUnitTestsSse2();
    void genAmd64EmitterUnitTestsApx();
    void genAmd64EmitterUnitTestsAvx10v2();
    void genAmd64EmitterUnitTestsCCMP();
#endif

#endif // defined(DEBUG)

#ifdef TARGET_ARM64
    virtual void SetSaveFpLrWithAllCalleeSavedRegisters(bool value);
    virtual bool IsSaveFpLrWithAllCalleeSavedRegisters() const;
    bool         genSaveFpLrWithAllCalleeSavedRegisters;
    bool         genForceFuncletFrameType5;
    bool         genReverseAndPairCalleeSavedRegisters;
#endif // TARGET_ARM64

    //-------------------------------------------------------------------------
    //
    // End prolog/epilog generation
    //
    //-------------------------------------------------------------------------

    void      genSinglePush();
    void      genSinglePop();
    regMaskTP genPushRegs(regMaskTP regs, regMaskTP* byrefRegs, regMaskTP* noRefRegs);
    void      genPopRegs(regMaskTP regs, regMaskTP byrefRegs, regMaskTP noRefRegs);

    /*
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XX                                                                           XX
    XX                           Debugging Support                               XX
    XX                                                                           XX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    */

#ifdef DEBUG
    void genIPmappingDisp(unsigned mappingNum, const IPmappingDsc* ipMapping);
    void genIPmappingListDisp();
#endif // DEBUG

    void genIPmappingAdd(IPmappingDscKind kind, const DebugInfo& di, bool isLabel);
    void genIPmappingAddToFront(IPmappingDscKind kind, const DebugInfo& di, bool isLabel);
    void genIPmappingGen();
    void genAddRichIPMappingHere(const DebugInfo& di);

    void genReportRichDebugInfo();

    void genRecordRichDebugInfoInlineTree(InlineContext* context, ICorDebugInfo::InlineTreeNode* tree);

#ifdef DEBUG
    void genReportRichDebugInfoToFile();
    void genReportRichDebugInfoInlineTreeToFile(FILE* file, InlineContext* context, bool* first);
#endif

    void genEnsureCodeEmitted(const DebugInfo& di);

    //-------------------------------------------------------------------------
    // scope info for the variables

    void genSetScopeInfo(unsigned       which,
                         UNATIVE_OFFSET startOffs,
                         UNATIVE_OFFSET length,
                         unsigned       varNum,
                         unsigned       LVnum,
                         bool           avail,
                         siVarLoc*      varLoc);

    void genSetScopeInfo();
    // Send VariableLiveRanges as debug info to the debugger
    void genSetScopeInfoUsingVariableRanges();

public:
    void siInit();
    void checkICodeDebugInfo();

    // The logic used to report debug info on debug code is the same for ScopeInfo and
    // VariableLiveRange
    void siBeginBlock(BasicBlock* block);
    void siEndBlock(BasicBlock* block);

    // VariableLiveRange and siScope needs this method to report variables on debug code
    void siOpenScopesForNonTrackedVars(const BasicBlock* block, unsigned int lastBlockILEndOffset);

protected:
    bool siInFuncletRegion; // Have we seen the start of the funclet region?

    IL_OFFSET siLastEndOffs; // IL offset of the (exclusive) end of the last block processed

    /*
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XX                                                                           XX
    XX                          PrologScopeInfo                                  XX
    XX                                                                           XX
    XX We need special handling in the prolog block, as the parameter variables  XX
    XX may not be in the same position described by genLclVarTable - they all    XX
    XX start out on the stack                                                    XX
    XX                                                                           XX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    */
public:
    void psiBegProlog();

    void psiEndProlog();

    NATIVE_OFFSET psiGetVarStackOffset(const LclVarDsc* lclVarDsc) const;

    /*****************************************************************************
     *                        TrnslLocalVarInfo
     *
     * This struct holds the LocalVarInfo in terms of the generated native code
     * after a call to genSetScopeInfo()
     */

protected:
#ifdef DEBUG

    struct TrnslLocalVarInfo
    {
        unsigned       tlviVarNum;
        unsigned       tlviLVnum;
        VarName        tlviName;
        UNATIVE_OFFSET tlviStartPC;
        size_t         tlviLength;
        bool           tlviAvailable;
        siVarLoc       tlviVarLoc;
    };

    // Array of scopes of LocalVars in terms of native code

    TrnslLocalVarInfo* genTrnslLocalVarInfo;
    unsigned           genTrnslLocalVarCount;
#endif

    void genSetRegToConst(regNumber targetReg, var_types targetType, GenTree* tree);
#if defined(FEATURE_SIMD)
    void genSetRegToConst(regNumber targetReg, var_types targetType, simd_t* val);
    void genSetRegToConst(regNumber targetReg, var_types targetType, simdmask_t* val);
#endif
    void genCodeForTreeNode(GenTree* treeNode);
    void genCodeForBinary(GenTreeOp* treeNode);

#if defined(TARGET_X86)
    void genCodeForLongUMod(GenTreeOp* node);
#endif // TARGET_X86

    void genCodeForDivMod(GenTreeOp* treeNode);
    void genCodeForMul(GenTreeOp* treeNode);
    void genCodeForIncSaturate(GenTree* treeNode);
    void genCodeForMulHi(GenTreeOp* treeNode);
    void genLeaInstruction(GenTreeAddrMode* lea);
    void genSetRegToCond(regNumber dstReg, GenTree* tree);

#if defined(TARGET_ARMARCH) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    void genScaledAdd(emitAttr  attr,
                      regNumber targetReg,
                      regNumber baseReg,
                      regNumber indexReg,
                      int scale RISCV64_ARG(regNumber scaleTempReg));
#endif // TARGET_ARMARCH || TARGET_LOONGARCH64 || TARGET_RISCV64

#if defined(TARGET_RISCV64)
    void        genCodeForShxadd(GenTreeOp* tree);
    void        genCodeForAddUw(GenTreeOp* tree);
    void        genCodeForSlliUw(GenTreeOp* tree);
    instruction getShxaddVariant(int scale, bool useUnsignedVariant);
#endif

#if defined(TARGET_ARMARCH)
    void genCodeForMulLong(GenTreeOp* mul);
#endif // TARGET_ARMARCH

#if !defined(TARGET_64BIT)
    void genLongToIntCast(GenTree* treeNode);
#endif

    // Generate code for a GT_BITCAST that is not contained.
    void genCodeForBitCast(GenTreeOp* treeNode);

    // Generate the instruction to move a value between register files
    void genBitCast(var_types targetType, regNumber targetReg, var_types srcType, regNumber srcReg);

public:
    struct GenIntCastDesc
    {
        enum CheckKind
        {
            CHECK_NONE,
            CHECK_SMALL_INT_RANGE,
            CHECK_POSITIVE,
#ifdef TARGET_64BIT
            CHECK_UINT_RANGE,
            CHECK_POSITIVE_INT_RANGE,
            CHECK_INT_RANGE,
#endif
        };

        enum ExtendKind
        {
            COPY,
            ZERO_EXTEND_SMALL_INT,
            SIGN_EXTEND_SMALL_INT,
#ifdef TARGET_64BIT
            ZERO_EXTEND_INT,
            SIGN_EXTEND_INT,
#endif
            LOAD_ZERO_EXTEND_SMALL_INT,
            LOAD_SIGN_EXTEND_SMALL_INT,
#ifdef TARGET_64BIT
            LOAD_ZERO_EXTEND_INT,
            LOAD_SIGN_EXTEND_INT,
#endif
            LOAD_SOURCE
        };

    private:
        CheckKind  m_checkKind;
        unsigned   m_checkSrcSize;
        int        m_checkSmallIntMin;
        int        m_checkSmallIntMax;
        ExtendKind m_extendKind;
        unsigned   m_extendSrcSize;

    public:
        GenIntCastDesc(GenTreeCast* cast);

        CheckKind CheckKind() const
        {
            return m_checkKind;
        }

        unsigned CheckSrcSize() const
        {
            assert(m_checkKind != CHECK_NONE);
            return m_checkSrcSize;
        }

        int CheckSmallIntMin() const
        {
            assert(m_checkKind == CHECK_SMALL_INT_RANGE);
            return m_checkSmallIntMin;
        }

        int CheckSmallIntMax() const
        {
            assert(m_checkKind == CHECK_SMALL_INT_RANGE);
            return m_checkSmallIntMax;
        }

        ExtendKind ExtendKind() const
        {
            return m_extendKind;
        }

        unsigned ExtendSrcSize() const
        {
            return m_extendSrcSize;
        }
    };

protected:
    void genIntCastOverflowCheck(GenTreeCast* cast, const GenIntCastDesc& desc, regNumber reg);
    void genIntToIntCast(GenTreeCast* cast);
    void genFloatToFloatCast(GenTree* treeNode);
    void genFloatToIntCast(GenTree* treeNode);
    void genIntToFloatCast(GenTree* treeNode);
    void genCkfinite(GenTree* treeNode);
    void genCodeForCompare(GenTreeOp* tree);
#if defined(TARGET_ARM64) || defined(TARGET_AMD64)
    void genCodeForCCMP(GenTreeCCMP* ccmp);
#endif
    void genCodeForSelect(GenTreeOp* select);
    void genIntrinsic(GenTreeIntrinsic* treeNode);
    void genPutArgStk(GenTreePutArgStk* treeNode);
    void genPutArgReg(GenTreeOp* tree);

#if defined(TARGET_XARCH)
    unsigned getBaseVarForPutArgStk(GenTree* treeNode);
#endif // TARGET_XARCH

    unsigned getFirstArgWithStackSlot();

    void genCompareFloat(GenTree* treeNode);
    void genCompareInt(GenTree* treeNode);
#ifdef TARGET_XARCH
    bool     genCanAvoidEmittingCompareAgainstZero(GenTree* tree, var_types opType);
    GenTree* genTryFindFlagsConsumer(GenTree* flagsProducer, GenCondition** condition);
#endif

#ifdef FEATURE_SIMD
#ifdef TARGET_ARM64
    insOpts genGetSimdInsOpt(emitAttr size, var_types elementType);
#endif
    void genSimdUpperSave(GenTreeIntrinsic* node);
    void genSimdUpperRestore(GenTreeIntrinsic* node);

    void genSimd12UpperClear(regNumber tgtReg);

    // TYP_SIMD12 (i.e Vector3 of size 12 bytes) is not a hardware supported size and requires
    // two reads/writes on 64-bit targets. These routines abstract reading/writing of Vector3
    // values through an indirection. Note that Vector3 locals allocated on stack would have
    // their size rounded to TARGET_POINTER_SIZE (which is 8 bytes on 64-bit targets) and hence
    // Vector3 locals could be treated as TYP_SIMD16 while reading/writing.
    void genStoreIndTypeSimd12(GenTreeStoreInd* treeNode);
    void genLoadIndTypeSimd12(GenTreeIndir* treeNode);
    void genStoreLclTypeSimd12(GenTreeLclVarCommon* treeNode);
    void genLoadLclTypeSimd12(GenTreeLclVarCommon* treeNode);
#ifdef TARGET_XARCH
    void genEmitStoreLclTypeSimd12(GenTree* store, unsigned lclNum, unsigned offset);
    void genEmitLoadLclTypeSimd12(regNumber tgtReg, unsigned lclNum, unsigned offset);
#endif // TARGET_XARCH
#ifdef TARGET_X86
    void genStoreSimd12ToStack(regNumber dataReg, regNumber tmpReg);
    void genPutArgStkSimd12(GenTreePutArgStk* treeNode);
#endif // TARGET_X86
#endif // FEATURE_SIMD

#ifdef FEATURE_HW_INTRINSICS
    void genHWIntrinsic(GenTreeHWIntrinsic* node);
#if defined(TARGET_XARCH)
    void genHWIntrinsic_R_RM(
        GenTreeHWIntrinsic* node, instruction ins, emitAttr attr, regNumber reg, GenTree* rmOp, insOpts instOptions);
    void genHWIntrinsic_R_RM_I(
        GenTreeHWIntrinsic* node, instruction ins, emitAttr attr, int8_t ival, insOpts instOptions);
    void genHWIntrinsic_R_R_RM(GenTreeHWIntrinsic* node, instruction ins, emitAttr attr, insOpts instOptions);
    void genHWIntrinsic_R_R_RM_I(
        GenTreeHWIntrinsic* node, instruction ins, emitAttr attr, int8_t ival, insOpts instOptions);
    void genHWIntrinsic_R_R_RM_R(GenTreeHWIntrinsic* node, instruction ins, emitAttr attr, insOpts instOptions);
    void genHWIntrinsic_R_R_R_RM(instruction ins,
                                 emitAttr    attr,
                                 regNumber   targetReg,
                                 regNumber   op1Reg,
                                 regNumber   op2Reg,
                                 GenTree*    op3,
                                 insOpts     instOptions);
    void genHWIntrinsic_R_R_R_RM_I(
        GenTreeHWIntrinsic* node, instruction ins, emitAttr attr, int8_t ival, insOpts instOptions);

    void genBaseIntrinsic(GenTreeHWIntrinsic* node, insOpts instOptions);
    void genX86BaseIntrinsic(GenTreeHWIntrinsic* node, insOpts instOptions);
    void genSse42Intrinsic(GenTreeHWIntrinsic* node, insOpts instOptions);
    void genAvxFamilyIntrinsic(GenTreeHWIntrinsic* node, insOpts instOptions);
    void genFmaIntrinsic(GenTreeHWIntrinsic* node, insOpts instOptions);
    void genPermuteVar2x(GenTreeHWIntrinsic* node, insOpts instOptions);
    void genXCNTIntrinsic(GenTreeHWIntrinsic* node, instruction ins);
    void genX86SerializeIntrinsic(GenTreeHWIntrinsic* node);

    template <typename HWIntrinsicSwitchCaseBody>
    void genHWIntrinsicJumpTableFallback(NamedIntrinsic            intrinsic,
                                         instruction               ins,
                                         emitAttr                  attr,
                                         regNumber                 nonConstImmReg,
                                         regNumber                 baseReg,
                                         regNumber                 offsReg,
                                         HWIntrinsicSwitchCaseBody emitSwCase);

    void genNonTableDrivenHWIntrinsicsJumpTableFallback(GenTreeHWIntrinsic* node, GenTree* lastOp);

    static insOpts AddEmbBroadcastMode(insOpts instOptions);
#endif // defined(TARGET_XARCH)

#ifdef TARGET_ARM64
    class HWIntrinsicImmOpHelper final
    {
    public:
        HWIntrinsicImmOpHelper(CodeGen* codeGen, GenTree* immOp, GenTreeHWIntrinsic* intrin, int numInstrs = 1);

        HWIntrinsicImmOpHelper(CodeGen*            codeGen,
                               regNumber           immReg,
                               int                 immLowerBound,
                               int                 immUpperBound,
                               GenTreeHWIntrinsic* intrin,
                               int                 numInstrs = 1);

        void EmitBegin();
        void EmitCaseEnd();

        // Returns true after the last call to EmitCaseEnd() (i.e. this signals that code generation is done).
        bool Done() const
        {
            return (immValue > immUpperBound);
        }

        // Returns a value of the immediate operand that should be used for a case.
        int ImmValue() const
        {
            return immValue;
        }

    private:
        // Returns true if immOp is non contained immediate (i.e. the value of the immediate operand is enregistered in
        // nonConstImmReg).
        bool NonConstImmOp() const
        {
            return nonConstImmReg != REG_NA;
        }

        // Returns true if a non constant immediate operand can be either 0 or 1.
        bool TestImmOpZeroOrOne() const
        {
            assert(NonConstImmOp());
            return (immLowerBound == 0) && (immUpperBound == 1);
        }

        emitter* GetEmitter() const
        {
            return codeGen->GetEmitter();
        }

        CodeGen* const codeGen;
        BasicBlock*    endLabel;
        BasicBlock*    nonZeroLabel;
        int            immValue;
        int            immLowerBound;
        int            immUpperBound;
        regNumber      nonConstImmReg;
        regNumber      branchTargetReg;
        int            numInstrs;
    };

#endif // TARGET_ARM64

#endif // FEATURE_HW_INTRINSICS

#if !defined(TARGET_64BIT)

    // CodeGen for Long Ints

    void genStoreLongLclVar(GenTree* treeNode);

#endif // !defined(TARGET_64BIT)

    //-------------------------------------------------------------------------
    // genUpdateLifeStore: Do liveness update after tree store instructions
    // were emitted, update result var's home if it was stored on stack.
    //
    // Arguments:
    //     tree        -  GenTree node
    //     targetReg   -  of the tree
    //     varDsc      -  result value's variable
    //
    // Return Value:
    //     None.
    __forceinline void genUpdateLifeStore(GenTree* tree, regNumber targetReg, LclVarDsc* varDsc)
    {
        if (targetReg != REG_NA)
        {
            genProduceReg(tree);
        }
        else
        {
            genUpdateLife(tree);
            varDsc->SetRegNum(REG_STK);
        }
    }

    // Do liveness update for register produced by the current node in codegen after
    // code has been emitted for it.
    void genProduceReg(GenTree* tree);
    void genSpillLocal(unsigned varNum, var_types type, GenTreeLclVar* lclNode, regNumber regNum);
    void genUnspillLocal(
        unsigned varNum, var_types type, GenTreeLclVar* lclNode, regNumber regNum, bool reSpill, bool isLastUse);
    void      genUnspillRegIfNeeded(GenTree* tree);
    void      genUnspillRegIfNeeded(GenTree* tree, unsigned multiRegIndex);
    regNumber genConsumeReg(GenTree* tree);
    regNumber genConsumeReg(GenTree* tree, unsigned multiRegIndex);
    void      genCopyRegIfNeeded(GenTree* tree, regNumber needReg);
    void      genConsumeRegAndCopy(GenTree* tree, regNumber needReg);

    void genConsumeIfReg(GenTree* tree)
    {
        if (!tree->isContained())
        {
            (void)genConsumeReg(tree);
        }
    }

    void      genRegCopy(GenTree* tree);
    regNumber genRegCopy(GenTree* tree, unsigned multiRegIndex);
    void      genTransferRegGCState(regNumber dst, regNumber src);
    void      genConsumeAddress(GenTree* addr);
    void      genConsumeAddrMode(GenTreeAddrMode* mode);
    void      genSetBlockSize(GenTreeBlk* blkNode, regNumber sizeReg);
    void      genConsumeBlockSrc(GenTreeBlk* blkNode);
    void      genSetBlockSrc(GenTreeBlk* blkNode, regNumber srcReg);
    void      genConsumeBlockOp(GenTreeBlk* blkNode, regNumber dstReg, regNumber srcReg, regNumber sizeReg);

    void genConsumePutStructArgStk(GenTreePutArgStk* putArgStkNode,
                                   regNumber         dstReg,
                                   regNumber         srcReg,
                                   regNumber         sizeReg);
    void genConsumeRegs(GenTree* tree);
    void genConsumeOperands(GenTreeOp* tree);
#if defined(FEATURE_SIMD) || defined(FEATURE_HW_INTRINSICS)
    void genConsumeMultiOpOperands(GenTreeMultiOp* tree);
#endif
    void genEmitGSCookieCheck(bool pushReg);
    void genCodeForShift(GenTree* tree);

#if defined(TARGET_X86) || defined(TARGET_ARM)
    void genCodeForShiftLong(GenTree* tree);
#endif

#ifdef TARGET_XARCH
    void genCodeForShiftRMW(GenTreeStoreInd* storeInd);
#endif // TARGET_XARCH

    void genCodeForCast(GenTreeOp* tree);
    void genCodeForLclAddr(GenTreeLclFld* lclAddrNode);
    void genCodeForIndexAddr(GenTreeIndexAddr* tree);
    void genCodeForIndir(GenTreeIndir* tree);
    void genCodeForNegNot(GenTree* tree);
    void genCodeForBswap(GenTree* tree);
    bool genCanOmitNormalizationForBswap16(GenTree* tree);
    void genCodeForLclVar(GenTreeLclVar* tree);
    void genCodeForLclFld(GenTreeLclFld* tree);
    void genCodeForStoreLclFld(GenTreeLclFld* tree);
    void genCodeForStoreLclVar(GenTreeLclVar* tree);
    void genCodeForReturnTrap(GenTreeOp* tree);
    void genCodeForStoreInd(GenTreeStoreInd* tree);
    void genCodeForSwap(GenTreeOp* tree);
    void genCodeForCpObj(GenTreeBlk* cpObjNode);
    void genCodeForCpBlkRepMovs(GenTreeBlk* cpBlkNode);
    void genCodeForCpBlkUnroll(GenTreeBlk* cpBlkNode);
    void genCodeForPhysReg(GenTreePhysReg* tree);
#ifdef SWIFT_SUPPORT
    void genCodeForSwiftErrorReg(GenTree* tree);
#endif // SWIFT_SUPPORT
    void genCodeForAsyncContinuation(GenTree* tree);
    void genCodeForNullCheck(GenTreeIndir* tree);
    void genCodeForCmpXchg(GenTreeCmpXchg* tree);
    void genCodeForReuseVal(GenTree* treeNode);

    void genAlignStackBeforeCall(GenTreePutArgStk* putArgStk);
    void genAlignStackBeforeCall(GenTreeCall* call);
    void genRemoveAlignmentAfterCall(GenTreeCall* call, unsigned bias = 0);

#if defined(UNIX_X86_ABI)

    unsigned curNestedAlignment; // Keep track of alignment adjustment required during codegen.
    unsigned maxNestedAlignment; // The maximum amount of alignment adjustment required.

    void SubtractNestedAlignment(unsigned adjustment)
    {
        assert(curNestedAlignment >= adjustment);
        unsigned newNestedAlignment = curNestedAlignment - adjustment;
        if (curNestedAlignment != newNestedAlignment)
        {
            JITDUMP("Adjusting stack nested alignment from %d to %d\n", curNestedAlignment, newNestedAlignment);
        }
        curNestedAlignment = newNestedAlignment;
    }

    void AddNestedAlignment(unsigned adjustment)
    {
        unsigned newNestedAlignment = curNestedAlignment + adjustment;
        if (curNestedAlignment != newNestedAlignment)
        {
            JITDUMP("Adjusting stack nested alignment from %d to %d\n", curNestedAlignment, newNestedAlignment);
        }
        curNestedAlignment = newNestedAlignment;

        if (curNestedAlignment > maxNestedAlignment)
        {
            JITDUMP("Max stack nested alignment changed from %d to %d\n", maxNestedAlignment, curNestedAlignment);
            maxNestedAlignment = curNestedAlignment;
        }
    }

#endif

#ifndef TARGET_X86
    void genPutArgStkFieldList(GenTreePutArgStk* putArgStk, unsigned outArgVarNum);
#endif // !TARGET_X86

#ifdef TARGET_X86
    bool genAdjustStackForPutArgStk(GenTreePutArgStk* putArgStk);
    void genPushReg(var_types type, regNumber srcReg);
    void genPutArgStkFieldList(GenTreePutArgStk* putArgStk);
#endif // TARGET_X86

    void genPutStructArgStk(GenTreePutArgStk* treeNode);

    unsigned genMove8IfNeeded(unsigned size, regNumber tmpReg, GenTree* src, unsigned offset);
    unsigned genMove4IfNeeded(unsigned size, regNumber tmpReg, GenTree* src, unsigned offset);
    unsigned genMove2IfNeeded(unsigned size, regNumber tmpReg, GenTree* src, unsigned offset);
    unsigned genMove1IfNeeded(unsigned size, regNumber tmpReg, GenTree* src, unsigned offset);
    void     genCodeForLoadOffset(instruction ins, emitAttr size, regNumber dst, GenTree* base, unsigned offset);
    void     genStoreRegToStackArg(var_types type, regNumber reg, int offset);
    void     genStructPutArgRepMovs(GenTreePutArgStk* putArgStkNode);
    void     genStructPutArgUnroll(GenTreePutArgStk* putArgStkNode);
#ifdef TARGET_X86
    void genStructPutArgPush(GenTreePutArgStk* putArgStkNode);
#else
    void genStructPutArgPartialRepMovs(GenTreePutArgStk* putArgStkNode);
#endif

    void     genCodeForStoreBlk(GenTreeBlk* storeBlkNode);
    void     genCodeForInitBlkLoop(GenTreeBlk* initBlkNode);
    void     genCodeForInitBlkRepStos(GenTreeBlk* initBlkNode);
    void     genCodeForInitBlkUnroll(GenTreeBlk* initBlkNode);
    unsigned genEmitJumpTable(GenTree* treeNode, bool relativeAddr);
    void     genJumpTable(GenTree* tree);
    void     genTableBasedSwitch(GenTree* tree);
#if defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    instruction genGetInsForOper(GenTree* treeNode);
#else
    instruction genGetInsForOper(genTreeOps oper, var_types type);
#endif
    instruction genGetVolatileLdStIns(instruction   currentIns,
                                      regNumber     targetReg,
                                      GenTreeIndir* indir,
                                      bool*         needsBarrier);
    bool        genEmitOptimizedGCWriteBarrier(GCInfo::WriteBarrierForm writeBarrierForm, GenTree* addr, GenTree* data);
    GenTree*    getCallTarget(const GenTreeCall* call, CORINFO_METHOD_HANDLE* methHnd);
    regNumber   getCallIndirectionCellReg(GenTreeCall* call);
    void        genCall(GenTreeCall* call);
    void        genCallInstruction(GenTreeCall* call X86_ARG(target_ssize_t stackArgBytes));
    void        genDefinePendingCallLabel(GenTreeCall* call);
    void        genCallPlaceRegArgs(GenTreeCall* call);
    void        genJmpPlaceArgs(GenTree* jmp);
    void        genJmpPlaceVarArgs();
    BasicBlock* genCallFinally(BasicBlock* block);
#if defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    void genCodeForJumpCompare(GenTreeOpCC* tree);
#endif
#if defined(TARGET_ARM64)
    void genCodeForJumpCompare(GenTreeOpCC* tree);
    void genCompareImmAndJump(
        GenCondition::Code cond, regNumber reg, ssize_t compareImm, emitAttr size, BasicBlock* target);
    void genCodeForBfiz(GenTreeOp* tree);
#endif // TARGET_ARM64

    void genEHCatchRet(BasicBlock* block);
#if defined(FEATURE_EH_WINDOWS_X86)
    void genEHFinallyOrFilterRet(BasicBlock* block);
#endif // FEATURE_EH_WINDOWS_X86

    void genMultiRegStoreToSIMDLocal(GenTreeLclVar* lclNode);
    void genMultiRegStoreToLocal(GenTreeLclVar* lclNode);

    // Codegen for multi-register struct returns.
    bool isStructReturn(GenTree* treeNode);
#ifdef FEATURE_SIMD
    void genSIMDSplitReturn(GenTree* src, const ReturnTypeDesc* retTypeDesc);
#endif
    void genStructReturn(GenTree* treeNode);

#if defined(TARGET_X86) || defined(TARGET_ARM)
    void genLongReturn(GenTree* treeNode);
#endif // TARGET_X86 ||  TARGET_ARM

#if defined(TARGET_X86)
    void genFloatReturn(GenTree* treeNode);
#endif // TARGET_X86

#if defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    void genSimpleReturn(GenTree* treeNode);
#endif // TARGET_ARM64 || TARGET_LOONGARCH64 || TARGET_RISCV64

    void genReturn(GenTree* treeNode);
    void genReturnSuspend(GenTreeUnOp* treeNode);
    void genMarkReturnGCInfo();

#ifdef SWIFT_SUPPORT
    void genSwiftErrorReturn(GenTree* treeNode);
#endif // SWIFT_SUPPORT

#ifdef TARGET_XARCH
    void           genStackPointerConstantAdjustment(ssize_t spDelta, bool trackSpAdjustments);
    void           genStackPointerConstantAdjustmentWithProbe(ssize_t spDelta, bool trackSpAdjustments);
    target_ssize_t genStackPointerConstantAdjustmentLoopWithProbe(ssize_t spDelta, bool trackSpAdjustments);
    void           genStackPointerDynamicAdjustmentWithProbe(regNumber regSpDelta);
#else  // !TARGET_XARCH
    void           genStackPointerConstantAdjustment(ssize_t spDelta, regNumber regTmp);
    void           genStackPointerConstantAdjustmentWithProbe(ssize_t spDelta, regNumber regTmp);
    target_ssize_t genStackPointerConstantAdjustmentLoopWithProbe(ssize_t spDelta, regNumber regTmp);
#endif // !TARGET_XARCH

    void genLclHeap(GenTree* tree);
    void genCodeForMemmove(GenTreeBlk* tree);

    bool genIsRegCandidateLocal(GenTree* tree)
    {
        if (!tree->IsLocal())
        {
            return false;
        }
        return compiler->lvaGetDesc(tree->AsLclVarCommon())->lvIsRegCandidate();
    }

#ifdef TARGET_X86
    bool m_pushStkArg;
#else  // !TARGET_X86
    unsigned m_stkArgVarNum;
    unsigned m_stkArgOffset;
#endif // !TARGET_X86

#if defined(DEBUG) && defined(TARGET_XARCH)
    void genStackPointerCheck(bool      doStackPointerCheck,
                              unsigned  lvaStackPointerVar,
                              ssize_t   offset = 0,
                              regNumber regTmp = REG_NA);
#endif // defined(DEBUG) && defined(TARGET_XARCH)

#ifdef DEBUG
    GenTree* lastConsumedNode;
    void     genNumberOperandUse(GenTree* const operand, int& useNum) const;
    void     genCheckConsumeNode(GenTree* const node);
#else  // !DEBUG
    inline void genCheckConsumeNode(GenTree* treeNode)
    {
    }
#endif // DEBUG

    /*
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XX                                                                           XX
    XX                           Instruction                                     XX
    XX                                                                           XX
    XX  The interface to generate a machine-instruction.                         XX
    XX  Currently specific to x86                                                XX
    XX  TODO-Cleanup: Consider factoring this out of CodeGen                     XX
    XX                                                                           XX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    */

public:
    void instGen(instruction ins);
#if defined(TARGET_XARCH)
    void inst_JMP(emitJumpKind jmp, BasicBlock* tgtBlock, bool isRemovableJmpCandidate = false);
#else
    void inst_JMP(emitJumpKind jmp, BasicBlock* tgtBlock);
#endif

    void inst_SET(emitJumpKind condition, regNumber reg, insOpts instOptions = INS_OPTS_NONE);

    void inst_RV(instruction ins, regNumber reg, var_types type, emitAttr size = EA_UNKNOWN);

    void inst_Mov(var_types dstType,
                  regNumber dstReg,
                  regNumber srcReg,
                  bool      canSkip,
                  emitAttr  size  = EA_UNKNOWN,
                  insFlags  flags = INS_FLAGS_DONT_CARE);

    void inst_Mov_Extend(var_types srcType,
                         bool      srcInReg,
                         regNumber dstReg,
                         regNumber srcReg,
                         bool      canSkip,
                         emitAttr  size  = EA_UNKNOWN,
                         insFlags  flags = INS_FLAGS_DONT_CARE);

    void inst_RV_RV(instruction ins,
                    regNumber   reg1,
                    regNumber   reg2,
                    var_types   type  = TYP_I_IMPL,
                    emitAttr    size  = EA_UNKNOWN,
                    insFlags    flags = INS_FLAGS_DONT_CARE);

    void inst_RV_RV_RV(instruction ins,
                       regNumber   reg1,
                       regNumber   reg2,
                       regNumber   reg3,
                       emitAttr    size,
                       insFlags    flags = INS_FLAGS_DONT_CARE);

    void inst_IV(instruction ins, cnsval_ssize_t val);
    void inst_IV_handle(instruction ins, cnsval_ssize_t val);

    void inst_RV_IV(
        instruction ins, regNumber reg, target_ssize_t val, emitAttr size, insFlags flags = INS_FLAGS_DONT_CARE);

    void inst_ST_RV(instruction ins, TempDsc* tmp, unsigned ofs, regNumber reg, var_types type);

    void inst_FS_ST(instruction ins, emitAttr size, TempDsc* tmp, unsigned ofs);

    void inst_TT_RV(instruction ins, emitAttr size, GenTree* tree, regNumber reg);

    void inst_RV_SH(instruction ins, emitAttr size, regNumber reg, unsigned val, insFlags flags = INS_FLAGS_DONT_CARE);

#if defined(TARGET_XARCH)

    enum class OperandKind
    {
        ClsVar, // [CLS_VAR_ADDR]                 - "C" in the emitter.
        Local,  // [Local or spill temp + offset] - "S" in the emitter.
        Indir,  // [base+index*scale+disp]        - "A" in the emitter.
        Imm,    // immediate                      - "I" in the emitter.
        Reg     // reg                            - "R" in the emitter.
    };

    class OperandDesc
    {
        OperandKind m_kind;
        union
        {
            struct
            {
                CORINFO_FIELD_HANDLE m_fieldHnd;
            };
            struct
            {
                int      m_varNum;
                uint16_t m_offset;
            };
            struct
            {
                GenTree*      m_addr;
                GenTreeIndir* m_indir;
                var_types     m_indirType;
            };
            struct
            {
                ssize_t m_immediate;
                bool    m_immediateNeedsReloc;
            };
            struct
            {
                regNumber m_reg;
            };
        };

    public:
        OperandDesc(CORINFO_FIELD_HANDLE fieldHnd)
            : m_kind(OperandKind::ClsVar)
            , m_fieldHnd(fieldHnd)
        {
        }

        OperandDesc(int varNum, uint16_t offset)
            : m_kind(OperandKind::Local)
            , m_varNum(varNum)
            , m_offset(offset)
        {
        }

        OperandDesc(GenTreeIndir* indir)
            : m_kind(OperandKind::Indir)
            , m_addr(indir->Addr())
            , m_indir(indir)
            , m_indirType(indir->TypeGet())
        {
        }

        OperandDesc(var_types indirType, GenTree* addr)
            : m_kind(OperandKind::Indir)
            , m_addr(addr)
            , m_indir(nullptr)
            , m_indirType(indirType)
        {
        }

        OperandDesc(ssize_t immediate, bool immediateNeedsReloc)
            : m_kind(OperandKind::Imm)
            , m_immediate(immediate)
            , m_immediateNeedsReloc(immediateNeedsReloc)
        {
        }

        OperandDesc(regNumber reg)
            : m_kind(OperandKind::Reg)
            , m_reg(reg)
        {
        }

        OperandKind GetKind() const
        {
            return m_kind;
        }

        CORINFO_FIELD_HANDLE GetFieldHnd() const
        {
            assert(m_kind == OperandKind::ClsVar);
            return m_fieldHnd;
        }

        int GetVarNum() const
        {
            assert(m_kind == OperandKind::Local);
            return m_varNum;
        }

        int GetLclOffset() const
        {
            assert(m_kind == OperandKind::Local);
            return m_offset;
        }

        // TODO-Cleanup: instead of this rather unsightly workaround with
        // "indirForm", create a new abstraction for address modes to pass
        // to the emitter (or at least just use "addr"...).
        GenTreeIndir* GetIndirForm(GenTreeIndir* pIndirForm)
        {
            if (m_indir == nullptr)
            {
                GenTreeIndir indirForm = CodeGen::indirForm(m_indirType, m_addr);
                memcpy((void*)pIndirForm, (void*)&indirForm, sizeof(GenTreeIndir));
            }
            else
            {
                pIndirForm = m_indir;
            }

            return pIndirForm;
        }

        ssize_t GetImmediate() const
        {
            assert(m_kind == OperandKind::Imm);
            return m_immediate;
        }

        emitAttr GetEmitAttrForImmediate(emitAttr baseAttr) const
        {
            assert(m_kind == OperandKind::Imm);
            return m_immediateNeedsReloc ? EA_SET_FLG(baseAttr, EA_CNS_RELOC_FLG) : baseAttr;
        }

        regNumber GetReg() const
        {
            return m_reg;
        }

        bool IsContained() const
        {
            return m_kind != OperandKind::Reg;
        }
    };

    OperandDesc genOperandDesc(instruction ins, GenTree* op);

    void inst_TT(instruction ins, emitAttr size, GenTree* op1);
    void inst_RV_TT(instruction ins, emitAttr size, regNumber op1Reg, GenTree* op2);
    void inst_RV_RV_IV(instruction ins, emitAttr size, regNumber reg1, regNumber reg2, unsigned ival);
    void inst_RV_TT_IV(instruction ins, emitAttr attr, regNumber reg1, GenTree* rmOp, int ival, insOpts instOptions);
    void inst_RV_RV_TT(instruction ins,
                       emitAttr    size,
                       regNumber   targetReg,
                       regNumber   op1Reg,
                       GenTree*    op2,
                       bool        isRMW,
                       insOpts     instOptions);
    void inst_RV_RV_TT_IV(instruction ins,
                          emitAttr    size,
                          regNumber   targetReg,
                          regNumber   op1Reg,
                          GenTree*    op2,
                          int8_t      ival,
                          bool        isRMW,
                          insOpts     instOptions);
#endif

    void inst_set_SV_var(GenTree* tree);

#ifdef TARGET_ARM
    bool arm_Valid_Imm_For_Instr(instruction ins, target_ssize_t imm, insFlags flags);
    bool arm_Valid_Imm_For_Add(target_ssize_t imm, insFlags flag);
    bool arm_Valid_Imm_For_Add_SP(target_ssize_t imm);
#endif

    instruction ins_Move_Extend(var_types srcType, bool srcInReg);

    instruction ins_Copy(var_types dstType);
    instruction ins_Copy(regNumber srcReg, var_types dstType);
    instruction ins_FloatConv(var_types to, var_types from);
    instruction ins_MathOp(genTreeOps oper, var_types type);

    void instGen_Return(unsigned stkArgSize);

    void instGen_MemoryBarrier(BarrierKind barrierKind = BARRIER_FULL);

    void instGen_Set_Reg_To_Zero(emitAttr size, regNumber reg, insFlags flags = INS_FLAGS_DONT_CARE);

    void instGen_Set_Reg_To_Base_Plus_Imm(emitAttr  size,
                                          regNumber dstReg,
                                          regNumber baseReg,
                                          ssize_t   imm,
                                          insFlags flags = INS_FLAGS_DONT_CARE DEBUGARG(size_t targetHandle = 0)
                                              DEBUGARG(GenTreeFlags gtFlags = GTF_EMPTY));

    void instGen_Set_Reg_To_Imm(emitAttr  size,
                                regNumber reg,
                                ssize_t   imm,
                                insFlags flags = INS_FLAGS_DONT_CARE DEBUGARG(size_t targetHandle = 0)
                                    DEBUGARG(GenTreeFlags gtFlags = GTF_EMPTY));

#if defined(TARGET_AMD64)
    void instGen_Push2Pop2Ppx(instruction ins, regNumber reg1, regNumber reg2);
#endif // defined(TARGET_AMD64)

#ifdef TARGET_XARCH
    instruction genMapShiftInsToShiftByConstantIns(instruction ins, int shiftByValue);
#endif // TARGET_XARCH

#if defined(TARGET_ARM64)
    static insCond JumpKindToInsCond(emitJumpKind condition);
    static insOpts ShiftOpToInsOpts(genTreeOps op);
#elif defined(TARGET_XARCH)
    static instruction JumpKindToCmov(emitJumpKind condition);
    static instruction JumpKindToCcmp(emitJumpKind condition);
    static insOpts     OptsFromCFlags(insCflags flags);
#endif
    void inst_JCC(GenCondition condition, BasicBlock* target);
    void inst_SETCC(GenCondition condition, var_types type, regNumber dstReg);

#if !defined(TARGET_LOONGARCH64) && !defined(TARGET_RISCV64)
    void genCodeForJcc(GenTreeCC* tree);
    void genCodeForSetcc(GenTreeCC* setcc);
    void genCodeForJTrue(GenTreeOp* jtrue);
#endif // !TARGET_LOONGARCH64 && !TARGET_RISCV64
};

// A simple phase that just invokes a method on the codegen instance
//
class CodeGenPhase final : public Phase
{
public:
    CodeGenPhase(CodeGen* _codeGen, Phases _phase, void (CodeGen::*_action)())
        : Phase(_codeGen->GetCompiler(), _phase)
        , codeGen(_codeGen)
        , action(_action)
    {
    }

protected:
    virtual PhaseStatus DoPhase() override
    {
        (codeGen->*action)();
        return PhaseStatus::MODIFIED_EVERYTHING;
    }

private:
    CodeGen* codeGen;
    void (CodeGen::*action)();
};

// Wrapper for using CodeGenPhase
//
inline void DoPhase(CodeGen* _codeGen, Phases _phase, void (CodeGen::*_action)())
{
    CodeGenPhase phase(_codeGen, _phase, _action);
    phase.Run();
}

#endif // _CODEGEN_H_
