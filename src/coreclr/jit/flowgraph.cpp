// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "jitpch.h"

#ifdef _MSC_VER
#pragma hdrstop
#endif

#include "lower.h" // for LowerRange()

// Flowgraph Miscellany

//------------------------------------------------------------------------
// blockNeedsGCPoll: Determine whether the block needs GC poll inserted
//
// Arguments:
//   block         - the block to check
//
// Notes:
//    The GC poll may not be required because of optimizations applied earlier
//    or because of GC poll done implicitly by regular unmanaged calls.
//
// Returns:
//    Whether the GC poll needs to be inserted after the block
//
static bool blockNeedsGCPoll(BasicBlock* block)
{
    bool blockMayNeedGCPoll = block->HasFlag(BBF_NEEDS_GCPOLL);
    for (Statement* const stmt : block->NonPhiStatements())
    {
        if ((stmt->GetRootNode()->gtFlags & GTF_CALL) != 0)
        {
            for (GenTree* const tree : stmt->TreeList())
            {
                if (tree->OperIs(GT_CALL))
                {
                    GenTreeCall* call = tree->AsCall();
                    if (call->IsUnmanaged())
                    {
                        if (!call->IsSuppressGCTransition())
                        {
                            // If the block contains regular unmanaged call, we can depend on it
                            // to poll for GC. No need to scan further.
                            return false;
                        }

                        blockMayNeedGCPoll = true;
                    }
                }
                else if (tree->OperIs(GT_GCPOLL))
                {
                    blockMayNeedGCPoll = true;
                }
            }
        }
    }
    return blockMayNeedGCPoll;
}

//------------------------------------------------------------------------------
// fgInsertGCPolls : Insert GC polls for basic blocks containing calls to methods
//                   with SuppressGCTransitionAttribute.
//
// Notes:
//    When not optimizing, the method relies on BBF_HAS_SUPPRESSGC_CALL flag to
//    find the basic blocks that require GC polls; when optimizing the tree nodes
//    are scanned to find calls to methods with SuppressGCTransitionAttribute.
//
//    This must be done after any transformations that would add control flow between
//    calls.
//
// Returns:
//    PhaseStatus indicating what, if anything, was changed.
//
PhaseStatus Compiler::fgInsertGCPolls()
{
    PhaseStatus result = PhaseStatus::MODIFIED_NOTHING;

    if ((optMethodFlags & OMF_NEEDS_GCPOLLS) == 0)
    {
        return result;
    }

    bool createdPollBlocks = false;

    // Walk through the blocks and hunt for a block that needs a GC Poll
    //
    for (BasicBlock* block = fgFirstBB; block != nullptr; block = block->Next())
    {
        compCurBB = block;

        // When optimizations are enabled, we can't rely on BBF_HAS_SUPPRESSGC_CALL flag:
        // the call could've been moved, e.g., hoisted from a loop, CSE'd, etc.
        if (opts.OptimizationDisabled())
        {
            if (!block->HasAnyFlag(BBF_HAS_SUPPRESSGC_CALL | BBF_NEEDS_GCPOLL))
            {
                continue;
            }
        }
        else
        {
            if (!blockNeedsGCPoll(block))
            {
                continue;
            }
        }

        result = PhaseStatus::MODIFIED_EVERYTHING;

        // This block needs a GC poll. We either just insert a callout or we split the block and inline part of
        // the test.

        // If we're doing GCPOLL_CALL, just insert a GT_CALL node before the last node in the block.

        assert(block->KindIs(BBJ_RETURN, BBJ_ALWAYS, BBJ_COND, BBJ_SWITCH, BBJ_THROW, BBJ_CALLFINALLY));

        GCPollType pollType = GCPOLL_INLINE;

        // We'd like to insert an inline poll. Below is the list of places where we
        // can't or don't want to emit an inline poll. Check all of those. If after all of that we still
        // have INLINE, then emit an inline check.

        if (opts.OptimizationDisabled())
        {
            // Don't split blocks and create inlined polls unless we're optimizing.
            //
            JITDUMP("Selecting CALL poll in block " FMT_BB " because of debug/minopts\n", block->bbNum);
            pollType = GCPOLL_CALL;
        }
        else if (genReturnBB == block)
        {
            // we don't want to split the single return block
            //
            JITDUMP("Selecting CALL poll in block " FMT_BB " because it is the single return block\n", block->bbNum);
            pollType = GCPOLL_CALL;
        }
        else if (BBJ_SWITCH == block->GetKind())
        {
            // We don't want to deal with all the outgoing edges of a switch block.
            //
            JITDUMP("Selecting CALL poll in block " FMT_BB " because it is a SWITCH block\n", block->bbNum);
            pollType = GCPOLL_CALL;
        }
        else if (block->HasFlag(BBF_COLD))
        {
            // We don't want to split a cold block.
            //
            JITDUMP("Selecting CALL poll in block " FMT_BB " because it is a cold block\n", block->bbNum);
            pollType = GCPOLL_CALL;
        }

        BasicBlock* curBasicBlock = fgCreateGCPoll(pollType, block);
        createdPollBlocks |= (block != curBasicBlock);
        block = curBasicBlock;
    }

    // We should never split blocks unless we're optimizing.
    assert(!createdPollBlocks || opts.OptimizationEnabled());

    return result;
}

//------------------------------------------------------------------------------
// fgCreateGCPoll : Insert a GC poll of the specified type for the given basic block.
//
// Arguments:
//    pollType  - The type of GC poll to insert
//    block     - Basic block to insert the poll for
//
// Return Value:
//    If new basic blocks are inserted, the last inserted block; otherwise, the input block.
//
BasicBlock* Compiler::fgCreateGCPoll(GCPollType pollType, BasicBlock* block)
{
    bool createdPollBlocks;

    void* addrTrap;
    void* pAddrOfCaptureThreadGlobal;

    addrTrap = info.compCompHnd->getAddrOfCaptureThreadGlobal(&pAddrOfCaptureThreadGlobal);

    // If the trap and address of thread global are null, make the call.
    if (addrTrap == nullptr && pAddrOfCaptureThreadGlobal == nullptr)
    {
        pollType = GCPOLL_CALL;
    }

    // Create the GC_CALL node
    GenTree* call = gtNewHelperCallNode(CORINFO_HELP_POLL_GC, TYP_VOID);
    call          = fgMorphCall(call->AsCall());
    gtSetEvalOrder(call);

    BasicBlock* bottom = nullptr;

    if (pollType == GCPOLL_CALL)
    {
        createdPollBlocks = false;

        Statement* newStmt = nullptr;

        if (block->HasFlag(BBF_NEEDS_GCPOLL))
        {
            // This is a block that ends in a tail call; gc probe early.
            //
            newStmt = fgNewStmtAtBeg(block, call);
        }
        else if (block->KindIs(BBJ_ALWAYS, BBJ_CALLFINALLY))
        {
            // For BBJ_ALWAYS and BBJ_CALLFINALLY, we don't need to insert it before the condition.
            // Just append it.
            newStmt = fgNewStmtAtEnd(block, call);
        }
        else
        {
            newStmt = fgNewStmtNearEnd(block, call);
            // For DDB156656, we need to associate the GC Poll with the IL offset (and therefore sequence
            // point) of the tree before which we inserted the poll.  One example of when this is a
            // problem:
            //  if (...) {  //1
            //      ...
            //  } //2
            //  else { //3
            //      ...
            //  }
            //  (gcpoll) //4
            //  return. //5
            //
            //  If we take the if statement at 1, we encounter a jump at 2.  This jumps over the else
            //  and lands at 4.  4 is where we inserted the gcpoll.  However, that is associated with
            //  the sequence point a 3.  Therefore, the debugger displays the wrong source line at the
            //  gc poll location.
            //
            //  More formally, if control flow targets an instruction, that instruction must be the
            //  start of a new sequence point.
            Statement* nextStmt = newStmt->GetNextStmt();
            if (nextStmt != nullptr)
            {
                // Is it possible for gtNextStmt to be NULL?
                newStmt->SetDebugInfo(nextStmt->GetDebugInfo());
            }
        }

        if (fgNodeThreading != NodeThreading::None)
        {
            gtSetStmtInfo(newStmt);
            fgSetStmtSeq(newStmt);
        }

        block->SetFlags(BBF_GC_SAFE_POINT);
#ifdef DEBUG
        if (verbose)
        {
            printf("*** creating GC Poll in block " FMT_BB "\n", block->bbNum);
            gtDispBlockStmts(block);
        }
#endif // DEBUG
    }
    else // GCPOLL_INLINE
    {
        assert(pollType == GCPOLL_INLINE);
        createdPollBlocks = true;
        // if we're doing GCPOLL_INLINE, then:
        //  1) Create two new blocks: Poll and Bottom.  The original block is called Top.

        // I want to create:
        // top -> poll -> bottom (lexically)
        // so that we jump over poll to get to bottom.
        BasicBlock* top = block;

        BasicBlock* poll = fgNewBBafter(BBJ_ALWAYS, top, true);
        bottom           = fgNewBBafter(top->GetKind(), poll, true);

        // Update block flags
        const BasicBlockFlags originalFlags = top->GetFlagsRaw() | BBF_GC_SAFE_POINT;

        // We need to keep a few flags...
        //
        noway_assert((originalFlags & (BBF_SPLIT_NONEXIST & ~BBF_RETLESS_CALL)) == 0);
        top->SetFlagsRaw(originalFlags & (~(BBF_SPLIT_LOST | BBF_RETLESS_CALL) | BBF_GC_SAFE_POINT));
        bottom->SetFlags(originalFlags & (BBF_SPLIT_GAINED | BBF_IMPORTED | BBF_GC_SAFE_POINT | BBF_RETLESS_CALL));
        bottom->inheritWeight(top);
        poll->SetFlags(originalFlags & (BBF_SPLIT_GAINED | BBF_IMPORTED | BBF_GC_SAFE_POINT));

        // Mark Poll as rarely run.
        poll->bbSetRunRarely();

        // Add the GC_CALL node to Poll.
        Statement* pollStmt = fgNewStmtAtEnd(poll, call);
        if (fgNodeThreading != NodeThreading::None)
        {
            gtSetStmtInfo(pollStmt);
            fgSetStmtSeq(pollStmt);
        }

        // Remove the last statement from Top and add it to Bottom if necessary.
        if (top->KindIs(BBJ_COND, BBJ_RETURN, BBJ_THROW))
        {
            Statement* stmt = top->firstStmt();
            while (stmt->GetNextStmt() != nullptr)
            {
                stmt = stmt->GetNextStmt();
            }
            fgUnlinkStmt(top, stmt);
            fgInsertStmtAtEnd(bottom, stmt);
        }

        // for BBJ_ALWAYS blocks, bottom is an empty block.

        // Create a GT_EQ node that checks against g_TrapReturningThreads.  True jumps to Bottom,
        // false falls through to poll.  Add this to the end of Top.  Top is now BBJ_COND.  Bottom is
        // now a jump target

#ifdef ENABLE_FAST_GCPOLL_HELPER
        // Prefer the fast gc poll helepr over the double indirection
        noway_assert(pAddrOfCaptureThreadGlobal == nullptr);
#endif

        GenTree* value; // The value of g_TrapReturningThreads
        if (pAddrOfCaptureThreadGlobal != nullptr)
        {
            // Use a double indirection
            GenTree* addr =
                gtNewIndOfIconHandleNode(TYP_I_IMPL, (size_t)pAddrOfCaptureThreadGlobal, GTF_ICON_CONST_PTR);
            value = gtNewIndir(TYP_INT, addr, GTF_IND_NONFAULTING);
        }
        else
        {
            // Use a single indirection
            value = gtNewIndOfIconHandleNode(TYP_INT, (size_t)addrTrap, GTF_ICON_GLOBAL_PTR);
        }

        // NOTE: in c++ an equivalent load is done via LoadWithoutBarrier() to ensure that the
        // program order is preserved. (not hoisted out of a loop or cached in a local, for example)
        //
        // Here we introduce the read really late after all major optimizations are done, and the location
        // is formally unknown, so noone could optimize the load, thus no special flags are needed.

        // Compare for equal to zero
        GenTree* trapRelop = gtNewOperNode(GT_EQ, TYP_INT, value, gtNewIconNode(0, TYP_INT));

        trapRelop->gtFlags |= GTF_RELOP_JMP_USED | GTF_DONT_CSE;
        GenTree* trapCheck = gtNewOperNode(GT_JTRUE, TYP_VOID, trapRelop);
        gtSetEvalOrder(trapCheck);
        Statement* trapCheckStmt = fgNewStmtAtEnd(top, trapCheck);
        if (fgNodeThreading != NodeThreading::None)
        {
            gtSetStmtInfo(trapCheckStmt);
            fgSetStmtSeq(trapCheckStmt);
        }

#ifdef DEBUG
        if (verbose)
        {
            printf("Adding trapCheck in " FMT_BB "\n", top->bbNum);
            gtDispTree(trapCheck);
        }
#endif

        // Bottom has Top and Poll as its predecessors.  Poll has just Top as a predecessor.
        FlowEdge* const trueEdge  = fgAddRefPred(bottom, top);
        FlowEdge* const falseEdge = fgAddRefPred(poll, top);
        trueEdge->setLikelihood(1.0);
        falseEdge->setLikelihood(0.0);

        FlowEdge* const newEdge = fgAddRefPred(bottom, poll);
        poll->SetTargetEdge(newEdge);
        assert(poll->JumpsToNext());

        // Replace Top with Bottom in the predecessor list of all outgoing edges from Bottom
        // (1 for unconditional branches, 2 for conditional branches, N for switches).
        switch (top->GetKind())
        {
            case BBJ_RETURN:
            case BBJ_THROW:
                // no successors
                break;

            case BBJ_COND:
                // replace predecessor in true/false successors.
                fgReplacePred(top->GetFalseEdge(), bottom);
                fgReplacePred(top->GetTrueEdge(), bottom);
                break;

            case BBJ_ALWAYS:
            case BBJ_CALLFINALLY:
                fgReplacePred(top->GetTargetEdge(), bottom);
                break;

            case BBJ_SWITCH:
                NO_WAY("SWITCH should be a call rather than an inlined poll.");
                break;

            default:
                NO_WAY("Unknown block type for updating predecessor lists.");
                break;
        }

        bottom->TransferTarget(top);
        top->SetCond(trueEdge, falseEdge);

        if (compCurBB == top)
        {
            compCurBB = bottom;
        }

#ifdef DEBUG
        if (verbose)
        {
            printf("*** creating inlined GC Poll in top block " FMT_BB "\n", top->bbNum);
            gtDispBlockStmts(top);
            printf(" poll block is " FMT_BB "\n", poll->bbNum);
            gtDispBlockStmts(poll);
            printf(" bottom block is " FMT_BB "\n", bottom->bbNum);
            gtDispBlockStmts(bottom);

            printf("\nAfter this change in fgCreateGCPoll the BB graph is:");
            fgDispBasicBlocks(false);
        }
#endif // DEBUG
    }

    return createdPollBlocks ? bottom : block;
}

//------------------------------------------------------------------------
// fgCanSwitchToOptimized: Determines if conditions are met to allow switching the opt level to optimized
//
// Return Value:
//    True if the opt level may be switched from tier 0 to optimized, false otherwise
//
// Assumptions:
//    - compInitOptions() has been called
//    - compSetOptimizationLevel() has not been called
//
// Notes:
//    This method is to be called at some point before compSetOptimizationLevel() to determine if the opt level may be
//    changed based on information gathered in early phases.

bool Compiler::fgCanSwitchToOptimized()
{
    bool result = opts.jitFlags->IsSet(JitFlags::JIT_FLAG_TIER0) && !opts.jitFlags->IsSet(JitFlags::JIT_FLAG_MIN_OPT) &&
                  !opts.compDbgCode && !compIsForInlining();
    if (result)
    {
        // Ensure that it would be safe to change the opt level
        assert(opts.compFlags == CLFLG_MINOPT);
        assert(!opts.IsMinOptsSet());
    }

    return result;
}

//------------------------------------------------------------------------
// fgSwitchToOptimized: Switch the opt level from tier 0 to optimized
//
// Arguments:
//    reason - reason why opt level was switched
//
// Assumptions:
//    - fgCanSwitchToOptimized() is true
//    - compSetOptimizationLevel() has not been called
//
// Notes:
//    This method is to be called at some point before compSetOptimizationLevel() to switch the opt level to optimized
//    based on information gathered in early phases.

void Compiler::fgSwitchToOptimized(const char* reason)
{
    assert(fgCanSwitchToOptimized());

    // Switch to optimized and re-init options
    JITDUMP("****\n**** JIT Tier0 jit request switching to Tier1 because: %s\n****\n", reason);
    assert(opts.jitFlags->IsSet(JitFlags::JIT_FLAG_TIER0));
    opts.jitFlags->Clear(JitFlags::JIT_FLAG_TIER0);
    opts.jitFlags->Clear(JitFlags::JIT_FLAG_BBINSTR);
    opts.jitFlags->Clear(JitFlags::JIT_FLAG_BBINSTR_IF_LOOPS);
    opts.jitFlags->Clear(JitFlags::JIT_FLAG_OSR);
    opts.jitFlags->Set(JitFlags::JIT_FLAG_BBOPT);

    // Leave a note for jit diagnostics
    compSwitchedToOptimized = true;

    compInitOptions(opts.jitFlags);

    // Notify the VM of the change
    info.compCompHnd->setMethodAttribs(info.compMethodHnd, CORINFO_FLG_SWITCHED_TO_OPTIMIZED);
}

//------------------------------------------------------------------------
// fgMayExplicitTailCall: Estimates conservatively for an explicit tail call, if the importer may actually use a tail
// call.
//
// Return Value:
//    - False if a tail call will not be generated
//    - True if a tail call *may* be generated
//
// Assumptions:
//    - compInitOptions() has been called
//    - info.compIsVarArgs has been initialized
//    - An explicit tail call has been seen
//    - compSetOptimizationLevel() has not been called

bool Compiler::fgMayExplicitTailCall()
{
    assert(!compIsForInlining());

    if (info.compFlags & CORINFO_FLG_SYNCH)
    {
        // Caller is synchronized
        return false;
    }

    if (opts.IsReversePInvoke())
    {
        // Reverse P/Invoke
        return false;
    }

#if !FEATURE_FIXED_OUT_ARGS
    if (info.compIsVarArgs)
    {
        // Caller is varargs
        return false;
    }
#endif // FEATURE_FIXED_OUT_ARGS

    return true;
}

//------------------------------------------------------------------------
// fgImport: read the IL for the method and create jit IR
//
// Returns:
//    phase status
//
PhaseStatus Compiler::fgImport()
{
    impImport();

    // Estimate how much of method IL was actually imported.
    //
    // Note this includes (to some extent) the impact of importer folded
    // branches, provided the folded tree covered the entire block's IL.
    unsigned importedILSize = 0;
    for (BasicBlock* const block : Blocks())
    {
        if (block->HasFlag(BBF_IMPORTED))
        {
            // Assume if we generate any IR for the block we generate IR for the entire block.
            if (block->firstStmt() != nullptr)
            {
                IL_OFFSET beginOffset = block->bbCodeOffs;
                IL_OFFSET endOffset   = block->bbCodeOffsEnd;

                if ((beginOffset != BAD_IL_OFFSET) && (endOffset != BAD_IL_OFFSET) && (endOffset > beginOffset))
                {
                    unsigned blockILSize = endOffset - beginOffset;
                    importedILSize += blockILSize;
                }
            }
        }
    }

    // Could be tripped up if we ever duplicate blocks
    assert(importedILSize <= info.compILCodeSize);

    // Leave a note if we only did a partial import.
    if (importedILSize != info.compILCodeSize)
    {
        JITDUMP("\n** Note: %s IL was partially imported -- imported %u of %u bytes of method IL\n",
                compIsForInlining() ? "inlinee" : "root method", importedILSize, info.compILCodeSize);
    }

    // Record this for diagnostics and for the inliner's budget computations
    info.compILImportSize = importedILSize;

    if (compIsForInlining())
    {
        compInlineResult->SetImportedILSize(info.compILImportSize);
    }

    // Now that we've made it through the importer, we know the IL was valid.
    // If we synthesized profile data and thought it should be consistent,
    // but it wasn't, assert now.
    //
    if (fgPgoSynthesized && fgPgoConsistent)
    {
        assert(!fgPgoDeferredInconsistency);

        // Reset this as it is a one-shot thing.
        //
        INDEBUG(fgPgoDeferredInconsistency = false);
    }

    fgImportDone = true;

    return PhaseStatus::MODIFIED_EVERYTHING;
}

/*****************************************************************************
 * This function returns true if tree is a node with a call
 * that unconditionally throws an exception
 */

bool Compiler::fgIsThrow(GenTree* tree)
{
    if (!tree->IsCall())
    {
        return false;
    }
    GenTreeCall* call = tree->AsCall();
    if (call->IsHelperCall() && s_helperCallProperties.AlwaysThrow(eeGetHelperNum(call->gtCallMethHnd)))
    {
        assert(call->IsNoReturn());
        noway_assert(call->gtFlags & GTF_EXCEPT);
        return true;
    }
    return false;
}

/*****************************************************************************
 * This function returns true for blocks that are in different hot-cold regions.
 * It returns false when the blocks are both in the same regions
 */

bool Compiler::fgInDifferentRegions(const BasicBlock* blk1, const BasicBlock* blk2) const
{
    noway_assert(blk1 != nullptr);
    noway_assert(blk2 != nullptr);

    if (fgFirstColdBlock == nullptr)
    {
        return false;
    }

    // If one block is Hot and the other is Cold then we are in different regions
    return (blk1->HasFlag(BBF_COLD) != blk2->HasFlag(BBF_COLD));
}

bool Compiler::fgIsBlockCold(BasicBlock* blk)
{
    noway_assert(blk != nullptr);

    if (fgFirstColdBlock == nullptr)
    {
        return false;
    }

    return blk->HasFlag(BBF_COLD);
}

/*****************************************************************************
 * This function returns true if tree is a GT_COMMA node with a call
 * that unconditionally throws an exception
 */

bool Compiler::fgIsCommaThrow(GenTree* tree, bool forFolding /* = false */)
{
    // Instead of always folding comma throws,
    // with stress enabled we only fold half the time

    if (forFolding && compStressCompile(STRESS_FOLD, 50))
    {
        return false; /* Don't fold */
    }

    /* Check for cast of a GT_COMMA with a throw overflow */
    if (tree->OperIs(GT_COMMA) && (tree->gtFlags & GTF_CALL) && (tree->gtFlags & GTF_EXCEPT))
    {
        return (fgIsThrow(tree->AsOp()->gtOp1));
    }
    return false;
}

//------------------------------------------------------------------------
// fgGetStaticsCCtorHelper: Creates a BasicBlock from the `tree` node.
//
// Arguments:
//    cls       - The class handle
//    helper    - The helper function
//    typeIndex - The static block type index. Used only for
//                CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED,
//                CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED, or
//                CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED2 to cache
//                the static block in an array at index typeIndex.
//
// Return Value:
//    The call node corresponding to the helper
GenTreeCall* Compiler::fgGetStaticsCCtorHelper(CORINFO_CLASS_HANDLE cls, CorInfoHelpFunc helper, uint32_t typeIndex)
{
    bool         bNeedClassID = true;
    GenTreeFlags callFlags    = GTF_EMPTY;

    var_types type = TYP_BYREF;

    // This is sort of ugly, as we have knowledge of what the helper is returning.
    // We need the return type.
    switch (helper)
    {
        case CORINFO_HELP_GET_GCSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GET_NONGCSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GET_GCTHREADSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GET_NONGCTHREADSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETDYNAMIC_GCSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETDYNAMIC_NONGCSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED:
        case CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED:
            callFlags |= GTF_CALL_HOISTABLE;
            FALLTHROUGH;

        case CORINFO_HELP_GET_GCSTATIC_BASE:
        case CORINFO_HELP_GET_NONGCSTATIC_BASE:
        case CORINFO_HELP_GETDYNAMIC_GCSTATIC_BASE:
        case CORINFO_HELP_GETDYNAMIC_NONGCSTATIC_BASE:
        case CORINFO_HELP_GET_GCTHREADSTATIC_BASE:
        case CORINFO_HELP_GET_NONGCTHREADSTATIC_BASE:
        case CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE:
        case CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE:
            // type = TYP_BYREF;
            break;

        case CORINFO_HELP_GETPINNED_GCSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETPINNED_NONGCSTATIC_BASE_NOCTOR:
        case CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED2:
        case CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED2_NOJITOPT:
            callFlags |= GTF_CALL_HOISTABLE;
            FALLTHROUGH;

        case CORINFO_HELP_GETPINNED_GCSTATIC_BASE:
        case CORINFO_HELP_GETPINNED_NONGCSTATIC_BASE:
            type = TYP_I_IMPL;
            break;

        case CORINFO_HELP_INITCLASS:
            type = TYP_VOID;
            break;

        default:
            assert(!"unknown shared statics helper");
            break;
    }

    if (!(callFlags & GTF_CALL_HOISTABLE))
    {
        if (info.compCompHnd->getClassAttribs(cls) & CORINFO_FLG_BEFOREFIELDINIT)
        {
            callFlags |= GTF_CALL_HOISTABLE;
        }
    }

    GenTreeCall* result;

    if ((helper == CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED) ||
        (helper == CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED) ||
        (helper == CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED2) ||
        (helper == CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED2_NOJITOPT))
    {
        result = gtNewHelperCallNode(helper, type, gtNewIconNode(typeIndex));
    }
    else if (helper == CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE_NOCTOR ||
             helper == CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE_NOCTOR ||
             helper == CORINFO_HELP_GETDYNAMIC_GCTHREADSTATIC_BASE ||
             helper == CORINFO_HELP_GETDYNAMIC_NONGCTHREADSTATIC_BASE)
    {
        result = gtNewHelperCallNode(helper, type,
                                     gtNewIconNode((size_t)info.compCompHnd->getClassThreadStaticDynamicInfo(cls),
                                                   TYP_I_IMPL));
    }
    else if (helper == CORINFO_HELP_GETDYNAMIC_GCSTATIC_BASE || helper == CORINFO_HELP_GETDYNAMIC_NONGCSTATIC_BASE ||
             helper == CORINFO_HELP_GETDYNAMIC_GCSTATIC_BASE_NOCTOR ||
             helper == CORINFO_HELP_GETDYNAMIC_NONGCSTATIC_BASE_NOCTOR ||
             helper == CORINFO_HELP_GETPINNED_GCSTATIC_BASE || helper == CORINFO_HELP_GETPINNED_NONGCSTATIC_BASE ||
             helper == CORINFO_HELP_GETPINNED_GCSTATIC_BASE_NOCTOR ||
             helper == CORINFO_HELP_GETPINNED_NONGCSTATIC_BASE_NOCTOR)
    {
        result = gtNewHelperCallNode(helper, type,
                                     gtNewIconNode(info.compCompHnd->getClassStaticDynamicInfo(cls), TYP_I_IMPL));
    }
    else
    {
        result = gtNewHelperCallNode(helper, type, gtNewIconEmbClsHndNode(cls));
    }

    if (IsStaticHelperEligibleForExpansion(result))
    {
        // Keep class handle attached to the helper call since it's difficult to restore it.
        result->gtInitClsHnd = cls;
    }
    result->gtFlags |= callFlags;

    // If we're importing the special EqualityComparer<T>.Default or Comparer<T>.Default
    // intrinsics, flag the helper call. Later during inlining, we can
    // remove the helper call if the associated field lookup is unused.
    if ((info.compFlags & CORINFO_FLG_INTRINSIC) != 0)
    {
        NamedIntrinsic ni = lookupNamedIntrinsic(info.compMethodHnd);
        if ((ni == NI_System_Collections_Generic_EqualityComparer_get_Default) ||
            (ni == NI_System_Collections_Generic_Comparer_get_Default))
        {
            JITDUMP("\nmarking helper call [%06u] as special dce...\n", result->gtTreeID);
            result->gtCallMoreFlags |= GTF_CALL_M_HELPER_SPECIAL_DCE;
        }
    }

    return result;
}

//------------------------------------------------------------------------------
// fgSetPreferredInitCctor: Set CORINFO_HELP_READYTORUN_NONGCSTATIC_BASE as the
// preferred call constructure if it is undefined.
//
void Compiler::fgSetPreferredInitCctor()
{
    if (m_preferredInitCctor == CORINFO_HELP_UNDEF)
    {
        // This is the cheapest helper that triggers the constructor.
        m_preferredInitCctor = CORINFO_HELP_READYTORUN_NONGCSTATIC_BASE;
    }
}

GenTreeCall* Compiler::fgGetSharedCCtor(CORINFO_CLASS_HANDLE cls)
{
#ifdef FEATURE_READYTORUN
    if (IsAot())
    {
        CORINFO_RESOLVED_TOKEN resolvedToken;
        memset(&resolvedToken, 0, sizeof(resolvedToken));
        resolvedToken.hClass = cls;
        fgSetPreferredInitCctor();
        return impReadyToRunHelperToTree(&resolvedToken, m_preferredInitCctor, TYP_BYREF);
    }
#endif

    // Call the shared non gc static helper, as its the fastest
    return fgGetStaticsCCtorHelper(cls, info.compCompHnd->getSharedCCtorHelper(cls));
}

//------------------------------------------------------------------------------
// fgAddrCouldBeNull : Check whether the address tree can represent null.
//
// Arguments:
//    addr     -  Address to check
//
// Return Value:
//    True if address could be null; false otherwise
//
bool Compiler::fgAddrCouldBeNull(GenTree* addr)
{
    switch (addr->OperGet())
    {
        case GT_CNS_INT:
            return !addr->IsIconHandle();

        case GT_CNS_STR:
        case GT_FIELD_ADDR:
        case GT_LCL_ADDR:
            return false;

        case GT_IND:
            return (addr->gtFlags & GTF_IND_NONNULL) == 0;

        case GT_INDEX_ADDR:
            return !addr->AsIndexAddr()->IsNotNull();

        case GT_ARR_ADDR:
            return (addr->gtFlags & GTF_ARR_ADDR_NONNULL) == 0;

        case GT_BOX:
            return !addr->IsBoxedValue();

        case GT_LCL_VAR:
            return !lvaIsImplicitByRefLocal(addr->AsLclVar()->GetLclNum());

        case GT_COMMA:
            return fgAddrCouldBeNull(addr->AsOp()->gtOp2);

        case GT_CALL:
            return !addr->IsHelperCall() || !s_helperCallProperties.NonNullReturn(addr->AsCall()->GetHelperNum());

        case GT_ADD:
            if (addr->AsOp()->gtOp1->OperIs(GT_CNS_INT))
            {
                GenTree* cns1Tree = addr->AsOp()->gtOp1;
                if (!cns1Tree->IsIconHandle())
                {
                    if (!fgIsBigOffset(cns1Tree->AsIntCon()->gtIconVal))
                    {
                        // Op1 was an ordinary small constant
                        return fgAddrCouldBeNull(addr->AsOp()->gtOp2);
                    }
                }
                else // Op1 was a handle represented as a constant
                {
                    // Is Op2 also a constant?
                    if (addr->AsOp()->gtOp2->OperIs(GT_CNS_INT))
                    {
                        GenTree* cns2Tree = addr->AsOp()->gtOp2;
                        // Is this an addition of a handle and constant
                        if (!cns2Tree->IsIconHandle())
                        {
                            if (!fgIsBigOffset(cns2Tree->AsIntCon()->gtIconVal))
                            {
                                // Op2 was an ordinary small constant
                                return false; // we can't have a null address
                            }
                        }
                    }
                }
            }
            else
            {
                // Op1 is not a constant. What about Op2?
                if (addr->AsOp()->gtOp2->OperIs(GT_CNS_INT))
                {
                    GenTree* cns2Tree = addr->AsOp()->gtOp2;
                    // Is this an addition of a small constant
                    if (!cns2Tree->IsIconHandle())
                    {
                        if (!fgIsBigOffset(cns2Tree->AsIntCon()->gtIconVal))
                        {
                            // Op2 was an ordinary small constant
                            return fgAddrCouldBeNull(addr->AsOp()->gtOp1);
                        }
                    }
                }
            }
            break;

        default:
            break;
    }

    return true; // default result: addr could be null.
}

//------------------------------------------------------------------------------
// fgAddrCouldBeHeap: Check whether the address tree may represent a heap address.
//
// Arguments:
//    addr - Address to check
//
// Return Value:
//    True if address could be a heap address; false otherwise (i.e. stack, native memory, etc.)
//
bool Compiler::fgAddrCouldBeHeap(GenTree* addr)
{
    GenTree* op = addr;
    while (op->OperIs(GT_FIELD_ADDR) && op->AsFieldAddr()->IsInstance())
    {
        op = op->AsFieldAddr()->GetFldObj();
    }

    target_ssize_t offset;
    gtPeelOffsets(&op, &offset);

    // Ignore the offset for locals

    if (op->OperIs(GT_LCL_ADDR))
    {
        return false;
    }

    if (op->OperIsScalarLocal() && (op->AsLclVarCommon()->GetLclNum() == impInlineRoot()->info.compRetBuffArg))
    {
        // RetBuf is known to be on the stack
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
// fgOptimizeDelegateConstructor: try and optimize construction of a delegate
//
// Arguments:
//    call -- call to original delegate constructor
//    exactContextHnd -- [out] context handle to update
//    ldftnToken -- [in]  resolved token for the method the delegate will invoke,
//      if known, or nullptr if not known
//
// Return Value:
//    Original call tree if no optimization applies.
//    Updated call tree if optimized.

GenTree* Compiler::fgOptimizeDelegateConstructor(GenTreeCall*            call,
                                                 CORINFO_CONTEXT_HANDLE* ExactContextHnd,
                                                 methodPointerInfo*      ldftnToken)
{
    JITDUMP("\nfgOptimizeDelegateConstructor: ");
    noway_assert(call->gtCallType == CT_USER_FUNC);
    CORINFO_METHOD_HANDLE methHnd = call->gtCallMethHnd;
    CORINFO_CLASS_HANDLE  clsHnd  = info.compCompHnd->getMethodClass(methHnd);

    assert(call->gtArgs.HasThisPointer());
    assert(call->gtArgs.CountArgs() == 3);
    assert(!call->gtArgs.AreArgsComplete());
    GenTree* targetMethod = call->gtArgs.GetArgByIndex(2)->GetNode();
    noway_assert(targetMethod->TypeIs(TYP_I_IMPL));
    genTreeOps            oper            = targetMethod->OperGet();
    CORINFO_METHOD_HANDLE targetMethodHnd = nullptr;
    GenTree*              qmarkNode       = nullptr;
    if (oper == GT_FTN_ADDR)
    {
        GenTreeFptrVal* fptrValTree       = targetMethod->AsFptrVal();
        fptrValTree->gtFptrDelegateTarget = true;
        targetMethodHnd                   = fptrValTree->gtFptrMethod;
    }
    else if (oper == GT_CALL && targetMethod->AsCall()->gtCallMethHnd == eeFindHelper(CORINFO_HELP_VIRTUAL_FUNC_PTR))
    {
        assert(targetMethod->AsCall()->gtArgs.CountArgs() == 3);
        GenTree* handleNode = targetMethod->AsCall()->gtArgs.GetArgByIndex(2)->GetNode();

        if (handleNode->OperIs(GT_CNS_INT))
        {
            // it's a ldvirtftn case, fetch the methodhandle off the helper for ldvirtftn. It's the 3rd arg
            targetMethodHnd = CORINFO_METHOD_HANDLE(handleNode->AsIntCon()->gtCompileTimeHandle);
        }
        // Sometimes the argument to this is the result of a generic dictionary lookup, which shows
        // up as a GT_QMARK.
        else if (handleNode->OperIs(GT_QMARK))
        {
            qmarkNode = handleNode;
        }
    }
    // Sometimes we don't call CORINFO_HELP_VIRTUAL_FUNC_PTR but instead just call
    // CORINFO_HELP_RUNTIMEHANDLE_METHOD directly.
    else if (oper == GT_QMARK)
    {
        qmarkNode = targetMethod;
    }
    if (qmarkNode)
    {
        noway_assert(qmarkNode->OperIs(GT_QMARK));
        // The argument is actually a generic dictionary lookup.  For delegate creation it looks
        // like:
        // GT_QMARK
        //  GT_COLON
        //      op1 -> call
        //              Arg 1 -> token (has compile time handle)
        //      op2 -> lclvar
        //
        //
        // In this case I can find the token (which is a method handle) and that is the compile time
        // handle.
        noway_assert(qmarkNode->AsOp()->gtOp2->OperIs(GT_COLON));
        noway_assert(qmarkNode->AsOp()->gtOp2->AsOp()->gtOp1->OperIs(GT_CALL));
        GenTreeCall* runtimeLookupCall = qmarkNode->AsOp()->gtOp2->AsOp()->gtOp1->AsCall();

        // This could be any of CORINFO_HELP_RUNTIMEHANDLE_(METHOD|CLASS)(_LOG?)
        GenTree* tokenNode = runtimeLookupCall->gtArgs.GetArgByIndex(1)->GetNode();
        noway_assert(tokenNode->OperIs(GT_CNS_INT));
        targetMethodHnd = CORINFO_METHOD_HANDLE(tokenNode->AsIntCon()->gtCompileTimeHandle);
    }

    // Verify using the ldftnToken gives us all of what we used to get
    // via the above pattern match, and more...
    if (ldftnToken != nullptr)
    {
        assert(ldftnToken->m_token.hMethod != nullptr);

        if (targetMethodHnd != nullptr)
        {
            assert(targetMethodHnd == ldftnToken->m_token.hMethod);
        }

        targetMethodHnd = ldftnToken->m_token.hMethod;
    }
    else
    {
        assert(targetMethodHnd == nullptr);
    }

#ifdef FEATURE_READYTORUN
    if (IsAot())
    {
        if (IsTargetAbi(CORINFO_NATIVEAOT_ABI))
        {
            if (ldftnToken != nullptr)
            {
                JITDUMP("optimized\n");

                GenTree*       thisPointer       = call->gtArgs.GetThisArg()->GetNode();
                GenTree*       targetObjPointers = call->gtArgs.GetArgByIndex(1)->GetNode();
                CORINFO_LOOKUP pLookup;
                info.compCompHnd->getReadyToRunDelegateCtorHelper(&ldftnToken->m_token, ldftnToken->m_tokenConstraint,
                                                                  clsHnd, info.compMethodHnd, &pLookup);
                if (!pLookup.lookupKind.needsRuntimeLookup)
                {
                    call = gtNewHelperCallNode(CORINFO_HELP_READYTORUN_DELEGATE_CTOR, TYP_VOID, thisPointer,
                                               targetObjPointers);
                    call->setEntryPoint(pLookup.constLookup);
                }
                else
                {
                    assert(oper != GT_FTN_ADDR);

                    if (pLookup.lookupKind.runtimeLookupKind != CORINFO_LOOKUP_NOT_SUPPORTED)
                    {
                        CORINFO_CONST_LOOKUP genericLookup;
                        info.compCompHnd->getReadyToRunHelper(&ldftnToken->m_token, &pLookup.lookupKind,
                                                              CORINFO_HELP_READYTORUN_GENERIC_HANDLE,
                                                              info.compMethodHnd, &genericLookup);
                        GenTree* ctxTree = getRuntimeContextTree(pLookup.lookupKind.runtimeLookupKind);
                        call = gtNewHelperCallNode(CORINFO_HELP_READYTORUN_DELEGATE_CTOR, TYP_VOID, thisPointer,
                                                   targetObjPointers, ctxTree);
                        call->setEntryPoint(genericLookup);
                    }
                    else
                    {
                        // Runtime does not support inlining of all shapes of runtime lookups
                        // Inlining has to be aborted in such a case
                        assert(compIsForInlining());
                        compInlineResult->NoteFatal(InlineObservation::CALLSITE_GENERIC_DICTIONARY_LOOKUP);
                        JITDUMP("not optimized, generic inlining restriction\n");
                    }
                }
            }
            else
            {
                JITDUMP("not optimized, NATIVEAOT no ldftnToken\n");
            }
        }
        // ReadyToRun has this optimization for a non-virtual function pointers only for now.
        else if (oper == GT_FTN_ADDR)
        {
            JITDUMP("optimized\n");

            GenTree* thisPointer       = call->gtArgs.GetArgByIndex(0)->GetNode();
            GenTree* targetObjPointers = call->gtArgs.GetArgByIndex(1)->GetNode();
            call = gtNewHelperCallNode(CORINFO_HELP_READYTORUN_DELEGATE_CTOR, TYP_VOID, thisPointer, targetObjPointers);

            CORINFO_LOOKUP entryPoint;
            info.compCompHnd->getReadyToRunDelegateCtorHelper(&ldftnToken->m_token, ldftnToken->m_tokenConstraint,
                                                              clsHnd, info.compMethodHnd, &entryPoint);
            assert(!entryPoint.lookupKind.needsRuntimeLookup);
            call->setEntryPoint(entryPoint.constLookup);
        }
        else
        {
            JITDUMP("not optimized, R2R virtual case\n");
        }
    }
    else
#endif
        if (targetMethodHnd != nullptr)
    {
        CORINFO_METHOD_HANDLE alternateCtor = nullptr;
        DelegateCtorArgs      ctorData;
        ctorData.pMethod = info.compMethodHnd;
        ctorData.pArg3   = nullptr;
        ctorData.pArg4   = nullptr;
        ctorData.pArg5   = nullptr;

        alternateCtor = info.compCompHnd->GetDelegateCtor(methHnd, clsHnd, targetMethodHnd, &ctorData);
        if (alternateCtor != methHnd)
        {
            JITDUMP("optimized\n");
            // we erase any inline info that may have been set for generics has it is not needed here,
            // and in fact it will pass the wrong info to the inliner code
            *ExactContextHnd = nullptr;

            call->gtCallMethHnd = alternateCtor;

            CallArg* lastArg = nullptr;
            if (ctorData.pArg3 != nullptr)
            {
                GenTree* arg3 = gtNewIconHandleNode(size_t(ctorData.pArg3), GTF_ICON_FTN_ADDR);
                lastArg       = call->gtArgs.PushBack(this, NewCallArg::Primitive(arg3));
            }

            if (ctorData.pArg4 != nullptr)
            {
                GenTree* arg4 = gtNewIconHandleNode(size_t(ctorData.pArg4), GTF_ICON_FTN_ADDR);
                lastArg       = call->gtArgs.InsertAfter(this, lastArg, NewCallArg::Primitive(arg4));
            }

            if (ctorData.pArg5 != nullptr)
            {
                GenTree* arg5 = gtNewIconHandleNode(size_t(ctorData.pArg5), GTF_ICON_FTN_ADDR);
                lastArg       = call->gtArgs.InsertAfter(this, lastArg, NewCallArg::Primitive(arg5));
            }
        }
        else
        {
            JITDUMP("not optimized, no alternate ctor\n");
        }
    }
    else
    {
        JITDUMP("not optimized, no target method\n");
    }
    return call;
}

bool Compiler::fgCastNeeded(GenTree* tree, var_types toType)
{
    //
    // If tree is a relop and we need an 4-byte integer
    //  then we never need to insert a cast
    //
    if (tree->OperIsCompare() && (genActualType(toType) == TYP_INT))
    {
        return false;
    }

    var_types fromType;

    //
    // Is the tree as GT_CAST or a GT_CALL ?
    //
    if (tree->OperIs(GT_CAST))
    {
        fromType = tree->CastToType();
    }
    else if (tree->OperIs(GT_CALL))
    {
        fromType = (var_types)tree->AsCall()->gtReturnType;
    }
    else if (tree->OperIs(GT_LCL_VAR))
    {
        LclVarDsc* varDsc = lvaGetDesc(tree->AsLclVarCommon());
        if (varDsc->lvNormalizeOnStore())
        {
            fromType = varDsc->TypeGet();
        }
        else
        {
            fromType = tree->TypeGet();
        }
    }
    else
    {
        fromType = tree->TypeGet();
    }

    //
    // If both types are the same then an additional cast is not necessary
    //
    if (toType == fromType)
    {
        return false;
    }
    //
    // If the sign-ness of the two types are different then a cast is necessary, except for
    // an unsigned -> signed cast where we already know the sign bit is zero.
    //
    if (varTypeIsUnsigned(toType) != varTypeIsUnsigned(fromType))
    {
        bool isZeroExtension = varTypeIsUnsigned(fromType) && (genTypeSize(fromType) < genTypeSize(toType));
        if (!isZeroExtension)
        {
            return true;
        }
    }
    //
    // If the from type is the same size or smaller then an additional cast is not necessary
    //
    if (genTypeSize(toType) >= genTypeSize(fromType))
    {
        return false;
    }

    //
    // Looks like we will need the cast
    //
    return true;
}

GenTree* Compiler::fgGetCritSectOfStaticMethod()
{
    noway_assert(!compIsForInlining());

    noway_assert(info.compIsStatic); // This method should only be called for static methods.

    GenTree* tree = nullptr;

    CORINFO_LOOKUP_KIND kind;
    info.compCompHnd->getLocationOfThisType(info.compMethodHnd, &kind);

    if (!kind.needsRuntimeLookup)
    {
        CORINFO_OBJECT_HANDLE ptr = info.compCompHnd->getRuntimeTypePointer(info.compClassHnd);
        if (ptr != NO_OBJECT_HANDLE)
        {
            tree = gtNewIconEmbObjHndNode(ptr);
        }
        else
        {
            tree = gtNewIconEmbClsHndNode(info.compClassHnd);

            // Given the class handle, get the pointer to the Monitor.
            tree = gtNewHelperCallNode(CORINFO_HELP_GETSYNCFROMCLASSHANDLE, TYP_REF, tree);
        }
    }
    else
    {
        // Collectible types requires that for shared generic code, if we use the generic context parameter
        // that we report it. (This is a conservative approach, we could detect some cases particularly when the
        // context parameter is this that we don't need the eager reporting logic.)
        lvaGenericsContextInUse = true;

        switch (kind.runtimeLookupKind)
        {
            case CORINFO_LOOKUP_THISOBJ:
            {
                noway_assert(!"Should never get this for static method.");
                break;
            }

            case CORINFO_LOOKUP_CLASSPARAM:
            {
                // In this case, the hidden param is the class handle.
                tree = gtNewLclvNode(info.compTypeCtxtArg, TYP_I_IMPL);
                tree->gtFlags |= GTF_VAR_CONTEXT;
                break;
            }

            case CORINFO_LOOKUP_METHODPARAM:
            {
                // In this case, the hidden param is the method handle.
                tree = gtNewLclvNode(info.compTypeCtxtArg, TYP_I_IMPL);
                tree->gtFlags |= GTF_VAR_CONTEXT;
                // Call helper CORINFO_HELP_GETCLASSFROMMETHODPARAM to get the class handle
                // from the method handle.
                tree = gtNewHelperCallNode(CORINFO_HELP_GETCLASSFROMMETHODPARAM, TYP_I_IMPL, tree);
                break;
            }

            default:
            {
                noway_assert(!"Unknown LOOKUP_KIND");
                break;
            }
        }

        noway_assert(tree); // tree should now contain the CORINFO_CLASS_HANDLE for the exact class.

        // Given the class handle, get the pointer to the Monitor.
        tree = gtNewHelperCallNode(CORINFO_HELP_GETSYNCFROMCLASSHANDLE, TYP_REF, tree);
    }

    noway_assert(tree);
    return tree;
}

/*****************************************************************************
 *
 *  Add monitor enter/exit calls for synchronized methods, and a try/fault
 *  to ensure the 'exit' is called if the 'enter' was successful. On x86, we
 *  generate monitor enter/exit calls and tell the VM the code location of
 *  these calls. When an exception occurs between those locations, the VM
 *  automatically releases the lock. For non-x86 platforms, the JIT is
 *  responsible for creating a try/finally to protect the monitor enter/exit,
 *  and the VM doesn't need to know anything special about the method during
 *  exception processing -- it's just a normal try/finally.
 *
 *  We generate the following code:
 *
 *      void Foo()
 *      {
 *          unsigned byte acquired = 0;
 *          try {
 *              Monitor.Enter(<lock object>, &acquired);
 *
 *              *** all the preexisting user code goes here ***
 *
 *              Monitor.ExitIfTaken(<lock object>, &acquired);
 *          } fault {
 *              Monitor.ExitIfTaken(<lock object>, &acquired);
 *         }
 *      L_return:
 *         ret
 *      }
 *
 *  If the lock is actually acquired, then the 'acquired' variable is set to 1
 *  by the helper call. During normal exit, the finally is called, 'acquired'
 *  is 1, and the lock is released. If an exception occurs before the lock is
 *  acquired, but within the 'try' (extremely unlikely, but possible), 'acquired'
 *  will be 0, and the monitor exit call will quickly return without attempting
 *  to release the lock. Otherwise, 'acquired' will be 1, and the lock will be
 *  released during exception processing.
 *
 *  For synchronized methods, we generate a single return block.
 *  We can do this without creating additional "step" blocks because "ret" blocks
 *  must occur at the top-level (of the original code), not nested within any EH
 *  constructs. From the CLI spec, 12.4.2.8.2.3 "ret": "Shall not be enclosed in any
 *  protected block, filter, or handler." Also, 3.57: "The ret instruction cannot be
 *  used to transfer control out of a try, filter, catch, or finally block. From within
 *  a try or catch, use the leave instruction with a destination of a ret instruction
 *  that is outside all enclosing exception blocks."
 *
 *  In addition, we can add a "fault" at the end of a method and be guaranteed that no
 *  control falls through. From the CLI spec, section 12.4 "Control flow": "Control is not
 *  permitted to simply fall through the end of a method. All paths shall terminate with one
 *  of these instructions: ret, throw, jmp, or (tail. followed by call, calli, or callvirt)."
 *
 *  We only need to worry about "ret" and "throw", as the CLI spec prevents any other
 *  alternatives. Section 15.4.3.3 "Implementation information" states about exiting
 *  synchronized methods: "Exiting a synchronized method using a tail. call shall be
 *  implemented as though the tail. had not been specified." Section 3.37 "jmp" states:
 *  "The jmp instruction cannot be used to transferred control out of a try, filter,
 *  catch, fault or finally block; or out of a synchronized region." And, "throw" will
 *  be handled naturally; no additional work is required.
 */

void Compiler::fgAddSyncMethodEnterExit()
{
    assert(UsesFunclets());

    assert((info.compFlags & CORINFO_FLG_SYNCH) != 0);

    // We need to do this transformation before funclets are created.
    assert(!fgFuncletsCreated);

    // We need to update the bbPreds lists.
    assert(fgPredsComputed);

#if !FEATURE_EH
    // If we don't support EH, we can't add the EH needed by synchronized methods.
    // Of course, we could simply ignore adding the EH constructs, since we don't
    // support exceptions being thrown in this mode, but we would still need to add
    // the monitor enter/exit, and that doesn't seem worth it for this minor case.
    // By the time EH is working, we can just enable the whole thing.
    NYI("No support for synchronized methods");
#endif // !FEATURE_EH

    // Create a block for the start of the try region, where the monitor enter call
    // will go.
    BasicBlock* const tryBegBB  = fgSplitBlockAtBeginning(fgFirstBB);
    BasicBlock* const tryLastBB = fgLastBB;

    // Create a block for the fault.
    // It gets an artificial ref count.

    BasicBlock* faultBB = fgNewBBafter(BBJ_EHFAULTRET, tryLastBB, false);

    assert(tryLastBB->NextIs(faultBB));
    assert(faultBB->IsLast());
    assert(faultBB == fgLastBB);

    faultBB->bbRefs = 1;

    { // Scope the EH region creation

        // Add the new EH region at the end, since it is the least nested,
        // and thus should be last.

        EHblkDsc* newEntry = nullptr;
        unsigned  XTnew    = compHndBBtabCount;

        newEntry = fgTryAddEHTableEntries(XTnew);

        if (newEntry == nullptr)
        {
            IMPL_LIMITATION("too many exception clauses");
        }

        // Initialize the new entry

        newEntry->ebdID          = impInlineRoot()->compEHID++;
        newEntry->ebdHandlerType = EH_HANDLER_FAULT;

        newEntry->ebdTryBeg  = tryBegBB;
        newEntry->ebdTryLast = tryLastBB;

        newEntry->ebdHndBeg  = faultBB;
        newEntry->ebdHndLast = faultBB;

        newEntry->ebdTyp = 0; // unused for fault

        newEntry->ebdEnclosingTryIndex = EHblkDsc::NO_ENCLOSING_INDEX;
        newEntry->ebdEnclosingHndIndex = EHblkDsc::NO_ENCLOSING_INDEX;

        newEntry->ebdTryBegOffset    = tryBegBB->bbCodeOffs;
        newEntry->ebdTryEndOffset    = tryLastBB->bbCodeOffsEnd;
        newEntry->ebdFilterBegOffset = 0;
        newEntry->ebdHndBegOffset    = 0; // handler doesn't correspond to any IL
        newEntry->ebdHndEndOffset    = 0; // handler doesn't correspond to any IL

        // Set some flags on the new region. This is the same as when we set up
        // EH regions in fgFindBasicBlocks(). Note that the try has no enclosing
        // handler, and the fault has no enclosing try.

        tryBegBB->SetFlags(BBF_DONT_REMOVE | BBF_IMPORTED);

        faultBB->SetFlags(BBF_DONT_REMOVE | BBF_IMPORTED);
        faultBB->bbCatchTyp = BBCT_FAULT;

        tryBegBB->setTryIndex(XTnew);
        tryBegBB->clearHndIndex();

        faultBB->clearTryIndex();
        faultBB->setHndIndex(XTnew);

        // Walk the user code blocks and set all blocks that don't already have a try handler
        // to point to the new try handler.

        BasicBlock* tmpBB;
        for (tmpBB = tryBegBB->Next(); tmpBB != faultBB; tmpBB = tmpBB->Next())
        {
            if (!tmpBB->hasTryIndex())
            {
                tmpBB->setTryIndex(XTnew);
            }
        }

        // Walk the EH table. Make every EH entry that doesn't already have an enclosing
        // try index mark this new entry as their enclosing try index.

        unsigned  XTnum;
        EHblkDsc* HBtab;

        for (XTnum = 0, HBtab = compHndBBtab; XTnum < XTnew; XTnum++, HBtab++)
        {
            if (HBtab->ebdEnclosingTryIndex == EHblkDsc::NO_ENCLOSING_INDEX)
            {
                HBtab->ebdEnclosingTryIndex =
                    (unsigned short)XTnew; // This EH region wasn't previously nested, but now it is.
            }
        }

#ifdef DEBUG
        if (verbose)
        {
            JITDUMP("Synchronized method - created additional EH descriptor EH#%u for try/fault wrapping monitor "
                    "enter/exit\n",
                    XTnew);
            fgDispBasicBlocks();
            fgDispHandlerTab();
        }

        fgVerifyHandlerTab();
#endif // DEBUG
    }

    // Create a 'monitor acquired' boolean (actually, an unsigned byte: 1 = acquired, 0 = not acquired).
    // For EnC this is part of the frame header. Furthermore, this is allocated above PSP on ARM64.
    // To avoid complicated reasoning about alignment we always allocate a full pointer sized slot for this.
    var_types typeMonAcquired = TYP_I_IMPL;
    this->lvaMonAcquired      = lvaGrabTemp(true DEBUGARG("Synchronized method monitor acquired boolean"));

    lvaTable[lvaMonAcquired].lvType = typeMonAcquired;

    // Create IR to initialize the 'acquired' boolean.
    //
    if (!opts.IsOSR())
    {
        GenTree* zero     = gtNewZeroConNode(typeMonAcquired);
        GenTree* initNode = gtNewStoreLclVarNode(lvaMonAcquired, zero);

        fgNewStmtAtBeg(fgFirstBB, initNode);

#ifdef DEBUG
        if (verbose)
        {
            printf("\nSynchronized method - Add 'acquired' initialization in first block %s\n",
                   fgFirstBB->dspToString());
            gtDispTree(initNode);
            printf("\n");
        }
#endif
    }

    // Make a copy of the 'this' pointer to be used in the handler so it does
    // not inhibit enregistration of all uses of the variable. We cannot do
    // this optimization in EnC code as we would need to take care to save the
    // copy on EnC transitions, so guard this on optimizations being enabled.
    unsigned lvaCopyThis = BAD_VAR_NUM;
    if (opts.OptimizationEnabled() && !info.compIsStatic)
    {
        lvaCopyThis                  = lvaGrabTemp(true DEBUGARG("Synchronized method copy of this for handler"));
        lvaTable[lvaCopyThis].lvType = TYP_REF;

        GenTree* thisNode = gtNewLclVarNode(info.compThisArg);
        GenTree* initNode = gtNewStoreLclVarNode(lvaCopyThis, thisNode);

        fgNewStmtAtBeg(tryBegBB, initNode);
    }

    // For OSR, we do not need the enter tree as the monitor is acquired by the original method.
    //
    if (!opts.IsOSR())
    {
        fgCreateMonitorTree(lvaMonAcquired, info.compThisArg, tryBegBB, true /*enter*/);
    }

    // exceptional case
    fgCreateMonitorTree(lvaMonAcquired, lvaCopyThis != BAD_VAR_NUM ? lvaCopyThis : info.compThisArg, faultBB,
                        false /*exit*/);

    // non-exceptional cases
    for (BasicBlock* const block : Blocks())
    {
        if (block->KindIs(BBJ_RETURN))
        {
            fgCreateMonitorTree(lvaMonAcquired, info.compThisArg, block, false /*exit*/);
        }
    }
}

// fgCreateMonitorTree: Create tree to execute a monitor enter or exit operation for synchronized methods
//    lvaMonAcquired: lvaNum of boolean variable that tracks if monitor has been acquired.
//    lvaThisVar: lvaNum of variable being used as 'this' pointer, may not be the original one.  Is only used for
//    nonstatic methods
//    block: block to insert the tree in.  It is inserted at the end or in the case of a return, immediately before the
//    GT_RETURN
//    enter: whether to create a monitor enter or exit

GenTree* Compiler::fgCreateMonitorTree(unsigned lvaMonAcquired, unsigned lvaThisVar, BasicBlock* block, bool enter)
{
    // Insert the expression "enter/exitCrit(this, &acquired)" or "enter/exitCrit(handle, &acquired)"

    GenTree* varAddrNode = gtNewLclVarAddrNode(lvaMonAcquired);
    GenTree* tree;

    if (info.compIsStatic)
    {
        tree = fgGetCritSectOfStaticMethod();
    }
    else
    {
        tree = gtNewLclvNode(lvaThisVar, TYP_REF);
    }

    tree = gtNewHelperCallNode(enter ? CORINFO_HELP_MON_ENTER : CORINFO_HELP_MON_EXIT, TYP_VOID, tree, varAddrNode);

#ifdef DEBUG
    if (verbose)
    {
        printf("\nSynchronized method - Add monitor %s call to block %s\n", enter ? "enter" : "exit",
               block->dspToString());
        gtDispTree(tree);
        printf("\n");
    }
#endif

    if (enter)
    {
        fgNewStmtAtBeg(block, tree);
    }
    else
    {
        if (block->KindIs(BBJ_RETURN) && block->lastStmt()->GetRootNode()->OperIs(GT_RETURN))
        {
            GenTreeUnOp* retNode = block->lastStmt()->GetRootNode()->AsUnOp();
            GenTree*     retExpr = retNode->gtOp1;

            if (retExpr != nullptr)
            {
                // have to insert this immediately before the GT_RETURN so we transform:
                // ret(...) ->
                // ret(comma(comma(tmp=...,call mon_exit), tmp))
                //
                TempInfo tempInfo = fgMakeTemp(retExpr);
                GenTree* lclVar   = tempInfo.load;

                // TODO-1stClassStructs: delete this NO_CSE propagation. Requires handling multi-regs in copy prop.
                lclVar->gtFlags |= (retExpr->gtFlags & GTF_DONT_CSE);

                retExpr        = gtNewOperNode(GT_COMMA, lclVar->TypeGet(), tree, lclVar);
                retExpr        = gtNewOperNode(GT_COMMA, lclVar->TypeGet(), tempInfo.store, retExpr);
                retNode->gtOp1 = retExpr;
                retNode->AddAllEffectsFlags(retExpr);
            }
            else
            {
                // Insert this immediately before the GT_RETURN
                fgNewStmtNearEnd(block, tree);
            }
        }
        else
        {
            fgNewStmtAtEnd(block, tree);
        }
    }

    return tree;
}

// Convert a BBJ_RETURN block in a synchronized method to a BBJ_ALWAYS.
// We've previously added a 'try' block around the original program code using fgAddSyncMethodEnterExit().
// Thus, we put BBJ_RETURN blocks inside a 'try'. In IL this is illegal. Instead, we would
// see a 'leave' inside a 'try' that would get transformed into BBJ_CALLFINALLY/BBJ_CALLFINALLYRET blocks
// during importing, and the BBJ_CALLFINALLYRET would point at an outer block with the BBJ_RETURN.
// Here, we mimic some of the logic of importing a LEAVE to get the same effect for synchronized methods.
void Compiler::fgConvertSyncReturnToLeave(BasicBlock* block)
{
    assert(!fgFuncletsCreated);
    assert(info.compFlags & CORINFO_FLG_SYNCH);
    assert(genReturnBB != nullptr);
    assert(genReturnBB != block);
    assert(fgReturnCount <= 1); // We have a single return for synchronized methods
    assert(block->KindIs(BBJ_RETURN));
    assert(!block->HasFlag(BBF_HAS_JMP));
    assert(block->hasTryIndex());
    assert(!block->hasHndIndex());
    assert(compHndBBtabCount >= 1);

    unsigned tryIndex = block->getTryIndex();
    assert(tryIndex == compHndBBtabCount - 1); // The BBJ_RETURN must be at the top-level before we inserted the
                                               // try/finally, which must be the last EH region.

    EHblkDsc* ehDsc = ehGetDsc(tryIndex);
    assert(ehDsc->ebdEnclosingTryIndex == EHblkDsc::NO_ENCLOSING_INDEX); // There are no enclosing regions of the
                                                                         // BBJ_RETURN block
    assert(ehDsc->ebdEnclosingHndIndex == EHblkDsc::NO_ENCLOSING_INDEX);

    // Convert the BBJ_RETURN to BBJ_ALWAYS, jumping to genReturnBB.
    FlowEdge* const newEdge = fgAddRefPred(genReturnBB, block);
    block->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);

#ifdef DEBUG
    if (verbose)
    {
        printf("Synchronized method - convert block " FMT_BB " to BBJ_ALWAYS [targets " FMT_BB "]\n", block->bbNum,
               block->GetTarget()->bbNum);
    }
#endif
}

//------------------------------------------------------------------------
// fgAddReversePInvokeEnterExit: Add enter/exit calls for reverse PInvoke methods
//
// Arguments:
//      None.
//
// Return Value:
//      None.

void Compiler::fgAddReversePInvokeEnterExit()
{
    assert(opts.IsReversePInvoke());

    lvaReversePInvokeFrameVar = lvaGrabTempWithImplicitUse(false DEBUGARG("Reverse Pinvoke FrameVar"));

    LclVarDsc* varDsc = lvaGetDesc(lvaReversePInvokeFrameVar);
    lvaSetStruct(lvaReversePInvokeFrameVar, typGetBlkLayout(eeGetEEInfo()->sizeOfReversePInvokeFrame), false);

    // Add enter pinvoke exit callout at the start of prolog

    GenTree* pInvokeFrameVar = gtNewLclVarAddrNode(lvaReversePInvokeFrameVar);

    GenTree* tree;

    if (opts.jitFlags->IsSet(JitFlags::JIT_FLAG_TRACK_TRANSITIONS))
    {
        GenTree* stubArgument;
        if (info.compPublishStubParam)
        {
            // If we have a secret param for a Reverse P/Invoke, that means that we are in an IL stub.
            // In this case, the method handle we pass down to the Reverse P/Invoke helper should be
            // the target method, which is passed in the secret parameter.
            stubArgument = gtNewLclvNode(lvaStubArgumentVar, TYP_I_IMPL);
        }
        else
        {
            stubArgument = gtNewIconNode(0, TYP_I_IMPL);
        }

        tree = gtNewHelperCallNode(CORINFO_HELP_JIT_REVERSE_PINVOKE_ENTER_TRACK_TRANSITIONS, TYP_VOID, pInvokeFrameVar,
                                   gtNewIconEmbMethHndNode(info.compMethodHnd), stubArgument);
    }
    else
    {
        tree = gtNewHelperCallNode(CORINFO_HELP_JIT_REVERSE_PINVOKE_ENTER, TYP_VOID, pInvokeFrameVar);
    }

    fgNewStmtAtBeg(fgFirstBB, tree);

#ifdef DEBUG
    if (verbose)
    {
        printf("\nReverse PInvoke method - Add reverse pinvoke enter in first basic block %s\n",
               fgFirstBB->dspToString());
        gtDispTree(tree);
        printf("\n");
    }
#endif

    // Add reverse pinvoke exit callout at the end of epilog

    tree = gtNewLclVarAddrNode(lvaReversePInvokeFrameVar);

    CorInfoHelpFunc reversePInvokeExitHelper = opts.jitFlags->IsSet(JitFlags::JIT_FLAG_TRACK_TRANSITIONS)
                                                   ? CORINFO_HELP_JIT_REVERSE_PINVOKE_EXIT_TRACK_TRANSITIONS
                                                   : CORINFO_HELP_JIT_REVERSE_PINVOKE_EXIT;

    tree = gtNewHelperCallNode(reversePInvokeExitHelper, TYP_VOID, tree);

    assert(genReturnBB != nullptr);

    fgNewStmtNearEnd(genReturnBB, tree);

#ifdef DEBUG
    if (verbose)
    {
        printf("\nReverse PInvoke method - Add reverse pinvoke exit in return basic block %s\n",
               genReturnBB->dspToString());
        gtDispTree(tree);
        printf("\n");
    }
#endif
}

/*****************************************************************************
 *
 *  Return 'true' if there is more than one BBJ_RETURN block.
 */

bool Compiler::fgMoreThanOneReturnBlock()
{
    unsigned retCnt = 0;

    for (BasicBlock* const block : Blocks())
    {
        if (block->KindIs(BBJ_RETURN))
        {
            retCnt++;
            if (retCnt > 1)
            {
                return true;
            }
        }
    }

    return false;
}

namespace
{
// Define a helper class for merging return blocks (which we do when the input has
// more than the limit for this configuration).
//
// Notes: sets fgReturnCount, genReturnBB, and genReturnLocal.
class MergedReturns
{
public:
#ifdef JIT32_GCENCODER

    // X86 GC encoding has a hard limit of SET_EPILOGCNT_MAX epilogs.
    const static unsigned ReturnCountHardLimit = SET_EPILOGCNT_MAX;
#else  // JIT32_GCENCODER

    // We currently apply a hard limit of '4' to all other targets (see
    // the other uses of SET_EPILOGCNT_MAX), though it would be good
    // to revisit that decision based on CQ analysis.
    const static unsigned ReturnCountHardLimit = 4;
#endif // JIT32_GCENCODER

private:
    Compiler* comp;

    // As we discover returns, we'll record them in `returnBlocks`, until
    // the limit is reached, at which point we'll keep track of the merged
    // return blocks in `returnBlocks`.
    BasicBlock* returnBlocks[ReturnCountHardLimit];

    // Each constant value returned gets its own merged return block that
    // returns that constant (up to the limit on number of returns); in
    // `returnConstants` we track the constant values returned by these
    // merged constant return blocks.
    INT64 returnConstants[ReturnCountHardLimit];

    // Indicators of where in the lexical block list we'd like to place
    // each constant return block.
    BasicBlock* insertionPoints[ReturnCountHardLimit];

    // Number of return blocks allowed
    PhasedVar<unsigned> maxReturns;

    // Flag to keep track of when we've hit the limit of returns and are
    // actively merging returns together.
    bool mergingReturns = false;

public:
    MergedReturns(Compiler* comp)
        : comp(comp)
    {
        comp->fgReturnCount = 0;
    }

    void SetMaxReturns(unsigned value)
    {
        maxReturns = value;
        maxReturns.MarkAsReadOnly();
    }

    //------------------------------------------------------------------------
    // Record: Make note of a return block in the input program.
    //
    // Arguments:
    //    returnBlock - Block in the input that has jump kind BBJ_RETURN
    //
    // Notes:
    //    Updates fgReturnCount appropriately, and generates a merged return
    //    block if necessary.  If a constant merged return block is used,
    //    `returnBlock` is rewritten to jump to it.  If a non-constant return
    //    block is used, `genReturnBB` is set to that block, and `genReturnLocal`
    //    is set to the lclvar that it returns; morph will need to rewrite
    //    `returnBlock` to set the local and jump to the return block in such
    //    cases, which it will do after some key transformations like rewriting
    //    tail calls and calls that return to hidden buffers.  In either of these
    //    cases, `fgReturnCount` and the merged return block's profile information
    //    will be updated to reflect or anticipate the rewrite of `returnBlock`.
    //
    void Record(BasicBlock* returnBlock)
    {
        // Add this return to our tally
        unsigned oldReturnCount = comp->fgReturnCount++;

        if (!mergingReturns)
        {
            if (oldReturnCount < maxReturns)
            {
                // No need to merge just yet; simply record this return.
                returnBlocks[oldReturnCount] = returnBlock;
                return;
            }

            // We've reached our threshold
            mergingReturns = true;

            // Merge any returns we've already identified
            for (unsigned i = 0, searchLimit = 0; i < oldReturnCount; ++i)
            {
                BasicBlock* mergedReturnBlock = Merge(returnBlocks[i], searchLimit);
                if (returnBlocks[searchLimit] == mergedReturnBlock)
                {
                    // We've added a new block to the searchable set
                    ++searchLimit;
                }
            }
        }

        // We have too many returns, so merge this one in.
        // Search limit is new return count minus one (to exclude this block).
        unsigned searchLimit = comp->fgReturnCount - 1;
        Merge(returnBlock, searchLimit);
    }

    //------------------------------------------------------------------------
    // EagerCreate: Force creation of a non-constant merged return block `genReturnBB`.
    //
    // Return Value:
    //    The newly-created block which returns `genReturnLocal`.
    //
    BasicBlock* EagerCreate()
    {
        mergingReturns = true;
        return Merge(nullptr, 0);
    }

    //------------------------------------------------------------------------
    // PlaceReturns: Move any generated const return blocks to an appropriate
    //    spot in the lexical block list.
    //
    // Returns:
    //    True if any returns were impacted.
    //
    // Notes:
    //    Prematurely optimizing the block layout is unnecessary.
    //    However, 'ReturnCountHardLimit' is small enough such that
    //    any throughput savings from skipping this pass are negated
    //    by the need to emit branches to these blocks in MinOpts.
    //    If we decide to increase the number of epilogues allowed,
    //    we should consider removing this pass.
    //
    bool PlaceReturns()
    {
        if (!mergingReturns)
        {
            // No returns generated => no returns to place.
            return false;
        }

        for (unsigned index = 0; index < comp->fgReturnCount; ++index)
        {
            BasicBlock* returnBlock    = returnBlocks[index];
            BasicBlock* genReturnBlock = comp->genReturnBB;
            if (returnBlock == genReturnBlock)
            {
                continue;
            }

            BasicBlock* insertionPoint = insertionPoints[index];
            assert(insertionPoint != nullptr);

            comp->fgUnlinkBlock(returnBlock);
            comp->fgMoveBlocksAfter(returnBlock, returnBlock, insertionPoint);
            // Treat the merged return block as belonging to the same EH region
            // as the insertion point block, to make sure we don't break up
            // EH regions; since returning a constant won't throw, this won't
            // affect program behavior.
            comp->fgExtendEHRegionAfter(insertionPoint);
        }

        return true;
    }

private:
    //------------------------------------------------------------------------
    // CreateReturnBB: Create a basic block to serve as a merged return point, stored to
    //    `returnBlocks` at the given index, and optionally returning the given constant.
    //
    // Arguments:
    //    index - Index into `returnBlocks` to store the new block into.
    //    returnConst - Constant that the new block should return; may be nullptr to
    //      indicate that the new merged return is for the non-constant case, in which
    //      case, if the method's return type is non-void, `comp->genReturnLocal` will
    //      be initialized to a new local of the appropriate type, and the new block will
    //      return it.
    //
    // Return Value:
    //    The new merged return block.
    //
    BasicBlock* CreateReturnBB(unsigned index, GenTreeIntConCommon* returnConst = nullptr)
    {
        BasicBlock* newReturnBB = comp->fgNewBBinRegion(BBJ_RETURN);
        comp->fgReturnCount++;

        noway_assert(newReturnBB->IsLast());

        JITDUMP("\n newReturnBB [" FMT_BB "] created\n", newReturnBB->bbNum);

        GenTree* returnExpr;

        if (returnConst != nullptr)
        {
            returnExpr             = comp->gtNewOperNode(GT_RETURN, returnConst->gtType, returnConst);
            returnConstants[index] = returnConst->IntegralValue();
        }
        else if (comp->compMethodHasRetVal())
        {
            // There is a return value, so create a temp for it.  Real returns will store the value in there and
            // it'll be reloaded by the single return.
            unsigned retLclNum   = comp->lvaGrabTemp(true DEBUGARG("Single return block return value"));
            comp->genReturnLocal = retLclNum;
            LclVarDsc* retVarDsc = comp->lvaGetDesc(retLclNum);
            var_types  retLclType =
                comp->compMethodReturnsRetBufAddr() ? TYP_BYREF : genActualType(comp->info.compRetType);

            if (varTypeIsStruct(retLclType))
            {
                comp->lvaSetStruct(retLclNum, comp->info.compMethodInfo->args.retTypeClass, false);

                if (comp->compMethodReturnsMultiRegRetType())
                {
                    retVarDsc->lvIsMultiRegRet = true;
                }
            }
            else
            {
                retVarDsc->lvType = retLclType;
            }

            if (varTypeIsFloating(retVarDsc->TypeGet()))
            {
                comp->compFloatingPointUsed = true;
            }

#ifdef DEBUG
            // This temporary should not be converted to a double in stress mode,
            // because we introduce assigns to it after the stress conversion
            retVarDsc->lvKeepType = 1;
#endif

            GenTree* retTemp = comp->gtNewLclvNode(retLclNum, retVarDsc->TypeGet());

            // make sure copy prop ignores this node (make sure it always does a reload from the temp).
            retTemp->gtFlags |= GTF_DONT_CSE;
            returnExpr = comp->gtNewOperNode(GT_RETURN, retTemp->TypeGet(), retTemp);
        }
        else // Return void.
        {
            assert((comp->info.compRetType == TYP_VOID) || varTypeIsStruct(comp->info.compRetType));
            comp->genReturnLocal = BAD_VAR_NUM;

            returnExpr = new (comp, GT_RETURN) GenTreeOp(GT_RETURN, TYP_VOID);
        }

        // Add 'return' expression to the return block
        comp->fgNewStmtAtEnd(newReturnBB, returnExpr);
        // Flag that this 'return' was generated by return merging so that subsequent
        // return block merging will know to leave it alone.
        returnExpr->gtFlags |= GTF_RET_MERGED;

#ifdef DEBUG
        if (comp->verbose)
        {
            printf("\nmergeReturns statement tree ");
            Compiler::printTreeID(returnExpr);
            printf(" added to genReturnBB %s\n", newReturnBB->dspToString());
            comp->gtDispTree(returnExpr);
            printf("\n");
        }
#endif
        assert(index < maxReturns);
        returnBlocks[index] = newReturnBB;
        return newReturnBB;
    }

    //------------------------------------------------------------------------
    // Merge: Find or create an appropriate merged return block for the given input block.
    //
    // Arguments:
    //    returnBlock - Return block from the input program to find a merged return for.
    //                  May be nullptr to indicate that new block suitable for non-constant
    //                  returns should be generated but no existing block modified.
    //    searchLimit - Blocks in `returnBlocks` up to but not including index `searchLimit`
    //                  will be checked to see if we already have an appropriate merged return
    //                  block for this case.  If a new block must be created, it will be stored
    //                  to `returnBlocks` at index `searchLimit`.
    //
    // Return Value:
    //    Merged return block suitable for handling this return value.  May be newly-created
    //    or pre-existing.
    //
    // Notes:
    //    If a constant-valued merged return block is used, `returnBlock` will be rewritten to
    //    jump to the merged return block and its `GT_RETURN` statement will be removed.  If
    //    a non-constant-valued merged return block is used, `genReturnBB` and `genReturnLocal`
    //    will be set so that Morph can perform that rewrite, which it will do after some key
    //    transformations like rewriting tail calls and calls that return to hidden buffers.
    //    In either of these cases, `fgReturnCount` and the merged return block's profile
    //    information will be updated to reflect or anticipate the rewrite of `returnBlock`.
    //
    BasicBlock* Merge(BasicBlock* returnBlock, unsigned searchLimit)
    {
        assert(mergingReturns);

        BasicBlock* mergedReturnBlock = nullptr;

        // Do not look for mergeable constant returns in debug codegen as
        // we may lose track of sequence points.
        if ((returnBlock != nullptr) && (maxReturns > 1) && !comp->opts.compDbgCode)
        {
            // Check to see if this is a constant return so that we can search
            // for and/or create a constant return block for it.

            GenTreeIntConCommon* retConst = GetReturnConst(returnBlock);
            if (retConst != nullptr)
            {
                // We have a constant.  Now find or create a corresponding return block.

                unsigned    index;
                BasicBlock* constReturnBlock = FindConstReturnBlock(retConst, searchLimit, &index);

                if (constReturnBlock == nullptr)
                {
                    // We didn't find a const return block.  See if we have space left
                    // to make one.

                    // We have already allocated `searchLimit` slots.
                    unsigned slotsReserved = searchLimit;
                    if (comp->genReturnBB == nullptr)
                    {
                        // We haven't made a non-const return yet, so we have to reserve
                        // a slot for one.
                        ++slotsReserved;
                    }

                    if (slotsReserved < maxReturns)
                    {
                        // We have enough space to allocate a slot for this constant.
                        constReturnBlock = CreateReturnBB(searchLimit, retConst);
                    }
                }

                if (constReturnBlock != nullptr)
                {
                    // Found a constant merged return block.
                    mergedReturnBlock = constReturnBlock;

                    // Change BBJ_RETURN to BBJ_ALWAYS targeting const return block.
                    assert((comp->info.compFlags & CORINFO_FLG_SYNCH) == 0);
                    FlowEdge* const newEdge = comp->fgAddRefPred(constReturnBlock, returnBlock);
                    returnBlock->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);

                    // Remove GT_RETURN since constReturnBlock returns the constant.
                    assert(returnBlock->lastStmt()->GetRootNode()->OperIs(GT_RETURN));
                    assert(returnBlock->lastStmt()->GetRootNode()->gtGetOp1()->IsIntegralConst());
                    comp->fgRemoveStmt(returnBlock, returnBlock->lastStmt());

                    // Using 'returnBlock' as the insertion point for 'mergedReturnBlock'
                    // will give it a chance to use fallthrough rather than BBJ_ALWAYS.
                    // Resetting this after each merge ensures that any branches to the
                    // merged return block are lexically forward.

                    insertionPoints[index] = returnBlock;

                    // Update profile information in the mergedReturnBlock to
                    // reflect the additional flow.
                    //
                    if (returnBlock->hasProfileWeight())
                    {
                        weight_t const oldWeight =
                            mergedReturnBlock->hasProfileWeight() ? mergedReturnBlock->bbWeight : BB_ZERO_WEIGHT;
                        weight_t const newWeight = oldWeight + returnBlock->bbWeight;

                        JITDUMP("merging profile weight " FMT_WT " from " FMT_BB " to const return " FMT_BB "\n",
                                returnBlock->bbWeight, returnBlock->bbNum, mergedReturnBlock->bbNum);

                        mergedReturnBlock->setBBProfileWeight(newWeight);
                        DISPBLOCK(mergedReturnBlock);
                    }
                }
            }
        }

        if (mergedReturnBlock == nullptr)
        {
            // No constant return block for this return; use the general one.
            // We defer flow update and profile update to morph.
            //
            mergedReturnBlock = comp->genReturnBB;
            if (mergedReturnBlock == nullptr)
            {
                // No general merged return for this function yet; create one.
                // There had better still be room left in the array.
                assert(searchLimit < maxReturns);
                mergedReturnBlock = CreateReturnBB(searchLimit);
                comp->genReturnBB = mergedReturnBlock;
                // Downstream code expects the `genReturnBB` to always remain
                // once created, so that it can redirect flow edges to it.
                mergedReturnBlock->SetFlags(BBF_DONT_REMOVE);
            }
        }

        if (returnBlock != nullptr)
        {
            // Update fgReturnCount to reflect or anticipate that `returnBlock` will no longer
            // be a return point.
            comp->fgReturnCount--;
        }

        return mergedReturnBlock;
    }

    //------------------------------------------------------------------------
    // GetReturnConst: If the given block returns an integral constant, return the
    //     GenTreeIntConCommon that represents the constant.
    //
    // Arguments:
    //    returnBlock - Block whose return value is to be inspected.
    //
    // Return Value:
    //    GenTreeIntCommon that is the argument of `returnBlock`'s `GT_RETURN` if
    //    such exists; nullptr otherwise.
    //
    static GenTreeIntConCommon* GetReturnConst(BasicBlock* returnBlock)
    {
        Statement* lastStmt = returnBlock->lastStmt();
        if (lastStmt == nullptr)
        {
            return nullptr;
        }

        GenTree* lastExpr = lastStmt->GetRootNode();
        if (!lastExpr->OperIs(GT_RETURN))
        {
            return nullptr;
        }

        GenTree* retExpr = lastExpr->gtGetOp1();
        if ((retExpr == nullptr) || !retExpr->IsIntegralConst())
        {
            return nullptr;
        }

        return retExpr->AsIntConCommon();
    }

    //------------------------------------------------------------------------
    // FindConstReturnBlock: Scan the already-created merged return blocks, up to `searchLimit`,
    //     and return the one corresponding to the given const expression if it exists.
    //
    // Arguments:
    //    constExpr - GenTreeIntCommon representing the constant return value we're
    //        searching for.
    //    searchLimit - Check `returnBlocks`/`returnConstants` up to but not including
    //        this index.
    //    index - [out] Index of return block in the `returnBlocks` array, if found;
    //        searchLimit otherwise.
    //
    // Return Value:
    //    A block that returns the same constant, if one is found; otherwise nullptr.
    //
    BasicBlock* FindConstReturnBlock(GenTreeIntConCommon* constExpr, unsigned searchLimit, unsigned* index)
    {
        INT64 constVal = constExpr->IntegralValue();

        for (unsigned i = 0; i < searchLimit; ++i)
        {
            // Need to check both for matching const val and for genReturnBB
            // because genReturnBB is used for non-constant returns and its
            // corresponding entry in the returnConstants array is garbage.
            // Check the returnBlocks[] first, so we don't access an uninitialized
            // returnConstants[] value (which some tools like valgrind will
            // complain about).

            BasicBlock* returnBlock = returnBlocks[i];

            if (returnBlock == comp->genReturnBB)
            {
                continue;
            }

            if (returnConstants[i] == constVal)
            {
                *index = i;
                return returnBlock;
            }
        }

        *index = searchLimit;
        return nullptr;
    }
};
} // namespace

//------------------------------------------------------------------------
// fgAddInternal: add blocks and trees to express special method semantics
//
// Notes:
//   * rewrites shared generic catches in to filters
//   * adds code to handle modifiable this
//   * determines number of epilogs and merges returns
//   * does special setup for pinvoke/reverse pinvoke methods
//   * adds callouts and EH for synchronized methods
//   * adds just my code callback
//
// Returns:
//   Suitable phase status.
//
PhaseStatus Compiler::fgAddInternal()
{
    noway_assert(!compIsForInlining());

    bool madeChanges = false;

    // For runtime determined Exception types we're going to emit a fake EH filter with isinst for this
    // type with a runtime lookup
    madeChanges |= fgCreateFiltersForGenericExceptions();

    /*
    <BUGNUM> VSW441487 </BUGNUM>

    The "this" pointer is implicitly used in the following cases:
    1. Locking of synchronized methods
    2. Dictionary access of shared generics code
    3. If a method has "catch(FooException<T>)", the EH code accesses "this" to determine T.
    4. Initializing the type from generic methods which require precise cctor semantics
    5. Verifier does special handling of "this" in the .ctor

    However, we might overwrite it with a "starg 0".
    In this case, we will redirect all "ldarg(a)/starg(a) 0" to a temp lvaTable[lvaArg0Var]
    */

    if (!info.compIsStatic)
    {
        if (lvaArg0Var != info.compThisArg)
        {
            // When we're using the general encoder, we mark compThisArg address-taken to ensure that it is not
            // enregistered (since the decoder always reports a stack location for "this" for generics
            // context vars).
            bool lva0CopiedForGenericsCtxt;
#ifndef JIT32_GCENCODER
            lva0CopiedForGenericsCtxt = ((info.compMethodInfo->options & CORINFO_GENERICS_CTXT_FROM_THIS) != 0);
#else  // JIT32_GCENCODER
            lva0CopiedForGenericsCtxt = false;
#endif // JIT32_GCENCODER
            noway_assert(lva0CopiedForGenericsCtxt || !lvaTable[info.compThisArg].IsAddressExposed());
            noway_assert(!lvaTable[info.compThisArg].lvHasILStoreOp);
            noway_assert(lvaTable[lvaArg0Var].IsAddressExposed() || lvaTable[lvaArg0Var].lvHasILStoreOp ||
                         lva0CopiedForGenericsCtxt);

            // Now assign the original input "this" to the temp.
            GenTree* store = gtNewStoreLclVarNode(lvaArg0Var, gtNewLclVarNode(info.compThisArg));

            fgNewStmtAtBeg(fgFirstBB, store);

            JITDUMP("\nCopy \"this\" to lvaArg0Var in first basic block %s\n", fgFirstBB->dspToString());
            DISPTREE(store);
            JITDUMP("\n");

            madeChanges = true;
        }
    }

    // Merge return points if required or beneficial
    MergedReturns merger(this);

    // Add the synchronized method enter/exit calls and try/finally protection. Note
    // that this must happen before the one BBJ_RETURN block is created below, so the
    // BBJ_RETURN block gets placed at the top-level, not within an EH region. (Otherwise,
    // we'd have to be really careful when creating the synchronized method try/finally
    // not to include the BBJ_RETURN block.)
    if (UsesFunclets() && (info.compFlags & CORINFO_FLG_SYNCH) != 0)
    {
        fgAddSyncMethodEnterExit();
    }

    //
    //  We will generate just one epilog (return block)
    //   when we are asked to generate enter/leave callbacks
    //   or for methods with PInvoke
    //   or for methods calling into unmanaged code
    //   or for synchronized methods.
    //
    BasicBlock* lastBlockBeforeGenReturns = fgLastBB;
    if (compIsProfilerHookNeeded() || compMethodRequiresPInvokeFrame() || opts.IsReversePInvoke() ||
        ((info.compFlags & CORINFO_FLG_SYNCH) != 0))
    {
        // We will generate only one return block
        // We will transform the BBJ_RETURN blocks
        //  into jumps to the one return block
        //
        merger.SetMaxReturns(1);

        // Eagerly create the genReturnBB since the lowering of these constructs
        // will expect to find it.
        BasicBlock* mergedReturn = merger.EagerCreate();
        assert(mergedReturn == genReturnBB);
    }
    else
    {
        bool stressMerging = compStressCompile(STRESS_MERGED_RETURNS, 50);

        //
        // We are allowed to have multiple individual exits
        // However we can still decide to have a single return
        //
        if ((compCodeOpt() == SMALL_CODE) || stressMerging)
        {
            // Under stress or for Small_Code case we always
            // generate a single return block when we have multiple
            // return points
            //
            merger.SetMaxReturns(1);
        }
        else
        {
            unsigned limit = MergedReturns::ReturnCountHardLimit;
#ifdef JIT32_GCENCODER
            // For the jit32 GC encoder the limit is an actual hard limit. In
            // async functions we will be introducing another return during
            // the async transformation, so make sure there's a free epilog
            // for it.
            if (compIsAsync())
            {
                limit--;
            }
#endif
            merger.SetMaxReturns(limit);
        }
    }

    // Visit the BBJ_RETURN blocks and merge as necessary.

    for (BasicBlock* block = fgFirstBB; !lastBlockBeforeGenReturns->NextIs(block); block = block->Next())
    {
        if (block->KindIs(BBJ_RETURN) && !block->HasFlag(BBF_HAS_JMP))
        {
            merger.Record(block);
        }
    }

    madeChanges |= merger.PlaceReturns();

    if (compMethodRequiresPInvokeFrame())
    {
        // The P/Invoke helpers only require a frame variable, so only allocate the
        // TCB variable if we're not using them.
        if (!opts.ShouldUsePInvokeHelpers())
        {
            info.compLvFrameListRoot           = lvaGrabTemp(false DEBUGARG("Pinvoke FrameListRoot"));
            LclVarDsc* rootVarDsc              = lvaGetDesc(info.compLvFrameListRoot);
            rootVarDsc->lvType                 = TYP_I_IMPL;
            rootVarDsc->lvImplicitlyReferenced = 1;
        }

        lvaInlinedPInvokeFrameVar = lvaGrabTempWithImplicitUse(false DEBUGARG("Pinvoke FrameVar"));

        // Lowering::InsertPInvokeMethodProlog will create a call with this local addr as an argument.
        lvaSetVarAddrExposed(lvaInlinedPInvokeFrameVar DEBUGARG(AddressExposedReason::ESCAPE_ADDRESS));

        LclVarDsc* varDsc = lvaGetDesc(lvaInlinedPInvokeFrameVar);
        // Make room for the inlined frame.
        const CORINFO_EE_INFO* eeInfo = eeGetEEInfo();
        unsigned frameSize            = info.compPublishStubParam ? eeInfo->inlinedCallFrameInfo.sizeWithSecretStubArg
                                                                  : eeInfo->inlinedCallFrameInfo.size;
        lvaSetStruct(lvaInlinedPInvokeFrameVar, typGetBlkLayout(frameSize), false);
    }

    // Do we need to insert a "JustMyCode" callback?

    CORINFO_JUST_MY_CODE_HANDLE* pDbgHandle = nullptr;
    CORINFO_JUST_MY_CODE_HANDLE  dbgHandle  = nullptr;
    if (opts.compDbgCode && !opts.jitFlags->IsSet(JitFlags::JIT_FLAG_IL_STUB))
    {
        dbgHandle = info.compCompHnd->getJustMyCodeHandle(info.compMethodHnd, &pDbgHandle);
    }

    noway_assert(!dbgHandle || !pDbgHandle);

    if (dbgHandle || pDbgHandle)
    {
        // Test the JustMyCode VM global state variable
        GenTree* embNode        = gtNewIconEmbHndNode(dbgHandle, pDbgHandle, GTF_ICON_GLOBAL_PTR, info.compMethodHnd);
        GenTree* guardCheckVal  = gtNewIndir(TYP_INT, embNode);
        GenTree* guardCheckCond = gtNewOperNode(GT_EQ, TYP_INT, guardCheckVal, gtNewZeroConNode(TYP_INT));

        // Create the callback which will yield the final answer

        GenTree* callback = gtNewHelperCallNode(CORINFO_HELP_DBG_IS_JUST_MY_CODE, TYP_VOID);
        callback          = new (this, GT_COLON) GenTreeColon(TYP_VOID, gtNewNothingNode(), callback);

        // Stick the conditional call at the start of the method

        fgNewStmtAtBeg(fgFirstBB, gtNewQmarkNode(TYP_VOID, guardCheckCond, callback->AsColon()));

        madeChanges = true;
    }

#if defined(FEATURE_EH_WINDOWS_X86)

    /* Is this a 'synchronized' method? */

    if (!UsesFunclets() && (info.compFlags & CORINFO_FLG_SYNCH))
    {
        GenTree* tree = nullptr;

        /* Insert the expression "enterCrit(this)" or "enterCrit(handle)" */

        if (info.compIsStatic)
        {
            tree = fgGetCritSectOfStaticMethod();
        }
        else
        {
            noway_assert(lvaTable[info.compThisArg].lvType == TYP_REF);
            tree = gtNewLclvNode(info.compThisArg, TYP_REF);
        }

        tree = gtNewHelperCallNode(CORINFO_HELP_MON_ENTER, TYP_VOID, tree);

        fgNewStmtAtBeg(fgFirstBB, tree);

#ifdef DEBUG
        if (verbose)
        {
            printf("\nSynchronized method - Add enterCrit statement in first basic block %s\n",
                   fgFirstBB->dspToString());
            gtDispTree(tree);
            printf("\n");
        }
#endif

        /* We must be generating a single exit point for this to work */

        noway_assert(genReturnBB != nullptr);

        /* Create the expression "exitCrit(this)" or "exitCrit(handle)" */

        if (info.compIsStatic)
        {
            tree = fgGetCritSectOfStaticMethod();
        }
        else
        {
            tree = gtNewLclvNode(info.compThisArg, TYP_REF);
        }

        tree = gtNewHelperCallNode(CORINFO_HELP_MON_EXIT, TYP_VOID, tree);

        fgNewStmtNearEnd(genReturnBB, tree);

#ifdef DEBUG
        if (verbose)
        {
            printf("\nSynchronized method - Add exitCrit statement in single return block %s\n",
                   genReturnBB->dspToString());
            gtDispTree(tree);
            printf("\n");
        }
#endif

        // Reset cookies used to track start and end of the protected region in synchronized methods
        syncStartEmitCookie = nullptr;
        syncEndEmitCookie   = nullptr;
        madeChanges         = true;
    }

#endif // FEATURE_EH_WINDOWS_X86

    if (opts.IsReversePInvoke())
    {
        fgAddReversePInvokeEnterExit();
        madeChanges = true;
    }

#ifdef DEBUG
    if (verbose)
    {
        printf("\n*************** After fgAddInternal()\n");
        fgDispBasicBlocks();
        fgDispHandlerTab();
    }
#endif

    return madeChanges ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}

#ifdef SWIFT_SUPPORT
//------------------------------------------------------------------------
// fgAddSwiftErrorReturns: If this method uses Swift error handling,
// transform all GT_RETURN nodes into GT_SWIFT_ERROR_RET nodes
// to handle returning the error value alongside the normal return value.
// Also transform any GT_LCL_VAR uses of lvaSwiftErrorArg (the SwiftError* parameter)
// into GT_LCL_ADDR uses of lvaSwiftErrorLocal (the SwiftError pseudolocal).
//
// Returns:
//   Suitable phase status.
//
PhaseStatus Compiler::fgAddSwiftErrorReturns()
{
    if (lvaSwiftErrorArg == BAD_VAR_NUM)
    {
        // No Swift error handling in this method
        return PhaseStatus::MODIFIED_NOTHING;
    }

    assert(lvaSwiftErrorLocal != BAD_VAR_NUM);
    assert(info.compCallConv == CorInfoCallConvExtension::Swift);

    struct ReplaceSwiftErrorVisitor final : public GenTreeVisitor<ReplaceSwiftErrorVisitor>
    {
        enum
        {
            DoPreOrder    = true,
            DoLclVarsOnly = true,
        };

        ReplaceSwiftErrorVisitor(Compiler* comp)
            : GenTreeVisitor(comp)
        {
        }

        fgWalkResult PreOrderVisit(GenTree** use, GenTree* user)
        {
            if ((*use)->AsLclVarCommon()->GetLclNum() == m_compiler->lvaSwiftErrorArg)
            {
                if (!(*use)->OperIs(GT_LCL_VAR))
                {
                    BADCODE("Found invalid use of SwiftError* parameter");
                }

                *use = m_compiler->gtNewLclVarAddrNode(m_compiler->lvaSwiftErrorLocal, genActualType(*use));
            }

            return fgWalkResult::WALK_CONTINUE;
        }
    };

    ReplaceSwiftErrorVisitor visitor(this);

    for (BasicBlock* block : Blocks())
    {
        for (Statement* const stmt : block->Statements())
        {
            visitor.WalkTree(stmt->GetRootNodePointer(), nullptr);
        }

        if (block->KindIs(BBJ_RETURN))
        {
            GenTree* const ret = block->lastNode();
            assert(ret->OperIs(GT_RETURN));
            ret->SetOperRaw(GT_SWIFT_ERROR_RET);
            ret->AsOp()->gtOp2 = ret->AsOp()->gtOp1;

            // If this is the merged return block, use the merged return error local as the error operand.
            // Else, load the error value from the SwiftError pseudolocal (this will probably get promoted, anyway).
            if (block == genReturnBB)
            {
                assert(genReturnErrorLocal == BAD_VAR_NUM);
                genReturnErrorLocal = lvaGrabTemp(true DEBUGARG("Single return block SwiftError value"));
                lvaGetDesc(genReturnErrorLocal)->lvType = TYP_I_IMPL;
                ret->AsOp()->gtOp1                      = gtNewLclvNode(genReturnErrorLocal, TYP_I_IMPL);
            }
            else
            {
                ret->AsOp()->gtOp1 = gtNewLclFldNode(lvaSwiftErrorLocal, TYP_I_IMPL, 0);
            }
        }
    }

    return PhaseStatus::MODIFIED_EVERYTHING;
}
#endif // SWIFT_SUPPORT

/*****************************************************************************/
/*****************************************************************************/

PhaseStatus Compiler::fgFindOperOrder()
{
#ifdef DEBUG
    if (verbose)
    {
        printf("*************** In fgFindOperOrder()\n");
    }
#endif

    /* Walk the basic blocks and for each statement determine
     * the evaluation order, cost, FP levels, etc... */

    for (BasicBlock* const block : Blocks())
    {
        compCurBB = block;
        for (Statement* const stmt : block->Statements())
        {
            /* Recursively process the statement */

            compCurStmt = stmt;
            gtSetStmtInfo(stmt);
        }
    }

    return PhaseStatus::MODIFIED_EVERYTHING;
}

//------------------------------------------------------------------------
// fgSimpleLowerCastOfSmpOp: Optimization to remove CAST nodes from operands of some simple ops that are safe to do so
// since the upper bits do not affect the lower bits, and result of the simple op is zero/sign-extended via a CAST.
// Example:
//      CAST(ADD(CAST(x), CAST(y))) transforms to CAST(ADD(x, y))
//
// Returns:
//      True or false, representing changes were made.
//
// Notes:
//      This optimization could be done in morph, but it cannot because there are correctness
//      problems with NOLs (normalized-on-load locals) and how they are handled in VN.
//      Simple put, you cannot remove a CAST from CAST(LCL_VAR{nol}) in HIR.
//
//      Because the optimization happens during rationalization, turning into LIR, it is safe to remove the CAST.
//
bool Compiler::fgSimpleLowerCastOfSmpOp(LIR::Range& range, GenTreeCast* cast)
{
    GenTree*  castOp     = cast->CastOp();
    var_types castToType = cast->CastToType();
    var_types srcType    = castOp->TypeGet();

    assert(castOp->OperIsSimple());

    if (opts.OptimizationDisabled())
        return false;

    if (cast->gtOverflow())
        return false;

    if (castOp->OperMayOverflow() && castOp->gtOverflow())
        return false;

    // Only optimize if the castToType is a small integer type.
    // Only optimize if the srcType is an integer type.
    if (!varTypeIsSmall(castToType) || !varTypeIsIntegral(srcType))
        return false;

    // These are the only safe ops where the CAST is not necessary for the inputs.
    if (castOp->OperIs(GT_ADD, GT_SUB, GT_MUL, GT_AND, GT_XOR, GT_OR, GT_NOT, GT_NEG))
    {
        bool madeChanges = false;

        if (castOp->gtGetOp1()->OperIs(GT_CAST))
        {
            GenTreeCast* op1 = castOp->gtGetOp1()->AsCast();

            if (!op1->gtOverflow() && (genActualType(op1->CastOp()) == genActualType(srcType)) &&
                (castToType == op1->CastToType()))
            {
                // Removes the cast.
                castOp->AsOp()->gtOp1 = op1->CastOp();
                range.Remove(op1);
                madeChanges = true;
            }
        }

        if (castOp->OperIsBinary() && castOp->gtGetOp2()->OperIs(GT_CAST))
        {
            GenTreeCast* op2 = castOp->gtGetOp2()->AsCast();

            if (!op2->gtOverflow() && (genActualType(op2->CastOp()) == genActualType(srcType)) &&
                (castToType == op2->CastToType()))
            {
                // Removes the cast.
                castOp->AsOp()->gtOp2 = op2->CastOp();
                range.Remove(op2);
                madeChanges = true;
            }
        }

#ifdef DEBUG
        if (madeChanges)
        {
            JITDUMP("Lower - Cast of Simple Op %s:\n", GenTree::OpName(cast->OperGet()));
            DISPTREE(cast);
        }
#endif // DEBUG

        return madeChanges;
    }

    return false;
}

//------------------------------------------------------------------------
// fgSimpleLowerBswap16 : Optimization to remove CAST nodes from operands of small ops that depents on
// lower bits only (currently only BSWAP16).
// Example:
//      BSWAP16(CAST(x)) transforms to BSWAP16(x)
//
// Returns:
//      True or false, representing changes were made.
//
// Notes:
//      This optimization could be done in morph, but it cannot because there are correctness
//      problems with NOLs (normalized-on-load locals) and how they are handled in VN.
//      Simple put, you cannot remove a CAST from CAST(LCL_VAR{nol}) in HIR.
//
//      Because the optimization happens during rationalization, turning into LIR, it is safe to remove the CAST.
//
bool Compiler::fgSimpleLowerBswap16(LIR::Range& range, GenTree* op)
{
    assert(op->OperIs(GT_BSWAP16));

    if (opts.OptimizationDisabled())
        return false;

    // When openrand is a integral cast
    // When both source and target sizes are at least the operation size
    bool madeChanges = false;

    if (op->gtGetOp1()->OperIs(GT_CAST))
    {
        GenTreeCast* op1 = op->gtGetOp1()->AsCast();

        if (!op1->gtOverflow() && (genTypeSize(op1->CastToType()) >= 2) &&
            genActualType(op1->CastFromType()) == TYP_INT)
        {
            // This cast does not affect the lower 16 bits. It can be removed.
            op->AsOp()->gtOp1 = op1->CastOp();
            range.Remove(op1);
            madeChanges = true;
        }
    }

#ifdef DEBUG
    if (madeChanges)
    {
        JITDUMP("Lower - Downcast of Small Op %s:\n", GenTree::OpName(op->OperGet()));
        DISPTREE(op);
    }
#endif // DEBUG

    return madeChanges;
}

//------------------------------------------------------------------------------
// fgGetDomSpeculatively: Try determine a more accurate dominator than cached bbIDom
//
// Arguments:
//    block - Basic block to get a dominator for
//
// Return Value:
//    Basic block that dominates this block
//
BasicBlock* Compiler::fgGetDomSpeculatively(const BasicBlock* block)
{
    assert(m_domTree != nullptr);
    BasicBlock* lastReachablePred = nullptr;

    // Check if we have unreachable preds
    for (const FlowEdge* predEdge : block->PredEdges())
    {
        BasicBlock* predBlock = predEdge->getSourceBlock();
        if (predBlock == block)
        {
            continue;
        }

        // We check pred's count of InEdges - it's quite conservative.
        // We, probably, could use optReachable(fgFirstBb, pred) here to detect unreachable preds
        if (predBlock->countOfInEdges() > 0)
        {
            if (lastReachablePred != nullptr)
            {
                // More than one of "reachable" preds - return cached result
                return block->bbIDom;
            }
            lastReachablePred = predBlock;
        }
        else if (predBlock == block->bbIDom)
        {
            // IDom is unreachable, so assume this block is too.
            //
            return nullptr;
        }
    }

    return lastReachablePred == nullptr ? block->bbIDom : lastReachablePred;
}

//------------------------------------------------------------------------------
// fgLastBBInMainFunction: Return the last basic block in the main part of the function.
// With funclets, it is the block immediately before the first funclet.
//
BasicBlock* Compiler::fgLastBBInMainFunction()
{
    if (fgFirstFuncletBB != nullptr)
    {
        return fgFirstFuncletBB->Prev();
    }

    assert(fgLastBB->IsLast());
    return fgLastBB;
}

//------------------------------------------------------------------------------
// fgEndBBAfterMainFunction: Return the first basic block after the main part of the function.
// With funclets, it is the block of the first funclet. Otherwise it is nullptr if there are no
// funclets. This is equivalent to fgLastBBInMainFunction()->Next().
//
BasicBlock* Compiler::fgEndBBAfterMainFunction()
{
    if (fgFirstFuncletBB != nullptr)
    {
        return fgFirstFuncletBB;
    }

    assert(fgLastBB->IsLast());
    return nullptr;
}

/*****************************************************************************
 * Introduce a new head block of the handler for the prolog to be put in, ahead
 * of the current handler head 'block'.
 * Note that this code has some similarities to fgCreateLoopPreHeader().
 */

void Compiler::fgInsertFuncletPrologBlock(BasicBlock* block)
{
#ifdef DEBUG
    if (verbose)
    {
        printf("\nCreating funclet prolog header for " FMT_BB "\n", block->bbNum);
    }
#endif

    assert(UsesFunclets());
    assert(block->hasHndIndex());
    assert(fgFirstBlockOfHandler(block) == block); // this block is the first block of a handler

    /* Allocate a new basic block */

    BasicBlock* newHead = BasicBlock::New(this);
    newHead->SetFlags(BBF_INTERNAL);
    newHead->inheritWeight(block);
    newHead->bbRefs = 0;

    fgInsertBBbefore(block, newHead); // insert the new block in the block list
    fgExtendEHRegionBefore(block);    // Update the EH table to make the prolog block the first block in the block's EH
                                      // block.

    // Distribute the pred list between newHead and block. Incoming edges coming from outside
    // the handler go to the prolog. Edges coming from with the handler are back-edges, and
    // go to the existing 'block'.

    weight_t incomingWeight = BB_ZERO_WEIGHT;
    for (BasicBlock* const predBlock : block->PredBlocksEditing())
    {
        if (!fgIsIntraHandlerPred(predBlock, block))
        {
            // It's a jump from outside the handler; add it to the newHead preds list and remove
            // it from the block preds list.

            switch (predBlock->GetKind())
            {
                case BBJ_CALLFINALLY:
                {
                    noway_assert(predBlock->TargetIs(block));
                    fgRedirectEdge(predBlock->TargetEdgeRef(), newHead);
                    incomingWeight += predBlock->bbWeight;
                    break;
                }

                default:
                    // The only way into the handler is via a BBJ_CALLFINALLY (to a finally handler), or
                    // via exception handling.
                    unreached();
            }
        }
    }

    assert(fgGetPredForBlock(block, newHead) == nullptr);
    FlowEdge* const newEdge = fgAddRefPred(block, newHead);
    newHead->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);
    assert(newHead->JumpsToNext());

    // Update flow into the header block
    if (block->hasProfileWeight())
    {
        newHead->setBBProfileWeight(incomingWeight);
    }
}

//------------------------------------------------------------------------
// fgCreateFuncletPrologBlocks: create prolog blocks for funclets if needed
//
// Notes:
//   Every funclet will have a prolog. That prolog will be inserted as the first instructions
//   in the first block of the funclet. If the prolog is also the head block of a loop, we
//   would end up with the prolog instructions being executed more than once.
//   Check for this by searching the predecessor list for loops, and create a new prolog header
//   block when needed. We detect a loop by looking for any predecessor that isn't in the
//   handler's try region, since the only way to get into a handler is via that try region.
//
void Compiler::fgCreateFuncletPrologBlocks()
{
    assert(UsesFunclets());
    noway_assert(fgPredsComputed);
    assert(!fgFuncletsCreated);

    bool prologBlocksCreated = false;

    for (EHblkDsc* const HBtab : EHClauses(this))
    {
        BasicBlock* head = HBtab->ebdHndBeg;

        if (fgAnyIntraHandlerPreds(head))
        {
            // We need to create a new block in which to place the prolog, and split the existing
            // head block predecessor edges into those that should point to the prolog, and those
            // that shouldn't.
            //
            // It's arguable that we should just always do this, and not only when we "need to",
            // so there aren't two different code paths. However, it's unlikely to be necessary
            // for catch handlers because they have an incoming argument (the exception object)
            // that needs to get stored or saved, so back-arcs won't normally go to the head. It's
            // possible when writing in IL to generate a legal loop (e.g., push an Exception object
            // on the stack before jumping back to the catch head), but C# probably won't. This will
            // most commonly only be needed for finallys with a do/while loop at the top of the
            // finally.
            //
            // Note that we don't check filters. This might be a bug, but filters always have a filter
            // object live on entry, so it's at least unlikely (illegal?) that a loop edge targets the
            // filter head.

            fgInsertFuncletPrologBlock(head);
            prologBlocksCreated = true;
        }
    }

    if (prologBlocksCreated)
    {
        // If we've modified the graph, reset the 'modified' flag, since the dominators haven't
        // been computed.
        fgModified = false;

#if DEBUG
        if (verbose)
        {
            JITDUMP("\nAfter fgCreateFuncletPrologBlocks()");
            fgDispBasicBlocks();
            fgDispHandlerTab();
        }

        fgVerifyHandlerTab();
        fgDebugCheckBBlist();
#endif // DEBUG
    }
}

//------------------------------------------------------------------------
// fgCreateFunclets: create funclets for EH catch/finally/fault blocks.
//
// Returns:
//    Suitable phase status
//
// Notes:
//    We only move filter and handler blocks, not try blocks.
//
PhaseStatus Compiler::fgCreateFunclets()
{
    assert(UsesFunclets());
    assert(!fgFuncletsCreated);

    fgCreateFuncletPrologBlocks();

    unsigned           XTnum;
    EHblkDsc*          HBtab;
    const unsigned int funcCnt = ehFuncletCount() + 1;

    if (!FitsIn<unsigned short>(funcCnt))
    {
        IMPL_LIMITATION("Too many funclets");
    }

    FuncInfoDsc* funcInfo = new (this, CMK_BasicBlock) FuncInfoDsc[funcCnt];

    unsigned short funcIdx;

    // Setup the root FuncInfoDsc and prepare to start associating
    // FuncInfoDsc's with their corresponding EH region
    memset((void*)funcInfo, 0, funcCnt * sizeof(FuncInfoDsc));
    assert(funcInfo[0].funKind == FUNC_ROOT);
    funcIdx = 1;

    // Because we iterate from the top to the bottom of the compHndBBtab array, we are iterating
    // from most nested (innermost) to least nested (outermost) EH region. It would be reasonable
    // to iterate in the opposite order, but the order of funclets shouldn't matter.
    //
    // We move every handler region to the end of the function: each handler will become a funclet.
    //
    // Note that fgRelocateEHRange() can add new entries to the EH table. However, they will always
    // be added *after* the current index, so our iteration here is not invalidated.
    // It *can* invalidate the compHndBBtab pointer itself, though, if it gets reallocated!

    for (XTnum = 0; XTnum < compHndBBtabCount; XTnum++)
    {
        HBtab = ehGetDsc(XTnum); // must re-compute this every loop, since fgRelocateEHRange changes the table
        if (HBtab->HasFilter())
        {
            assert(funcIdx < funcCnt);
            funcInfo[funcIdx].funKind    = FUNC_FILTER;
            funcInfo[funcIdx].funEHIndex = (unsigned short)XTnum;
            funcIdx++;
        }
        assert(funcIdx < funcCnt);
        funcInfo[funcIdx].funKind    = FUNC_HANDLER;
        funcInfo[funcIdx].funEHIndex = (unsigned short)XTnum;
        HBtab->ebdFuncIndex          = funcIdx;
        funcIdx++;
        fgRelocateEHRange(XTnum, FG_RELOCATE_HANDLER);
    }

    // We better have populated all of them by now
    assert(funcIdx == funcCnt);

    // Publish
    compCurrFuncIdx   = 0;
    compFuncInfos     = funcInfo;
    compFuncInfoCount = (unsigned short)funcCnt;

    fgFuncletsCreated = true;

    return (compHndBBtabCount > 0) ? PhaseStatus::MODIFIED_EVERYTHING : PhaseStatus::MODIFIED_NOTHING;
}

//------------------------------------------------------------------------
// fgFuncletsAreCold: Determine if EH funclets can be moved to cold section.
//
// Notes:
//   Walk the EH funclet blocks of a function to determine if the funclet
//   section is cold. If any of the funclets are hot, then it may not be
//   beneficial to split at fgFirstFuncletBB and move all funclets to
//   the cold section.
//
bool Compiler::fgFuncletsAreCold()
{
    assert(UsesFunclets());

    for (BasicBlock* block = fgFirstFuncletBB; block != nullptr; block = block->Next())
    {
        if (!block->isRunRarely())
        {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------
// fgDetermineFirstColdBlock: figure out where we might split the block
//    list to put some blocks into the cold code section
//
// Returns:
//    Suitable phase status
//
// Notes:
//    Walk the basic blocks list to determine the first block to place in the
//    cold section. This would be the first of a series of rarely executed blocks
//    such that no succeeding blocks are in a try region or an exception handler
//    or are rarely executed.
//
PhaseStatus Compiler::fgDetermineFirstColdBlock()
{
    assert(fgFirstColdBlock == nullptr);

    if (!opts.compProcedureSplitting)
    {
        JITDUMP("No procedure splitting will be done for this method\n");
        return PhaseStatus::MODIFIED_NOTHING;
    }

#ifdef DEBUG
    if ((compHndBBtabCount > 0) && !opts.compProcedureSplittingEH)
    {
        JITDUMP("No procedure splitting will be done for this method with EH (by request)\n");
        return PhaseStatus::MODIFIED_NOTHING;
    }
#endif // DEBUG

    BasicBlock* firstColdBlock       = nullptr;
    BasicBlock* prevToFirstColdBlock = nullptr;
    BasicBlock* block;
    BasicBlock* lblk;

    bool forceSplit = false;

#ifdef DEBUG
    // If stress-splitting, split right after the first block
    forceSplit = JitConfig.JitStressProcedureSplitting();
#endif

    if (forceSplit)
    {
        firstColdBlock       = fgFirstBB->Next();
        prevToFirstColdBlock = fgFirstBB;
        JITDUMP("JitStressProcedureSplitting is enabled: Splitting after the first basic block\n");
    }
    else
    {
        bool inFuncletSection = false;

        for (lblk = nullptr, block = fgFirstBB; block != nullptr; lblk = block, block = block->Next())
        {
            // Make note of if we're in the funclet section,
            // so we can stop the search early.
            if (block == fgFirstFuncletBB)
            {
                inFuncletSection = true;
            }

            // Do we have a candidate for the first cold block?
            if (firstColdBlock != nullptr)
            {
                // We have a candidate for first cold block

                // Is this a hot block?
                if (!block->isRunRarely())
                {
                    // We have to restart the search for the first cold block
                    firstColdBlock       = nullptr;
                    prevToFirstColdBlock = nullptr;

                    // If we're already in the funclet section, try to split
                    // at fgFirstFuncletBB, and stop the search.
                    if (inFuncletSection)
                    {
                        if (fgFuncletsAreCold())
                        {
                            firstColdBlock       = fgFirstFuncletBB;
                            prevToFirstColdBlock = fgFirstFuncletBB->Prev();
                        }

                        break;
                    }
                }
            }
            else // (firstColdBlock == NULL) -- we don't have a candidate for first cold block
            {
                //
                // If a function has exception handling and we haven't found the first cold block yet,
                // consider splitting at the first funclet; do not consider splitting between funclets,
                // as this may break unwind info.
                //
                if (inFuncletSection)
                {
                    if (fgFuncletsAreCold())
                    {
                        firstColdBlock       = block;
                        prevToFirstColdBlock = lblk;
                    }

                    break;
                }

                // Is this a cold block?
                if (block->isRunRarely())
                {
                    //
                    // If the last block that was hot was a BBJ_COND
                    // then we will have to add an unconditional jump
                    // so the code size for block needs be large
                    // enough to make it worth our while
                    //
                    if ((lblk == nullptr) || !lblk->KindIs(BBJ_COND) || (fgGetCodeEstimate(block) >= 8))
                    {
                        // This block is now a candidate for first cold block
                        // Also remember the predecessor to this block
                        firstColdBlock       = block;
                        prevToFirstColdBlock = lblk;
                    }
                }
            }
        }
    }

    if (firstColdBlock == fgFirstBB)
    {
        // If the first block is Cold then we can't move any blocks
        // into the cold section

        firstColdBlock = nullptr;
    }

    if (firstColdBlock != nullptr)
    {
        noway_assert(prevToFirstColdBlock != nullptr);

        if (prevToFirstColdBlock == nullptr)
        {
            return PhaseStatus::MODIFIED_EVERYTHING; // To keep Prefast happy
        }

        // If we only have one cold block
        // then it may not be worth it to move it
        // into the Cold section as a jump to the
        // Cold section is 5 bytes in size.
        // Ignore if stress-splitting.
        //
        if (!forceSplit && firstColdBlock->IsLast())
        {
            // If the size of the cold block is 7 or less
            // then we will keep it in the Hot section.
            //
            if (fgGetCodeEstimate(firstColdBlock) < 8)
            {
                firstColdBlock = nullptr;
                goto EXIT;
            }
        }

        // Don't split up call/finally pairs
        if (prevToFirstColdBlock->isBBCallFinallyPair())
        {
            // Note that this assignment could make firstColdBlock == nullptr
            firstColdBlock = firstColdBlock->Next();
        }
    }

    for (block = firstColdBlock; block != nullptr; block = block->Next())
    {
        block->SetFlags(BBF_COLD);
    }

EXIT:;

#ifdef DEBUG
    if (verbose)
    {
        if (firstColdBlock)
        {
            printf("fgFirstColdBlock is " FMT_BB ".\n", firstColdBlock->bbNum);
        }
        else
        {
            printf("fgFirstColdBlock is NULL.\n");
        }
    }
#endif // DEBUG

    fgFirstColdBlock = firstColdBlock;

    return PhaseStatus::MODIFIED_EVERYTHING;
}

//------------------------------------------------------------------------
// acdHelper: map from special code kind to runtime helper
//
// Arguments:
//   codeKind - kind of special code desired
//
// Returns:
//   Helper to throw the correct exception
//
unsigned Compiler::acdHelper(SpecialCodeKind codeKind)
{
    switch (codeKind)
    {
        case SCK_RNGCHK_FAIL:
            return CORINFO_HELP_RNGCHKFAIL;
        case SCK_ARG_EXCPN:
            return CORINFO_HELP_THROW_ARGUMENTEXCEPTION;
        case SCK_ARG_RNG_EXCPN:
            return CORINFO_HELP_THROW_ARGUMENTOUTOFRANGEEXCEPTION;
        case SCK_DIV_BY_ZERO:
            return CORINFO_HELP_THROWDIVZERO;
        case SCK_ARITH_EXCPN:
            return CORINFO_HELP_OVERFLOW;
        case SCK_FAIL_FAST:
            return CORINFO_HELP_FAIL_FAST;
        default:
            assert(!"Bad codeKind");
            return 0;
    }
}

#ifdef DEBUG
//------------------------------------------------------------------------
// acdHelper: map from special code kind to descriptive name
//
// Arguments:
//   codeKind - kind of special code
//
// Returns:
//   String describing the special code kind
//
const char* sckName(SpecialCodeKind codeKind)
{
    switch (codeKind)
    {
        case SCK_RNGCHK_FAIL:
            return "SCK_RNGCHK_FAIL";
        case SCK_ARG_EXCPN:
            return "SCK_ARG_EXCPN";
        case SCK_ARG_RNG_EXCPN:
            return "SCK_ARG_RNG_EXCPN";
        case SCK_DIV_BY_ZERO:
            return "SCK_DIV_BY_ZERO";
        case SCK_ARITH_EXCPN:
            return "SCK_ARITH_EXCPN";
        case SCK_FAIL_FAST:
            return "SCK_FAIL_FAST";
        default:
            return "SCK_UNKNOWN";
    }
}
#endif

//------------------------------------------------------------------------
// fgGetAddCodeDscMap; create or return the add code desc map
//
// Returns:
//   add code desc map
//
Compiler::AddCodeDscMap* Compiler::fgGetAddCodeDscMap()
{
    if (fgAddCodeDscMap == nullptr)
    {
        fgAddCodeDscMap = new (getAllocator(CMK_Unknown)) AddCodeDscMap(getAllocator(CMK_Unknown));
    }
    return fgAddCodeDscMap;
}

//------------------------------------------------------------------------
// fgAddCodeRef: Indicate that a particular throw helper block will
//   be needed by the method.
//
// Arguments:
//   srcBlk  - the block that needs an entry;
//   kind    - the kind of exception;
//
// Notes:
//   You can call this method after throw helpers have been created,
//   but it will assert if this entails creation of a new helper.
//
void Compiler::fgAddCodeRef(BasicBlock* srcBlk, SpecialCodeKind kind)
{
    // Record that the code will call a THROW_HELPER
    // so on Windows Amd64 we can allocate the 4 outgoing
    // arg slots on the stack frame if there are no other calls.
    //
    compUsesThrowHelper = true;

    if (!fgUseThrowHelperBlocks() && (kind != SCK_FAIL_FAST))
    {
        // FailFast will still use a common throw helper, even in debuggable modes.
        //
        return;
    }

    // Fetch block data and designator
    //
    AcdKeyDesignator dsg     = AcdKeyDesignator::KD_NONE;
    unsigned const   refData = (kind == SCK_FAIL_FAST) ? 0 : bbThrowIndex(srcBlk, &dsg);

    // Look for an existing entry that matches what we're looking for
    //
    AddCodeDsc* add = fgFindExcptnTarget(kind, srcBlk);

    if (add != nullptr)
    {
        JITDUMP(FMT_BB " requires throw helper block for %s, sharing ACD%u (data 0x%08x)\n", srcBlk->bbNum,
                sckName(kind), add->acdNum, refData);
        return;
    }

    assert(!fgRngChkThrowAdded);

    // Allocate a new entry and prepend it to the list
    //
    add              = new (this, CMK_Unknown) AddCodeDsc;
    add->acdDstBlk   = nullptr;
    add->acdTryIndex = srcBlk->bbTryIndex;

    // For non-funclet EH we don't constrain ACD placement via handler regions
    add->acdHndIndex = UsesFunclets() ? srcBlk->bbHndIndex : 0;

    add->acdKeyDsg = dsg;
    add->acdKind   = kind;

    // This gets set true in the stack level setter
    // if there's still a need for this helper
    add->acdUsed = false;

#if !FEATURE_FIXED_OUT_ARGS
    add->acdStkLvl     = 0;
    add->acdStkLvlInit = false;
#endif // !FEATURE_FIXED_OUT_ARGS
    INDEBUG(add->acdNum = acdCount++);

    // Add to map
    //
    AddCodeDscMap* const map = fgGetAddCodeDscMap();
    AddCodeDscKey        key(add);
    assert(key.Data() == refData);
    map->Set(key, add);

    JITDUMP(FMT_BB " requires throw helper block for %s, created ACD%u with data 0x%08x\n", srcBlk->bbNum,
            sckName(kind), add->acdNum, key.Data());

#ifdef DEBUG
    // Verify we can re-lookup...
    AddCodeDscKey key2(kind, srcBlk, this);
    AddCodeDsc*   add2 = nullptr;
    assert(map->Lookup(key2, &add2));
    assert(add == add2);
#endif
}

//------------------------------------------------------------------------
// fgCreateThrowHelperBlocks: create the needed throw helpers
//
// Returns:
//   Suitable phase status
//
PhaseStatus Compiler::fgCreateThrowHelperBlocks()
{
    if (fgAddCodeDscMap == nullptr)
    {
        return PhaseStatus::MODIFIED_NOTHING;
    }

    // We should not have added throw helper blocks yet.
    //
    assert(!fgRngChkThrowAdded);

    const static BBKinds jumpKinds[] = {
        BBJ_ALWAYS, // SCK_NONE
        BBJ_THROW,  // SCK_RNGCHK_FAIL
        BBJ_THROW,  // SCK_DIV_BY_ZERO
        BBJ_THROW,  // SCK_ARITH_EXCP, SCK_OVERFLOW
        BBJ_THROW,  // SCK_ARG_EXCPN
        BBJ_THROW,  // SCK_ARG_RNG_EXCPN
        BBJ_THROW,  // SCK_FAIL_FAST
    };

    noway_assert(sizeof(jumpKinds) == SCK_COUNT); // sanity check

    for (AddCodeDsc* const add : AddCodeDscMap::ValueIteration(fgAddCodeDscMap))
    {
        // Create the target basic block in the region indicated by the acd info
        //
        assert(add->acdKind != SCK_NONE);
        bool const        putInFilter = (add->acdKeyDsg == AcdKeyDesignator::KD_FLT);
        BasicBlock* const newBlk      = fgNewBBinRegion(jumpKinds[add->acdKind], add->acdTryIndex, add->acdHndIndex,
                                                        /* nearBlk */ nullptr, putInFilter,
                                                        /* runRarely */ true, /* insertAtEnd */ true);

        // Update the descriptor so future lookups can find the block
        //
        add->acdDstBlk = newBlk;

#ifdef DEBUG
        if (verbose)
        {
            const char* msgWhere = "";
            switch (add->acdKeyDsg)
            {
                case AcdKeyDesignator::KD_NONE:
                    msgWhere = "non-EH region";
                    break;

                case AcdKeyDesignator::KD_HND:
                    msgWhere = "handler";
                    break;

                case AcdKeyDesignator::KD_TRY:
                    msgWhere = "try";
                    break;

                case AcdKeyDesignator::KD_FLT:
                    msgWhere = "filter";
                    break;

                default:
                    msgWhere = "? unexpected";
            }

            const char* msg;
            switch (add->acdKind)
            {
                case SCK_RNGCHK_FAIL:
                    msg = " for RNGCHK_FAIL";
                    break;
                case SCK_DIV_BY_ZERO:
                    msg = " for DIV_BY_ZERO";
                    break;
                case SCK_OVERFLOW:
                    msg = " for OVERFLOW";
                    break;
                case SCK_ARG_EXCPN:
                    msg = " for ARG_EXCPN";
                    break;
                case SCK_ARG_RNG_EXCPN:
                    msg = " for ARG_RNG_EXCPN";
                    break;
                case SCK_FAIL_FAST:
                    msg = " for FAIL_FAST";
                    break;
                default:
                    msg = " for ??";
                    break;
            }

            printf("\nAdding throw helper " FMT_BB " for ACD%u %s in %s%s\n", newBlk->bbNum, add->acdNum,
                   sckName(add->acdKind), msgWhere, msg);
        }
#endif // DEBUG

        // Mark the block as added by the compiler and not removable by future flow
        // graph optimizations. Note that no target block points to these blocks.
        //
        newBlk->SetFlags(BBF_IMPORTED | BBF_DONT_REMOVE);
    }

    fgRngChkThrowAdded = true;

    return PhaseStatus::MODIFIED_EVERYTHING;
}

//------------------------------------------------------------------------
// fgCreateThrowHelperBlockCode: create the code for throw helper blocks
//
void Compiler::fgCreateThrowHelperBlockCode(AddCodeDsc* add)
{
    assert(add->acdUsed);

    // Find the block created earlier. It should be empty.
    //
    BasicBlock* const block = add->acdDstBlk;
    assert(block->isEmpty());

    // Figure out what code to insert
    //
    int helper = CORINFO_HELP_UNDEF;

    switch (add->acdKind)
    {
        case SCK_RNGCHK_FAIL:
            helper = CORINFO_HELP_RNGCHKFAIL;
            break;

        case SCK_DIV_BY_ZERO:
            helper = CORINFO_HELP_THROWDIVZERO;
            break;

        case SCK_ARITH_EXCPN:
            helper = CORINFO_HELP_OVERFLOW;
            noway_assert(SCK_OVERFLOW == SCK_ARITH_EXCPN);
            break;

        case SCK_ARG_EXCPN:
            helper = CORINFO_HELP_THROW_ARGUMENTEXCEPTION;
            break;

        case SCK_ARG_RNG_EXCPN:
            helper = CORINFO_HELP_THROW_ARGUMENTOUTOFRANGEEXCEPTION;
            break;

        case SCK_FAIL_FAST:
            helper = CORINFO_HELP_FAIL_FAST;
            break;

        default:
            noway_assert(!"unexpected code addition kind");
    }

    noway_assert(helper != CORINFO_HELP_UNDEF);

    // Add the appropriate helper call.
    //
    GenTreeCall* tree = gtNewHelperCallNode(helper, TYP_VOID);

    // There are no args here but fgMorphArgs has side effects
    // such as setting the outgoing arg area (which is necessary
    // on AMD if there are any calls).
    //
    tree = fgMorphArgs(tree);

    // Store the tree in the new basic block.
    //
    if (fgNodeThreading != NodeThreading::LIR)
    {
        fgInsertStmtAtEnd(block, fgNewStmtFromTree(tree));
    }
    else
    {
        LIR::AsRange(block).InsertAtEnd(tree);
        LIR::ReadOnlyRange range(tree, tree);
        m_pLowering->LowerRange(block, range);
    }
}

//------------------------------------------------------------------------
// fgFindExcptnTarget: finds the block to jump to that will throw a given kind of exception
//
// Arguments:
//    kind -- kind of exception to throw
//    fromBlock -- block that will jump to the throw helper
//
// Return Value:
//    Code descriptor for the appropriate throw helper block, or nullptr if no such
//    descriptor exists.
//
Compiler::AddCodeDsc* Compiler::fgFindExcptnTarget(SpecialCodeKind kind, BasicBlock* fromBlock)
{
    assert(fgUseThrowHelperBlocks() || (kind == SCK_FAIL_FAST));
    AddCodeDsc*          add = nullptr;
    AddCodeDscMap* const map = fgGetAddCodeDscMap();
    AddCodeDscKey        key(kind, fromBlock, this);
    map->Lookup(key, &add);

    if (add == nullptr)
    {
        // We shouldn't be asking for these blocks late in compilation
        // unless we know there are entries to be found.
        if (fgRngChkThrowAdded)
        {
            JITDUMP(FMT_BB ": unexpected request for new throw helper: kind %d (%s), data 0x%08x\n", fromBlock->bbNum,
                    kind, sckName(kind), key.Data());
        }
        assert(!fgRngChkThrowAdded);
    }

    return add;
}

//------------------------------------------------------------------------
// bbThrowIndex: find acd map key for a given block
//
// Arguments:
//    blk -- block that may eventually throw an exception
//    dsg [out] -- designator for which region controls throw block placement
//
// Return Value:
//    encoded region value to use in acd key formation
//
unsigned Compiler::bbThrowIndex(BasicBlock* blk, AcdKeyDesignator* dsg)
{
    if (!UsesFunclets())
    {
        if (blk->hasTryIndex())
        {
            *dsg = AcdKeyDesignator::KD_TRY;
        }
        else
        {
            *dsg = AcdKeyDesignator::KD_NONE;
        }
        return blk->bbTryIndex;
    }

    const unsigned tryIndex = blk->bbTryIndex;
    const unsigned hndIndex = blk->bbHndIndex;
    const bool     inTry    = tryIndex > 0;
    const bool     inHnd    = hndIndex > 0;

    if (!inTry && !inHnd)
    {
        *dsg = AcdKeyDesignator::KD_NONE;
        return 0;
    }

    assert(inTry || inHnd);

    if (inTry && (!inHnd || (tryIndex < hndIndex)))
    {
        // The most enclosing region is a try body, use it
        assert(tryIndex <= 0x3FFFFFFF);
        *dsg = AcdKeyDesignator::KD_TRY;
        return tryIndex;
    }

    // The most enclosing region is a handler which will be a funclet
    // Now we have to figure out if blk is in the filter or handler
    assert(hndIndex <= 0x3FFFFFFF);
    assert(hndIndex >= 1);
    if (ehGetDsc(hndIndex - 1)->InFilterRegionBBRange(blk))
    {
        *dsg = AcdKeyDesignator::KD_FLT;
        return hndIndex | 0x80000000;
    }

    *dsg = AcdKeyDesignator::KD_HND;
    return hndIndex | 0x40000000;
}

//------------------------------------------------------------------------
// AddCodedDscKey: construct from kind and block
//
// Arguments:
//    kind - exception kind
//    block - block throwing (or potentially throwing) an exception
//
// Returns:
//    appropriate lookup key
//
Compiler::AddCodeDscKey::AddCodeDscKey(SpecialCodeKind kind, BasicBlock* block, Compiler* comp)
    : acdKind(kind)
{
    if (acdKind == SCK_FAIL_FAST)
    {
        acdData = 0;
    }
    else
    {
        AcdKeyDesignator dsg;
        acdData = comp->bbThrowIndex(block, &dsg);
    }
}

//------------------------------------------------------------------------
// AddCodedDscKey: construct from AddCodeDsc
//
// Arguments:
//    add - add code dsc in querstion
//
// Returns:
//    appropriate lookup key
//
Compiler::AddCodeDscKey::AddCodeDscKey(AddCodeDsc* add)
    : acdKind(add->acdKind)
{
    if (acdKind == SCK_FAIL_FAST)
    {
        acdData = 0;
    }
    else
    {
        switch (add->acdKeyDsg)
        {
            case AcdKeyDesignator::KD_NONE:
                acdData = 0;
                break;
            case AcdKeyDesignator::KD_TRY:
                acdData = add->acdTryIndex;
                break;
            case AcdKeyDesignator::KD_HND:
                acdData = add->acdHndIndex | 0x40000000;
                break;
            case AcdKeyDesignator::KD_FLT:
                acdData = add->acdHndIndex | 0x80000000;
                break;
            default:
                unreached();
        }
    }
}

//------------------------------------------------------------------------
// UpdateKeyDesignator: determine new key designator after modifying
//   the region indices.
//
// Arguments:
//   compiler - current compiler instance
//
// Returns:
//   true if the key desinator changes
//
bool Compiler::AddCodeDsc::UpdateKeyDesignator(Compiler* compiler)
{
    // This ACD may now have a new enclosing region.
    // Figure out the new parent key designator.
    //
    // For example, suppose there is a try that has an array
    // bounds check and an empty finally, all within a
    // finally. When we remove the try, the ACD for the bounds
    // check changes from being enclosed in a try to being
    // enclosed in a finally.
    //
    // Filter ACDs should always remain in filter regions.
    //
    const bool inHnd = acdHndIndex > 0;
    const bool inTry = acdTryIndex > 0;

    AcdKeyDesignator newDsg = AcdKeyDesignator::KD_NONE;

    if (!compiler->UsesFunclets())
    {
        // Non-funclet case
        //
        assert(acdKeyDsg != AcdKeyDesignator::KD_FLT);
        newDsg = inTry ? AcdKeyDesignator::KD_TRY : AcdKeyDesignator::KD_NONE;
    }
    else if (!inTry && !inHnd)
    {
        // Moved outside of all EH regions.
        //
        assert(acdKeyDsg != AcdKeyDesignator::KD_FLT);
        newDsg = AcdKeyDesignator::KD_NONE;
    }
    else if (inTry && (!inHnd || (acdTryIndex < acdHndIndex)))
    {
        // Moved into a parent try region.
        //
        assert(acdKeyDsg != AcdKeyDesignator::KD_FLT);
        newDsg = AcdKeyDesignator::KD_TRY;
    }
    else
    {
        // Moved into a parent or renumbered handler or filter region.
        //
        if (acdKeyDsg == AcdKeyDesignator::KD_FLT)
        {
            newDsg = AcdKeyDesignator::KD_FLT;
        }
        else
        {
            newDsg = AcdKeyDesignator::KD_HND;
        }
    }

    bool result = (newDsg != acdKeyDsg);
    acdKeyDsg   = newDsg;

    return result;
}

#ifdef DEBUG
//------------------------------------------------------------------------
// Dump: dump info about an AddCodeDesc
//
void Compiler::AddCodeDsc::Dump()
{
    printf("ACD%u %s ", acdNum, sckName(acdKind));
    switch (acdKeyDsg)
    {
        case AcdKeyDesignator::KD_NONE:
            printf("in method region");
            break;
        case AcdKeyDesignator::KD_TRY:
            printf("in try region of EH#%u", acdTryIndex - 1);
            break;
        case AcdKeyDesignator::KD_HND:
            printf("in handler region of EH#%u", acdHndIndex - 1);
            break;
        case AcdKeyDesignator::KD_FLT:
            printf("in filter region of EH#%u", acdHndIndex - 1);
            break;
        default:
            printf("(unexpected region)");
            break;
    }

    AddCodeDscKey key(this);
    printf(" map key 0x%x\n", key.Data());
}
#endif

//------------------------------------------------------------------------
// fgSetTreeSeq: Sequence the tree, setting the "gtPrev" and "gtNext" links.
//
// Also sets the sequence numbers for dumps. The last and first node of
// the resulting "range" will have their "gtNext" and "gtPrev" links set
// to "nullptr".
//
// Arguments:
//    tree  - the tree to sequence
//    isLIR - whether the sequencing is being done for LIR. If so, the
//            GTF_REVERSE_OPS flag will be cleared on all nodes.
//
// Return Value:
//    The first node to execute in the sequenced tree.
//
GenTree* Compiler::fgSetTreeSeq(GenTree* tree, bool isLIR)
{
    class SetTreeSeqVisitor final : public GenTreeVisitor<SetTreeSeqVisitor>
    {
        GenTree*   m_prevNode;
        const bool m_isLIR;

    public:
        enum
        {
            DoPostOrder       = true,
            UseExecutionOrder = true
        };

        SetTreeSeqVisitor(Compiler* compiler, GenTree* tree, bool isLIR)
            : GenTreeVisitor<SetTreeSeqVisitor>(compiler)
            , m_prevNode(tree)
            , m_isLIR(isLIR)
        {
            INDEBUG(tree->gtSeqNum = 0);
        }

        fgWalkResult PostOrderVisit(GenTree** use, GenTree* user)
        {
            GenTree* node = *use;

            if (m_isLIR)
            {
                node->ClearReverseOp();
            }

            node->gtPrev       = m_prevNode;
            m_prevNode->gtNext = node;

            INDEBUG(node->gtSeqNum = m_prevNode->gtSeqNum + 1);

            m_prevNode = node;

            return fgWalkResult::WALK_CONTINUE;
        }

        GenTree* Sequence()
        {
            // We have set "m_prevNode" to "tree" in the constructor - this will give us a
            // circular list here ("tree->gtNext == firstNode", "firstNode->gtPrev == tree").
            GenTree* tree = m_prevNode;
            WalkTree(&tree, nullptr);
            assert(tree == m_prevNode);

            // Extract the first node in the sequence and break the circularity.
            GenTree* lastNode  = tree;
            GenTree* firstNode = lastNode->gtNext;
            lastNode->gtNext   = nullptr;
            firstNode->gtPrev  = nullptr;

            return firstNode;
        }
    };

#ifdef DEBUG
    if (isLIR)
    {
        assert((fgNodeThreading == NodeThreading::LIR) || (mostRecentlyActivePhase == PHASE_RATIONALIZE));
    }
    else
    {
        assert((fgNodeThreading == NodeThreading::AllTrees) || (mostRecentlyActivePhase == PHASE_SET_BLOCK_ORDER));
    }
#endif
    return SetTreeSeqVisitor(this, tree, isLIR).Sequence();
}

//------------------------------------------------------------------------------
// fgSetBlockOrder: Determine the interruptibility model of the method and
// thread the IR.
//
PhaseStatus Compiler::fgSetBlockOrder()
{
    JITDUMP("*************** In fgSetBlockOrder()\n");

#ifdef DEBUG
    BasicBlock::s_nMaxTrees = 0;
#endif

    if (fgHasCycleWithoutGCSafePoint())
    {
        JITDUMP("Marking method as fully interruptible\n");
        SetInterruptible(true);
    }

    for (BasicBlock* const block : Blocks())
    {
        fgSetBlockOrder(block);
    }

    JITDUMP("The biggest BB has %4u tree nodes\n", BasicBlock::s_nMaxTrees);

    // Return "everything" to enable consistency checking of the statement links during post phase.
    //
    return PhaseStatus::MODIFIED_EVERYTHING;
}

class GCSafePointSuccessorEnumerator
{
    BasicBlock* m_block;
    union
    {
        BasicBlock*  m_successors[2];
        BasicBlock** m_pSuccessors;
    };

    unsigned m_numSuccs;
    unsigned m_curSucc = UINT_MAX;

public:
    // Constructs an enumerator of successors to be used for checking for GC
    // safe point cycles.
    GCSafePointSuccessorEnumerator(Compiler* comp, BasicBlock* block)
        : m_block(block)
    {
        m_numSuccs = 0;
        block->VisitRegularSuccs(comp, [this](BasicBlock* succ) {
            if (m_numSuccs < ArrLen(m_successors))
            {
                m_successors[m_numSuccs] = succ;
            }

            m_numSuccs++;
            return BasicBlockVisit::Continue;
        });

        if (m_numSuccs == 0)
        {
            if (block->endsWithTailCallOrJmp(comp, true))
            {
                // This tail call might combine with other tail calls to form a
                // loop. Add a pseudo successor back to the entry to model
                // this.
                m_successors[0] = comp->fgFirstBB;
                m_numSuccs      = 1;
                return;
            }
        }
        else
        {
            assert(!block->endsWithTailCallOrJmp(comp, true));
        }

        if (m_numSuccs > ArrLen(m_successors))
        {
            m_pSuccessors = new (comp, CMK_BasicBlock) BasicBlock*[m_numSuccs];

            unsigned numSuccs = 0;
            block->VisitRegularSuccs(comp, [this, &numSuccs](BasicBlock* succ) {
                assert(numSuccs < m_numSuccs);
                m_pSuccessors[numSuccs++] = succ;
                return BasicBlockVisit::Continue;
            });

            assert(numSuccs == m_numSuccs);
        }
    }

    // Gets the block whose successors are enumerated.
    BasicBlock* Block()
    {
        return m_block;
    }

    // Returns the next available successor or `nullptr` if there are no more successors.
    BasicBlock* NextSuccessor()
    {
        m_curSucc++;
        if (m_curSucc >= m_numSuccs)
        {
            return nullptr;
        }

        if (m_numSuccs <= ArrLen(m_successors))
        {
            return m_successors[m_curSucc];
        }

        return m_pSuccessors[m_curSucc];
    }
};

//------------------------------------------------------------------------------
// fgHasCycleWithoutGCSafePoint: Check if the flow graph has a cycle in it that
// does not go through a BBF_GC_SAFE_POINT block.
//
// Returns:
//   True if a cycle exists, in which case the function needs to be marked
//   fully interruptible.
//
bool Compiler::fgHasCycleWithoutGCSafePoint()
{
    ArrayStack<GCSafePointSuccessorEnumerator> stack(getAllocator(CMK_ArrayStack));
    BitVecTraits                               traits(fgBBNumMax + 1, this);
    BitVec                                     visited(BitVecOps::MakeEmpty(&traits));
    BitVec                                     finished(BitVecOps::MakeEmpty(&traits));

    for (BasicBlock* block : Blocks())
    {
        if (block->HasFlag(BBF_GC_SAFE_POINT))
        {
            continue;
        }

        if (BitVecOps::IsMember(&traits, finished, block->bbNum))
        {
            continue;
        }

        bool added = BitVecOps::TryAddElemD(&traits, visited, block->bbNum);
        assert(added);

        stack.Emplace(this, block);

        while (stack.Height() > 0)
        {
            BasicBlock* block = stack.TopRef().Block();
            BasicBlock* succ  = stack.TopRef().NextSuccessor();

            if (succ != nullptr)
            {
                if (succ->HasFlag(BBF_GC_SAFE_POINT))
                {
                    continue;
                }

                if (BitVecOps::IsMember(&traits, finished, succ->bbNum))
                {
                    continue;
                }

                if (!BitVecOps::TryAddElemD(&traits, visited, succ->bbNum))
                {
#ifdef DEBUG
                    if (verbose)
                    {
                        printf("Found a cycle that does not go through a GC safe point:\n");
                        printf(FMT_BB, succ->bbNum);
                        for (int index = 0; index < stack.Height(); index++)
                        {
                            BasicBlock* block = stack.TopRef(index).Block();
                            printf(" <- " FMT_BB, block->bbNum);

                            if (block == succ)
                                break;
                        }
                        printf("\n");
                    }
#endif

                    return true;
                }

                stack.Emplace(this, succ);
            }
            else
            {
                BitVecOps::AddElemD(&traits, finished, block->bbNum);
                stack.Pop();
            }
        }
    }

    return false;
}

/*****************************************************************************/

void Compiler::fgSetStmtSeq(Statement* stmt)
{
    stmt->SetTreeList(fgSetTreeSeq(stmt->GetRootNode()));

#ifdef DEBUG
    // Keep track of the highest # of tree nodes.
    if (BasicBlock::s_nMaxTrees < stmt->GetRootNode()->gtSeqNum)
    {
        BasicBlock::s_nMaxTrees = stmt->GetRootNode()->gtSeqNum;
    }
#endif // DEBUG
}

/*****************************************************************************/

void Compiler::fgSetBlockOrder(BasicBlock* block)
{
    for (Statement* const stmt : block->Statements())
    {
        fgSetStmtSeq(stmt);

        /* Are there any more trees in this basic block? */

        if (stmt->GetNextStmt() == nullptr)
        {
            /* last statement in the tree list */
            noway_assert(block->lastStmt() == stmt);
            break;
        }

#ifdef DEBUG
        if (block->bbStmtList == stmt)
        {
            /* first statement in the list */
            assert(stmt->GetPrevStmt()->GetNextStmt() == nullptr);
        }
        else
        {
            assert(stmt->GetPrevStmt()->GetNextStmt() == stmt);
        }

        assert(stmt->GetNextStmt()->GetPrevStmt() == stmt);
#endif // DEBUG
    }
}

//------------------------------------------------------------------------
// fgGetFirstNode: Get the first node in the tree, in execution order
//
// Arguments:
//    tree - The top node of the tree of interest
//
// Return Value:
//    The first node in execution order, that belongs to tree.
//
// Notes:
//    This function is only correct for HIR trees.
//
/* static */ GenTree* Compiler::fgGetFirstNode(GenTree* tree)
{
    GenTree* firstNode = tree;
    while (true)
    {
        auto operandsBegin = firstNode->OperandsBegin();
        auto operandsEnd   = firstNode->OperandsEnd();

        if (operandsBegin == operandsEnd)
        {
            break;
        }

        firstNode = *operandsBegin;
    }

    return firstNode;
}

#ifdef DEBUG

//------------------------------------------------------------------------
// FlowGraphDfsTree::Dump: Dump a textual representation of the DFS tree.
//
void FlowGraphDfsTree::Dump() const
{
    printf("DFS tree. %s.\n", HasCycle() ? "Has cycle" : "No cycle");
    printf("PO RPO -> BB [pre, post]\n");
    for (unsigned i = 0; i < GetPostOrderCount(); i++)
    {
        unsigned          rpoNum = GetPostOrderCount() - i - 1;
        BasicBlock* const block  = GetPostOrder(i);
        printf("%02u %02u -> " FMT_BB "[%u, %u]\n", i, rpoNum, block->bbNum, block->bbPreorderNum,
               block->bbPostorderNum);
    }
}

#endif // DEBUG

//------------------------------------------------------------------------
// FlowGraphDfsTree::Contains: Check if a block is contained in the DFS tree;
// i.e., if it is reachable.
//
// Arguments:
//    block - The block
//
// Return Value:
//    True if the block is reachable from the root.
//
// Remarks:
//    If the block was added after the DFS tree was computed, then this
//    function returns false.
//
bool FlowGraphDfsTree::Contains(BasicBlock* block) const
{
    return (block->bbPostorderNum < m_postOrderCount) && (m_postOrder[block->bbPostorderNum] == block);
}

//------------------------------------------------------------------------
// FlowGraphDfsTree::IsAncestor: Check if block `ancestor` is an ancestor of
// block `descendant`
//
// Arguments:
//   ancestor   - block that is possible ancestor
//   descendant - block that is possible descendant
//
// Returns:
//   True if `ancestor` is ancestor of `descendant` in the depth first spanning
//   tree.
//
// Notes:
//   If return value is false, then `ancestor` does not dominate `descendant`.
//
bool FlowGraphDfsTree::IsAncestor(BasicBlock* ancestor, BasicBlock* descendant) const
{
    assert(Contains(ancestor) && Contains(descendant));
    return (ancestor->bbPreorderNum <= descendant->bbPreorderNum) &&
           (descendant->bbPostorderNum <= ancestor->bbPostorderNum);
}

//------------------------------------------------------------------------
// fgComputeDfs: Compute a depth-first search tree for the flow graph.
//
// Type parameters:
//   useProfile - If true, determines order of successors visited using profile data
//
// Returns:
//   The tree.
//
// Notes:
//   Preorder and postorder numbers are assigned into the BasicBlock structure.
//   The tree returned contains a postorder of the basic blocks.
//
template <const bool useProfile /* = false */>
FlowGraphDfsTree* Compiler::fgComputeDfs()
{
    BasicBlock** postOrder = new (this, CMK_DepthFirstSearch) BasicBlock*[fgBBcount];
    bool         hasCycle  = false;

    auto visitPreorder = [](BasicBlock* block, unsigned preorderNum) {
        block->bbPreorderNum  = preorderNum;
        block->bbPostorderNum = UINT_MAX;
    };

    auto visitPostorder = [=](BasicBlock* block, unsigned postorderNum) {
        block->bbPostorderNum = postorderNum;
        assert(postorderNum < fgBBcount);
        postOrder[postorderNum] = block;
    };

    auto visitEdge = [&hasCycle](BasicBlock* block, BasicBlock* succ) {
        // Check if block -> succ is a backedge, in which case the flow
        // graph has a cycle.
        if ((succ->bbPreorderNum <= block->bbPreorderNum) && (succ->bbPostorderNum == UINT_MAX))
        {
            hasCycle = true;
        }
    };

    unsigned numBlocks =
        fgRunDfs<decltype(visitPreorder), decltype(visitPostorder), decltype(visitEdge), useProfile>(visitPreorder,
                                                                                                     visitPostorder,
                                                                                                     visitEdge);
    return new (this, CMK_DepthFirstSearch) FlowGraphDfsTree(this, postOrder, numBlocks, hasCycle, useProfile);
}

// Add explicit instantiations.
template FlowGraphDfsTree* Compiler::fgComputeDfs<false>();
template FlowGraphDfsTree* Compiler::fgComputeDfs<true>();

//------------------------------------------------------------------------
// fgInvalidateDfsTree: Invalidate computed DFS tree and dependent annotations
// (like loops, dominators and SSA).
//
void Compiler::fgInvalidateDfsTree()
{
    m_dfsTree          = nullptr;
    m_loops            = nullptr;
    m_domTree          = nullptr;
    m_domFrontiers     = nullptr;
    m_reachabilitySets = nullptr;
    fgSsaValid         = false;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::FlowGraphNaturalLoop: Initialize a new loop instance.
//
// Returns:
//   tree   - The DFS tree
//   header - The loop header
//
FlowGraphNaturalLoop::FlowGraphNaturalLoop(const FlowGraphDfsTree* dfsTree, BasicBlock* header)
    : m_dfsTree(dfsTree)
    , m_header(header)
    , m_blocks(BitVecOps::UninitVal())
    , m_backEdges(dfsTree->GetCompiler()->getAllocator(CMK_Loops))
    , m_entryEdges(dfsTree->GetCompiler()->getAllocator(CMK_Loops))
    , m_exitEdges(dfsTree->GetCompiler()->getAllocator(CMK_Loops))
{
}

//------------------------------------------------------------------------
// GetPreheader: Get the preheader of this loop, if it has one.
//
// Returns:
//   The preheader, or nullptr if there is no preheader.
//
BasicBlock* FlowGraphNaturalLoop::GetPreheader() const
{
    if (m_entryEdges.size() != 1)
    {
        return nullptr;
    }

    BasicBlock* preheader = m_entryEdges[0]->getSourceBlock();
    if (!preheader->KindIs(BBJ_ALWAYS))
    {
        return nullptr;
    }

    return preheader;
}

//------------------------------------------------------------------------
// SetEntryEdge: Set the entry edge of a loop
//
// Arguments:
//   entryEdge - The new entry edge
//
void FlowGraphNaturalLoop::SetEntryEdge(FlowEdge* entryEdge)
{
    m_entryEdges.clear();
    m_entryEdges.push_back(entryEdge);
}

//------------------------------------------------------------------------
// GetDepth: Get the depth of the loop.
//
// Returns:
//   The number of ancestors (0 for a top-most loop).
//
unsigned FlowGraphNaturalLoop::GetDepth() const
{
    unsigned depth = 0;
    for (FlowGraphNaturalLoop* ancestor = GetParent(); ancestor != nullptr; ancestor = ancestor->GetParent())
    {
        depth++;
    }

    return depth;
}

//------------------------------------------------------------------------
// LoopBlockBitVecIndex: Convert a basic block to an index into the bit vector
// used to store the set of loop blocks.
//
// Parameters:
//   block - The block
//
// Returns:
//   Index into the bit vector
//
// Remarks:
//   The bit vector is stored with the base index of the loop header since we
//   know the header is an ancestor of all loop blocks. Thus we do not need to
//   waste space on previous blocks.
//
//   This function should only be used when it is known that the block has an
//   index in the loop bit vector.
//
unsigned FlowGraphNaturalLoop::LoopBlockBitVecIndex(BasicBlock* block)
{
    assert(m_dfsTree->Contains(block));

    unsigned index = m_header->bbPostorderNum - block->bbPostorderNum;
    assert(index < m_blocksSize);
    return index;
}

//------------------------------------------------------------------------
// TryGetLoopBlockBitVecIndex: Convert a basic block to an index into the bit
// vector used to store the set of loop blocks.
//
// Parameters:
//   block  - The block
//   pIndex - [out] Index into the bit vector, if this function returns true.
//
// Returns:
//   True if the block has an index in the loop bit vector.
//
// Remarks:
//   See GetLoopBlockBitVecIndex for more information. This function can be
//   used when it is not known whether the block has an index in the loop bit
//   vector.
//
bool FlowGraphNaturalLoop::TryGetLoopBlockBitVecIndex(BasicBlock* block, unsigned* pIndex)
{
    if (block->bbPostorderNum > m_header->bbPostorderNum)
    {
        return false;
    }

    unsigned index = m_header->bbPostorderNum - block->bbPostorderNum;
    if (index >= m_blocksSize)
    {
        return false;
    }

    *pIndex = index;
    return true;
}

//------------------------------------------------------------------------
// LoopBlockTraits: Get traits for a bit vector for blocks in this loop.
//
// Returns:
//   Bit vector traits.
//
BitVecTraits FlowGraphNaturalLoop::LoopBlockTraits()
{
    return BitVecTraits(m_blocksSize, m_dfsTree->GetCompiler());
}

//------------------------------------------------------------------------
// ContainsBlock: Returns true if this loop contains the specified block.
//
// Parameters:
//   block - A block
//
// Returns:
//   True if the block is contained in the loop.
//
// Remarks:
//   Containment here means that the block is in the SCC of the loop; i.e. it
//   is in a cycle with the header block. Note that EH successors are taken
//   into account.
//
bool FlowGraphNaturalLoop::ContainsBlock(BasicBlock* block)
{
    if (!m_dfsTree->Contains(block))
    {
        return false;
    }

    unsigned index;
    if (!TryGetLoopBlockBitVecIndex(block, &index))
    {
        return false;
    }

    BitVecTraits traits = LoopBlockTraits();
    return BitVecOps::IsMember(&traits, m_blocks, index);
}

//------------------------------------------------------------------------
// ContainsLoop: Returns true if this loop contains the specified other loop.
//
// Parameters:
//   childLoop - The potential candidate child loop
//
// Returns:
//   True if the child loop is contained in this loop.
//
bool FlowGraphNaturalLoop::ContainsLoop(FlowGraphNaturalLoop* childLoop)
{
    return ContainsBlock(childLoop->GetHeader());
}

//------------------------------------------------------------------------
// NumLoopBlocks: Get the number of blocks in the SCC of the loop.
//
// Returns:
//   Count of blocks.
//
unsigned FlowGraphNaturalLoop::NumLoopBlocks()
{
    BitVecTraits loopTraits = LoopBlockTraits();
    return BitVecOps::Count(&loopTraits, m_blocks);
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoops::FlowGraphNaturalLoops: Initialize a new instance to
// track a set of loops over the flow graph.
//
// Parameters:
//   dfs - A DFS tree.
//
FlowGraphNaturalLoops::FlowGraphNaturalLoops(const FlowGraphDfsTree* dfsTree)
    : m_dfsTree(dfsTree)
    , m_loops(m_dfsTree->GetCompiler()->getAllocator(CMK_Loops))
    , m_improperLoopHeaders(0)
{
}

//------------------------------------------------------------------------
// GetLoopByIndex: Get loop by a specified index.
//
// Parameters:
//    index - Index of loop. Must be less than NumLoops()
//
// Returns:
//    Loop with the specified index.
//
FlowGraphNaturalLoop* FlowGraphNaturalLoops::GetLoopByIndex(unsigned index)
{
    assert(index < m_loops.size());
    return m_loops[index];
}

//------------------------------------------------------------------------
// GetLoopByHeader: See if a block is a loop header, and if so return the
// associated loop.
//
// Parameters:
//    block - block in question
//
// Returns:
//    Loop headed by block, or nullptr
//
FlowGraphNaturalLoop* FlowGraphNaturalLoops::GetLoopByHeader(BasicBlock* block)
{
    if (!m_dfsTree->Contains(block))
    {
        return nullptr;
    }

    // Loops are stored in reverse post-order,
    // so we can binary-search for the desired loop's header by its post-order number.
    size_t min = 0;
    size_t max = NumLoops();

    while (min < max)
    {
        const size_t                mid    = min + ((max - min) / 2);
        FlowGraphNaturalLoop* const loop   = m_loops[mid];
        BasicBlock* const           header = loop->m_header;

        if (header == block)
        {
            return loop;
        }
        else if (header->bbPostorderNum < block->bbPostorderNum)
        {
            max = mid;
        }
        else
        {
            assert(header->bbPostorderNum > block->bbPostorderNum);
            min = mid + 1;
        }
    }

    return nullptr;
}

//------------------------------------------------------------------------
// IsLoopBackEdge: See if an edge is a loop back edge
//
// Parameters:
//   edge - edge in question
//
// Returns:
//   True if edge is a backedge in some recognized loop.
//
bool FlowGraphNaturalLoops::IsLoopBackEdge(FlowEdge* edge)
{
    for (FlowGraphNaturalLoop* loop : m_loops)
    {
        for (FlowEdge* loopBackEdge : loop->m_backEdges)
        {
            if (loopBackEdge == edge)
            {
                return true;
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------
// IsLoopExitEdge: see if a flow edge is a loop exit edge
//
// Parameters:
//   edge - edge in question
//
// Returns:
//   True if edge is an exit edge in some recognized loop. Note that a single
//   edge may exit multiple loops.
//
bool FlowGraphNaturalLoops::IsLoopExitEdge(FlowEdge* edge)
{
    for (FlowGraphNaturalLoop* loop : m_loops)
    {
        for (FlowEdge* loopExitEdge : loop->m_exitEdges)
        {
            if (loopExitEdge == edge)
            {
                return true;
            }
        }
    }

    return false;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoops::Find: Find natural loops in the specified DFS tree
// constructed for the flow graph.
//
// Parameters:
//   dfsTree - The DFS tree
//
// Returns:
//   Identified natural loops.
//
FlowGraphNaturalLoops* FlowGraphNaturalLoops::Find(const FlowGraphDfsTree* dfsTree)
{
    Compiler* comp         = dfsTree->GetCompiler();
    comp->m_blockToEHPreds = nullptr;

#ifdef DEBUG
    JITDUMP("Identifying loops in DFS tree with following reverse post order:\n");
    JITDUMP("RPO -> BB [pre, post]\n");
    for (unsigned i = dfsTree->GetPostOrderCount(); i != 0; i--)
    {
        unsigned          rpoNum = dfsTree->GetPostOrderCount() - i;
        BasicBlock* const block  = dfsTree->GetPostOrder(i - 1);
        JITDUMP("%02u -> " FMT_BB "[%u, %u]\n", rpoNum, block->bbNum, block->bbPreorderNum, block->bbPostorderNum);
    }

#endif

    FlowGraphNaturalLoops* loops = new (comp, CMK_Loops) FlowGraphNaturalLoops(dfsTree);

    if (!dfsTree->HasCycle())
    {
        JITDUMP("Flow graph has no cycles; skipping identification of natural loops\n");
        return loops;
    }

    ArrayStack<BasicBlock*> worklist(comp->getAllocator(CMK_Loops));

    for (unsigned i = dfsTree->GetPostOrderCount(); i != 0; i--)
    {
        BasicBlock* const header = dfsTree->GetPostOrder(i - 1);

        // If a block is a DFS ancestor of one if its predecessors then the block is a loop header.
        //
        FlowGraphNaturalLoop* loop = nullptr;

        for (FlowEdge* predEdge : header->PredEdges())
        {
            BasicBlock* predBlock = predEdge->getSourceBlock();
            if (dfsTree->Contains(predBlock) && dfsTree->IsAncestor(header, predBlock))
            {
                if (loop == nullptr)
                {
                    loop = new (comp, CMK_Loops) FlowGraphNaturalLoop(dfsTree, header);
                    JITDUMP("\n");
                }

                JITDUMP(FMT_BB " -> " FMT_BB " is a backedge\n", predBlock->bbNum, header->bbNum);
                loop->m_backEdges.push_back(predEdge);
            }
        }

        if (loop == nullptr)
        {
            continue;
        }

        JITDUMP(FMT_BB " is the header of a DFS loop with %zu back edges\n", header->bbNum, loop->m_backEdges.size());

        // Now walk back in flow along the back edges from head to determine if
        // this is a natural loop and to find all the blocks in the loop.
        //

        loop->m_blocksSize = loop->m_header->bbPostorderNum + 1;

        BitVecTraits loopTraits = loop->LoopBlockTraits();
        loop->m_blocks          = BitVecOps::MakeEmpty(&loopTraits);

        if (!FindNaturalLoopBlocks(loop, worklist) || !IsLoopCanonicalizable(loop))
        {
            loops->m_improperLoopHeaders++;

            for (FlowGraphNaturalLoop* const otherLoop : loops->InPostOrder())
            {
                if (otherLoop->ContainsBlock(header))
                {
                    JITDUMP("Noting that " FMT_LP " contains an improper loop header\n", loop->GetIndex());
                    otherLoop->m_containsImproperHeader = true;
                }
            }

            continue;
        }

        JITDUMP("Loop has %u blocks\n", BitVecOps::Count(&loopTraits, loop->m_blocks));

        // Find the exit edges
        //
        loop->VisitLoopBlocksReversePostOrder([=](BasicBlock* loopBlock) {
            loopBlock->VisitRegularSuccs(comp, [=](BasicBlock* succBlock) {
                if (!loop->ContainsBlock(succBlock))
                {
                    FlowEdge* const exitEdge = comp->fgGetPredForBlock(succBlock, loopBlock);
                    JITDUMP(FMT_BB " -> " FMT_BB " is an exit edge\n", loopBlock->bbNum, succBlock->bbNum);
                    loop->m_exitEdges.push_back(exitEdge);
                }

                return BasicBlockVisit::Continue;
            });

            return BasicBlockVisit::Continue;
        });

        // Find the entry edges
        //
        // Note if fgEntryBB is a loop head we won't have an entry edge.
        // So it needs to be special cased later on when processing
        // entry edges.
        //
        for (FlowEdge* const predEdge : loop->m_header->PredEdges())
        {
            BasicBlock* predBlock = predEdge->getSourceBlock();
            if (dfsTree->Contains(predBlock) && !dfsTree->IsAncestor(header, predBlock))
            {
                JITDUMP(FMT_BB " -> " FMT_BB " is an entry edge\n", predBlock->bbNum, loop->m_header->bbNum);
                loop->m_entryEdges.push_back(predEdge);
            }
        }

        // Search for parent loop.
        //
        // Since loops record in outer->inner order the parent will be the
        // most recently recorded loop that contains this loop's header.
        //
        for (FlowGraphNaturalLoop* const otherLoop : loops->InPostOrder())
        {
            if (otherLoop->ContainsBlock(header))
            {
                loop->m_parent = otherLoop;
                JITDUMP("Nested within loop starting at " FMT_BB "\n", otherLoop->GetHeader()->bbNum);
                break;
            }
        }

#ifdef DEBUG
        // In debug, validate nestedness versus other loops.
        //
        for (FlowGraphNaturalLoop* const otherLoop : loops->InPostOrder())
        {
            if (otherLoop->ContainsBlock(header))
            {
                // Ancestor loop; should contain all blocks of this loop
                //
                loop->VisitLoopBlocks([otherLoop](BasicBlock* loopBlock) {
                    assert(otherLoop->ContainsBlock(loopBlock));
                    return BasicBlockVisit::Continue;
                });
            }
            else
            {
                // Non-ancestor loop; should have no blocks in common with current loop
                //
                loop->VisitLoopBlocks([otherLoop](BasicBlock* loopBlock) {
                    assert(!otherLoop->ContainsBlock(loopBlock));
                    return BasicBlockVisit::Continue;
                });
            }
        }
#endif

        // Record this loop
        //
        loop->m_index = (unsigned)loops->m_loops.size();
        loops->m_loops.push_back(loop);

        JITDUMP("Added loop " FMT_LP " with header " FMT_BB "\n", loop->GetIndex(), loop->GetHeader()->bbNum);
    }

    // Now build sibling/child links by iterating loops in post order. This
    // makes us end up with sibling links in reverse post order.
    for (FlowGraphNaturalLoop* loop : loops->InPostOrder())
    {
        if (loop->m_parent != nullptr)
        {
            loop->m_sibling         = loop->m_parent->m_child;
            loop->m_parent->m_child = loop;
        }
    }

#ifdef DEBUG
    if (loops->m_loops.size() > 0)
    {
        JITDUMP("\nFound %zu loops\n", loops->m_loops.size());
    }

    if (loops->m_improperLoopHeaders > 0)
    {
        JITDUMP("Rejected %u loop headers\n", loops->m_improperLoopHeaders);
    }

    JITDUMPEXEC(Dump(loops));
#endif

    return loops;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoops::FindNaturalLoopBlocks: Find the loop blocks for a
// specified loop.
//
// Parameters:
//   loop     - The natural loop we are constructing
//   worklist - Scratch worklist to use for the search
//
// Returns:
//   True if the loop is natural; marks the loop blocks into 'loop' as part of
//   the search.
//
bool FlowGraphNaturalLoops::FindNaturalLoopBlocks(FlowGraphNaturalLoop* loop, ArrayStack<BasicBlock*>& worklist)
{
    const FlowGraphDfsTree* dfsTree    = loop->m_dfsTree;
    Compiler*               comp       = dfsTree->GetCompiler();
    BitVecTraits            loopTraits = loop->LoopBlockTraits();
    BitVecOps::AddElemD(&loopTraits, loop->m_blocks, 0);

    // Seed the worklist
    //
    worklist.Reset();
    for (FlowEdge* backEdge : loop->m_backEdges)
    {
        BasicBlock* const backEdgeSource = backEdge->getSourceBlock();
        if (backEdgeSource == loop->GetHeader())
        {
            continue;
        }

        assert(!BitVecOps::IsMember(&loopTraits, loop->m_blocks, loop->LoopBlockBitVecIndex(backEdgeSource)));
        worklist.Push(backEdgeSource);
        BitVecOps::AddElemD(&loopTraits, loop->m_blocks, loop->LoopBlockBitVecIndex(backEdgeSource));
    }

    // Work back through flow to loop head or to another pred
    // that is clearly outside the loop.
    //
    while (!worklist.Empty())
    {
        BasicBlock* const loopBlock = worklist.Pop();

        for (FlowEdge* predEdge = comp->BlockPredsWithEH(loopBlock); predEdge != nullptr;
             predEdge           = predEdge->getNextPredEdge())
        {
            BasicBlock* const predBlock = predEdge->getSourceBlock();

            if (!dfsTree->Contains(predBlock))
            {
                continue;
            }

            // Head cannot dominate `predBlock` unless it is a DFS ancestor.
            //
            if (!dfsTree->IsAncestor(loop->GetHeader(), predBlock))
            {
                JITDUMP("Loop is not natural; witness " FMT_BB " -> " FMT_BB "\n", predBlock->bbNum, loopBlock->bbNum);
                return false;
            }

            if (BitVecOps::TryAddElemD(&loopTraits, loop->m_blocks, loop->LoopBlockBitVecIndex(predBlock)))
            {
                worklist.Push(predBlock);
            }
        }
    }

    return true;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoops::IsLoopCanonicalizable:
//   Check if a loop will be able to be canonicalized if we record it.
//
// Parameters:
//   loop - Loop structure (partially filled by caller)
//
// Returns:
//   True if the loop header can be canonicalized:
//     - Can have a preheader created
//     - Exits can be made unique from the loop
//
bool FlowGraphNaturalLoops::IsLoopCanonicalizable(FlowGraphNaturalLoop* loop)
{
    Compiler* comp = loop->GetDfsTree()->GetCompiler();
    // The only (known) problematic case is when a backedge is a callfinally edge.
    if (!comp->bbIsHandlerBeg(loop->GetHeader()))
    {
        return true;
    }

    for (FlowEdge* backedge : loop->BackEdges())
    {
        if (backedge->getSourceBlock()->KindIs(BBJ_CALLFINALLY))
        {
            // It would not be possible to create a preheader for this loop
            // since this backedge could not be redirected.
            return false;
        }
    }

    return true;
}

#ifdef DEBUG

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::Dump: print one loops to the JitDump
//
/* static */
void FlowGraphNaturalLoop::Dump(FlowGraphNaturalLoop* loop)
{
    if (loop == nullptr)
    {
        printf("loop is nullptr");
        return;
    }

    // Display: LOOP# / header / parent loop# / blocks / entry edges / exit edges / back edges
    // Blocks can compacted be "[top .. bottom]" if lexically adjacent and no non-loop blocks in
    // the range. Otherwise, print a verbose list of blocks.

    Compiler* comp = loop->GetDfsTree()->GetCompiler();
    printf(FMT_LP, loop->GetIndex());

    printf(" header: " FMT_BB, loop->GetHeader()->bbNum);
    if (loop->GetParent() != nullptr)
    {
        printf(" parent: " FMT_LP, loop->GetParent()->GetIndex());
    }

    // Dump the set of blocks in the loop. There are three cases:
    // 1. If there is only one block in the loop, display it.
    // 2. If the blocks happen to be lexically dense and without non-loop blocks in the range,
    //    then use a shortcut of `[BBtop .. BBbottom]`. Note that "lexically dense" is defined
    //    in terms of the "bbNext" ordering of blocks, which is the default used by the basic
    //    block dumper fgDispBasicBlocks. However, setting JitDumpFgBlockOrder can change the
    //    basic block dump order.
    // 3. If all the loop blocks are found when traversing from the lexical top to lexical
    //    bottom block (as defined by `bbNum` ordering, not `bbNext` ordering), then display
    //    a set of ranges, with the non-loop blocks in the range breaking up the continuous range.
    // 4. Otherwise, display the entire list of blocks individually.
    //
    // Lexicality depends on properly renumbered blocks, which we might not have when dumping.

    bool           first;
    const unsigned numBlocks = loop->NumLoopBlocks();
    printf("\n  Members (%u): ", numBlocks);

    if (numBlocks == 0)
    {
        // This should never happen
        printf("NONE?");
    }
    else if (numBlocks == 1)
    {
        // If there's exactly one block, it must be the header.
        printf(FMT_BB, loop->GetHeader()->bbNum);
    }
    else
    {
        BasicBlock* const lexicalTopBlock     = loop->GetLexicallyTopMostBlock();
        BasicBlock* const lexicalBottomBlock  = loop->GetLexicallyBottomMostBlock();
        BasicBlock* const lexicalEndIteration = lexicalBottomBlock->Next();
        unsigned          numLexicalBlocks    = 0;

        // Count the number of loop blocks found in the identified lexical range. If there are non-loop blocks
        // found, or if we don't find all the loop blocks in the lexical walk (meaning the bbNums might not be
        // properly ordered), we fail.
        bool lexicallyDense                    = true; // assume the best
        bool lexicalRangeContainsAllLoopBlocks = true; // assume the best
        for (BasicBlock* block = lexicalTopBlock; (block != nullptr) && (block != lexicalEndIteration);
             block             = block->Next())
        {
            if (!loop->ContainsBlock(block))
            {
                lexicallyDense = false;
            }
            else
            {
                ++numLexicalBlocks;
            }
        }
        if (numBlocks != numLexicalBlocks)
        {
            lexicalRangeContainsAllLoopBlocks = false;
        }

        if (lexicallyDense && lexicalRangeContainsAllLoopBlocks)
        {
            // This is just an optimization over the next case (`!lexicallyDense`) as there's no need to
            // loop over the blocks again.
            printf("[" FMT_BB ".." FMT_BB "]", lexicalTopBlock->bbNum, lexicalBottomBlock->bbNum);
        }
        else if (lexicalRangeContainsAllLoopBlocks)
        {
            // The lexical range from top to bottom contains all the loop blocks, but also contains some
            // non-loop blocks. Try to display the blocks in groups of ranges, to avoid dumping all the
            // blocks individually.
            BasicBlock* firstInRange = nullptr;
            BasicBlock* lastInRange  = nullptr;
            first                    = true;
            auto printRange          = [&]() {
                // Dump current range if there is one; reset firstInRange.
                if (firstInRange == nullptr)
                {
                    return;
                }
                if (!first)
                {
                    printf(";");
                }
                if (firstInRange == lastInRange)
                {
                    // Just one block in range
                    printf(FMT_BB, firstInRange->bbNum);
                }
                else
                {
                    printf("[" FMT_BB ".." FMT_BB "]", firstInRange->bbNum, lastInRange->bbNum);
                }
                firstInRange = lastInRange = nullptr;
                first                      = false;
            };
            for (BasicBlock* block = lexicalTopBlock; block != lexicalEndIteration; block = block->Next())
            {
                if (!loop->ContainsBlock(block))
                {
                    printRange();
                }
                else
                {
                    if (firstInRange == nullptr)
                    {
                        firstInRange = block;
                    }
                    lastInRange = block;
                }
            }
            printRange();
        }
        else
        {
            // We didn't see all the loop blocks in the lexical range; maybe the `bbNum` order is
            // not well ordered such that `top` and `bottom` are not first/last in `bbNext` order.
            // Just dump all the blocks individually using the loop block visitor.
            first = true;
            loop->VisitLoopBlocksReversePostOrder([&first](BasicBlock* block) {
                printf("%s" FMT_BB, first ? "" : ";", block->bbNum);
                first = false;
                return BasicBlockVisit::Continue;
            });

            // Print out the lexical top and bottom blocks, which will explain why we didn't print ranges.
            printf("\n  Lexical top: " FMT_BB, lexicalTopBlock->bbNum);
            printf("\n  Lexical bottom: " FMT_BB, lexicalBottomBlock->bbNum);
        }
    }

    // Dump Entry Edges, Back Edges, Exit Edges

    printf("\n  Entry: ");
    if (loop->EntryEdges().size() == 0)
    {
        printf("NONE");
    }
    else
    {
        first = true;
        for (FlowEdge* const edge : loop->EntryEdges())
        {
            printf("%s" FMT_BB " -> " FMT_BB, first ? "" : "; ", edge->getSourceBlock()->bbNum,
                   loop->GetHeader()->bbNum);
            first = false;
        }
    }

    printf("\n  Exit: ");
    if (loop->ExitEdges().size() == 0)
    {
        printf("NONE");
    }
    else
    {
        first = true;
        for (FlowEdge* const edge : loop->ExitEdges())
        {
            BasicBlock* const exitingBlock = edge->getSourceBlock();
            BasicBlock* const exitBlock    = edge->getDestinationBlock();
            printf("%s" FMT_BB " -> " FMT_BB, first ? "" : "; ", exitingBlock->bbNum, exitBlock->bbNum);
            first = false;
        }
    }

    printf("\n  Back: ");
    if (loop->BackEdges().size() == 0)
    {
        printf("NONE");
    }
    else
    {
        first = true;
        for (FlowEdge* const edge : loop->BackEdges())
        {
            printf("%s" FMT_BB " -> " FMT_BB, first ? "" : "; ", edge->getSourceBlock()->bbNum,
                   loop->GetHeader()->bbNum);
            first = false;
        }
    }

    printf("\n");
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoops::Dump: print the loops to the JitDump
//
/* static */
void FlowGraphNaturalLoops::Dump(FlowGraphNaturalLoops* loops)
{
    printf("\n***************  Natural loop graph\n");

    if (loops == nullptr)
    {
        printf("loops is nullptr\n");
    }
    else if (loops->NumLoops() == 0)
    {
        printf("No loops\n");
    }
    else
    {
        for (FlowGraphNaturalLoop* loop : loops->InReversePostOrder())
        {
            FlowGraphNaturalLoop::Dump(loop);
        }
    }

    printf("\n");
}

#endif // DEBUG

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::VisitDefs: Visit all definitions contained in the
// loop.
//
// Type parameters:
//   TFunc - Callback functor type
//
// Parameters:
//   func - Callback functor that accepts a GenTreeLclVarCommon* and returns a
//   bool. On true, continue looking for defs; on false, abort.
//
// Returns:
//   True if all defs were visited and the functor never returned false; otherwise false.
//
template <typename TFunc>
bool FlowGraphNaturalLoop::VisitDefs(TFunc func)
{
    class VisitDefsVisitor : public GenTreeVisitor<VisitDefsVisitor>
    {
        using GenTreeVisitor<VisitDefsVisitor>::m_compiler;

        TFunc& m_func;

    public:
        enum
        {
            DoPreOrder = true,
        };

        VisitDefsVisitor(Compiler* comp, TFunc& func)
            : GenTreeVisitor<VisitDefsVisitor>(comp)
            , m_func(func)
        {
        }

        Compiler::fgWalkResult PreOrderVisit(GenTree** use, GenTree* user)
        {
            GenTree* tree = *use;
            if ((tree->gtFlags & GTF_ASG) == 0)
            {
                return Compiler::WALK_SKIP_SUBTREES;
            }

            auto visitDef = [=](GenTreeLclVarCommon* lcl) {
                return m_func(lcl) ? GenTree::VisitResult::Continue : GenTree::VisitResult::Abort;
            };

            if (tree->VisitLocalDefNodes(m_compiler, visitDef) == GenTree::VisitResult::Abort)
            {
                return Compiler::WALK_ABORT;
            }

            return Compiler::WALK_CONTINUE;
        }
    };

    VisitDefsVisitor visitor(m_dfsTree->GetCompiler(), func);

    BasicBlockVisit result = VisitLoopBlocks([&](BasicBlock* loopBlock) {
        for (Statement* stmt : loopBlock->Statements())
        {
            if (visitor.WalkTree(stmt->GetRootNodePointer(), nullptr) == Compiler::WALK_ABORT)
                return BasicBlockVisit::Abort;
        }

        return BasicBlockVisit::Continue;
    });

    return result == BasicBlockVisit::Continue;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::FindDef: Find a def of the specified local number.
//
// Parameters:
//   lclNum - The local.
//
// Returns:
//   Tree that represents a def of the local, or a def of the parent local if
//   the local is a field; nullptr if no def was found.
//
// Remarks:
//   Does not support promoted struct locals, but does support fields of
//   promoted structs.
//
GenTreeLclVarCommon* FlowGraphNaturalLoop::FindDef(unsigned lclNum)
{
    LclVarDsc* dsc = m_dfsTree->GetCompiler()->lvaGetDesc(lclNum);
    assert(!dsc->lvPromoted);

    unsigned lclNum2 = BAD_VAR_NUM;

    if (dsc->lvIsStructField)
    {
        lclNum2 = dsc->lvParentLcl;
    }

    GenTreeLclVarCommon* result = nullptr;
    VisitDefs([&result, lclNum, lclNum2](GenTreeLclVarCommon* def) {
        if ((def->GetLclNum() == lclNum) || (def->GetLclNum() == lclNum2))
        {
            result = def;
            return false;
        }

        return true;
    });

    return result;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::AnalyzeIteration: Analyze the induction structure of
// the loop.
//
// Parameters:
//   info - [out] Loop information
//
// Returns:
//   True if the structure was analyzed and we can make guarantees about it;
//   otherwise false.
//
// Remarks:
//   On a true return, the function guarantees that the loop invariant is true
//   and maintained at all points within the loop, except possibly right after
//   the update of the iterator variable (NaturalLoopIterInfo::IterTree). The
//   function guarantees that the test (NaturalLoopIterInfo::TestTree) occurs
//   immediately after the update, so no IR in the loop is executed without the
//   loop invariant being true, except for the test.
//
//   The loop invariant is defined as the expression obtained by
//   [info->IterVar] [info->TestOper()] [info->Limit()]. Note that
//   [info->TestTree()] may not be of this form; it could for instance have the
//   iterator variable as the second operand. However,
//   [NaturalLoopIterInfo::TestOper()] will automatically normalize the test
//   oper so that the invariant is equivalent to the returned form that has the
//   iteration variable as op1 and the limit as op2.
//
//   The limit can be further decomposed via NaturalLoopIterInfo::ConstLimit,
//   ::VarLimit and ::ArrLenLimit.
//
//   As an example, if info->IterVar == V02, info->TestOper() == GT_LT and
//   info->ConstLimit() == 10, then the function guarantees that the value of
//   the local V02 is less than 10 everywhere within the loop (except possibly
//   at the test).
//
//   In some cases we also know the initial value on entry to the loop; see
//   ::HasConstInit and ::ConstInitValue.
//
bool FlowGraphNaturalLoop::AnalyzeIteration(NaturalLoopIterInfo* info)
{
    JITDUMP("Analyzing iteration for " FMT_LP " with header " FMT_BB "\n", m_index, m_header->bbNum);

    const FlowGraphDfsTree* dfs  = m_dfsTree;
    Compiler*               comp = dfs->GetCompiler();
    assert((m_entryEdges.size() == 1) && "Expected preheader");

    BasicBlock* preheader = m_entryEdges[0]->getSourceBlock();

    JITDUMP("  Preheader = " FMT_BB "\n", preheader->bbNum);

    BasicBlock* initBlock = nullptr;
    GenTree*    init      = nullptr;
    GenTree*    test      = nullptr;

    info->IterVar = BAD_VAR_NUM;

    for (FlowEdge* exitEdge : ExitEdges())
    {
        BasicBlock* cond = exitEdge->getSourceBlock();
        JITDUMP("  Checking exiting block " FMT_BB "\n", cond->bbNum);
        if (!cond->KindIs(BBJ_COND))
        {
            JITDUMP("    Not a BBJ_COND\n");
            continue;
        }

        GenTree* iterTree = nullptr;
        initBlock         = preheader;
        if (!comp->optExtractInitTestIncr(&initBlock, cond, m_header, &init, &test, &iterTree))
        {
            JITDUMP("    Could not extract an IV\n");
            continue;
        }

        unsigned iterVar = comp->optIsLoopIncrTree(iterTree);
        assert(iterVar != BAD_VAR_NUM);
        LclVarDsc* const iterVarDsc = comp->lvaGetDesc(iterVar);
        // Bail on promoted case, otherwise we'd have to search the loop
        // for both iterVar and its parent.
        // TODO-CQ: Fix this
        //
        if (iterVarDsc->lvIsStructField)
        {
            JITDUMP("    iterVar V%02u is a promoted field\n", iterVar);
            continue;
        }

        // Bail on the potentially aliased case.
        //
        if (iterVarDsc->IsAddressExposed())
        {
            JITDUMP("    iterVar V%02u is address exposed\n", iterVar);
            continue;
        }

        if (!MatchLimit(iterVar, test, info))
        {
            continue;
        }

        bool result = VisitDefs([=](GenTreeLclVarCommon* def) {
            if ((def->GetLclNum() != iterVar) || (def == iterTree))
                return true;

            JITDUMP("    Loop has extraneous def [%06u]\n", Compiler::dspTreeID(def));
            return false;
        });

        if (!result)
        {
            continue;
        }

        info->TestBlock    = cond;
        info->IterVar      = iterVar;
        info->IterTree     = iterTree;
        info->ExitedOnTrue = exitEdge->getDestinationBlock() == cond->GetTrueTarget();
        break;
    }

    if (info->IterVar == BAD_VAR_NUM)
    {
        JITDUMP("  Could not find any IV\n");
        return false;
    }

    if (init == nullptr)
    {
        JITDUMP("  Init = <none>, test = [%06u], incr = [%06u]\n", Compiler::dspTreeID(test),
                Compiler::dspTreeID(info->IterTree));
    }
    else
    {
        JITDUMP("  Init = [%06u], test = [%06u], incr = [%06u]\n", Compiler::dspTreeID(init), Compiler::dspTreeID(test),
                Compiler::dspTreeID(info->IterTree));
    }

    MatchInit(info, initBlock, init);

    bool result = VisitDefs([=](GenTreeLclVarCommon* def) {
        if ((def->GetLclNum() != info->IterVar) || (def == info->IterTree))
            return true;

        JITDUMP("  Loop has extraneous def [%06u]\n", Compiler::dspTreeID(def));
        return false;
    });

    if (!result)
    {
        return false;
    }

    if (!CheckLoopConditionBaseCase(initBlock, info))
    {
        JITDUMP("  Loop condition may not be true on the first iteration\n");
        return false;
    }

#ifdef DEBUG
    if (comp->verbose)
    {
        printf("  IterVar = V%02u\n", info->IterVar);

        if (info->HasConstInit)
            printf("  Const init with value %d (at [%06u])\n", info->ConstInitValue,
                   Compiler::dspTreeID(info->InitTree));

        printf("  Test is [%06u] (", Compiler::dspTreeID(info->TestTree));
        if (info->HasConstLimit)
            printf("const limit ");
        if (info->HasSimdLimit)
            printf("simd limit ");
        if (info->HasInvariantLocalLimit)
            printf("invariant local limit ");
        if (info->HasArrayLengthLimit)
            printf("array length limit ");
        printf(")\n");
    }
#endif

    return true;
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::MatchInit: Try to pattern match the initialization of
// an induction variable.
//
// Parameters:
//   info      - [in, out] Info structure to query and fill out
//   initBlock - Block containing the initialization tree
//   init      - Initialization tree
//
// Remarks:
//   We do not necessarily guarantee or require to be able to find any
//   initialization.
//
void FlowGraphNaturalLoop::MatchInit(NaturalLoopIterInfo* info, BasicBlock* initBlock, GenTree* init)
{
    if ((init == nullptr) || !init->OperIs(GT_STORE_LCL_VAR) || (init->AsLclVarCommon()->GetLclNum() != info->IterVar))
        return;

    GenTree* initValue = init->AsLclVar()->Data();
    if (!initValue->IsCnsIntOrI() || !initValue->TypeIs(TYP_INT))
        return;

    info->HasConstInit   = true;
    info->ConstInitValue = (int)initValue->AsIntCon()->IconValue();
    INDEBUG(info->InitTree = init);
}

//------------------------------------------------------------------------
// FlowGraphNaturalLoop::MatchLimit: Try to pattern match the loop test of an
// induction variable.
//
// Parameters:
//   iterVar - Local number of potential IV
//   test    - Loop condition test
//   info    - [out] Info structure to fill out with information about the limit
//
// Returns:
//   True if the loop condition was recognized and "info" was filled out.
//
// Remarks:
//   Unlike the initialization, we do require that we are able to match the
//   loop condition.
//
bool FlowGraphNaturalLoop::MatchLimit(unsigned iterVar, GenTree* test, NaturalLoopIterInfo* info)
{
    info->HasConstLimit          = false;
    info->HasSimdLimit           = false;
    info->HasArrayLengthLimit    = false;
    info->HasInvariantLocalLimit = false;

    Compiler* comp = m_dfsTree->GetCompiler();

    // Obtain the relop from the "test" tree.
    GenTree* relop;
    if (test->OperIs(GT_JTRUE))
    {
        relop = test->gtGetOp1();
    }
    else
    {
        assert(test->OperIs(GT_STORE_LCL_VAR));
        relop = test->AsLclVar()->Data();
    }

    noway_assert(relop->OperIsCompare());

    GenTree* opr1 = relop->AsOp()->gtOp1;
    GenTree* opr2 = relop->AsOp()->gtOp2;

    GenTree* iterOp;
    GenTree* limitOp;

    // Make sure op1 or op2 is the iterVar.
    if (opr1->OperIsScalarLocal() && (opr1->AsLclVarCommon()->GetLclNum() == iterVar))
    {
        iterOp  = opr1;
        limitOp = opr2;
    }
    else if (opr2->OperIsScalarLocal() && (opr2->AsLclVarCommon()->GetLclNum() == iterVar))
    {
        iterOp  = opr2;
        limitOp = opr1;
    }
    else
    {
        return false;
    }

    if (!iterOp->TypeIs(TYP_INT))
    {
        return false;
    }

    // Check what type of limit we have - constant, variable or arr-len.
    if (limitOp->IsCnsIntOrI())
    {
        info->HasConstLimit = true;
        if ((limitOp->gtFlags & GTF_ICON_SIMD_COUNT) != 0)
        {
            info->HasSimdLimit = true;
        }
    }
    else if (limitOp->OperIs(GT_LCL_VAR))
    {
        // See if limit var is loop invariant
        //
        if (comp->lvaGetDesc(limitOp->AsLclVarCommon())->IsAddressExposed())
        {
            JITDUMP("    Limit var V%02u is address exposed\n", limitOp->AsLclVarCommon()->GetLclNum());
            return false;
        }

        GenTreeLclVarCommon* def = FindDef(limitOp->AsLclVarCommon()->GetLclNum());
        if (def != nullptr)
        {
            JITDUMP("    Limit var V%02u modified by [%06u]\n", limitOp->AsLclVarCommon()->GetLclNum(),
                    Compiler::dspTreeID(def));
            return false;
        }

        info->HasInvariantLocalLimit = true;
    }
    else if (limitOp->OperIs(GT_ARR_LENGTH))
    {
        // See if limit array is a loop invariant
        //
        GenTree* const array = limitOp->AsArrLen()->ArrRef();

        if (!array->OperIs(GT_LCL_VAR))
        {
            JITDUMP("    Array limit tree [%06u] not analyzable\n", Compiler::dspTreeID(limitOp));
            return false;
        }

        if (comp->lvaGetDesc(array->AsLclVarCommon())->IsAddressExposed())
        {
            JITDUMP("    Array base local V%02u is address exposed\n", array->AsLclVarCommon()->GetLclNum());
            return false;
        }

        GenTreeLclVarCommon* def = FindDef(array->AsLclVarCommon()->GetLclNum());
        if (def != nullptr)
        {
            JITDUMP("    Array limit var V%02u modified by [%06u]\n", array->AsLclVarCommon()->GetLclNum(),
                    Compiler::dspTreeID(def));
            return false;
        }

        info->HasArrayLengthLimit = true;
    }
    else
    {
        JITDUMP("    Loop limit tree [%06u] not analyzable\n", Compiler::dspTreeID(limitOp));
        return false;
    }

    // Were we able to successfully analyze the limit?
    //
    assert(info->HasConstLimit || info->HasInvariantLocalLimit || info->HasArrayLengthLimit);

    info->TestTree = relop;
    return true;
}

//------------------------------------------------------------------------
// EvaluateRelop: Evaluate a relational operator with constant arguments.
//
// Parameters:
//   op1  - First operand
//   op2  - Second operand
//   oper - Operator
//
// Returns:
//   Result.
//
template <typename T>
bool FlowGraphNaturalLoop::EvaluateRelop(T op1, T op2, genTreeOps oper)
{
    switch (oper)
    {
        case GT_EQ:
            return op1 == op2;

        case GT_NE:
            return op1 != op2;

        case GT_LT:
            return op1 < op2;

        case GT_LE:
            return op1 <= op2;

        case GT_GT:
            return op1 > op2;

        case GT_GE:
            return op1 >= op2;

        default:
            unreached();
    }
}

//------------------------------------------------------------------------
// CheckLoopConditionBaseCase: Verify that the loop condition is true when the
// loop is entered.
//
// Returns:
//   True if we could prove that the condition is true on entry.
//
// Remarks:
//   Currently handles the following cases:
//     * The condition being trivially true in the first iteration (e.g. for (int i = 0; i < 3; i++))
//     * The condition is checked before entry (often due to loop inversion)
//
bool FlowGraphNaturalLoop::CheckLoopConditionBaseCase(BasicBlock* initBlock, NaturalLoopIterInfo* info)
{
    // TODO: A common loop idiom is to enter the loop at the test, with the
    // unique in-loop predecessor of the header block being the increment. We
    // currently do not handle these patterns in `optExtractInitTestIncr`.
    // Instead we depend on loop inversion to put them into an
    //   if (x) { do { ... } while (x) }
    // form. Once we handle the pattern in `optExtractInitTestIncr` we can
    // handle it here by checking for whether the test is the header and first
    // thing in the header.

    // Is it trivially true?
    if (info->HasConstInit && info->HasConstLimit)
    {
        int initVal  = info->ConstInitValue;
        int limitVal = info->ConstLimit();

        assert(genActualType(info->TestTree->gtGetOp1()) == TYP_INT);

        bool isTriviallyTrue = info->TestTree->IsUnsigned()
                                   ? EvaluateRelop<uint32_t>((uint32_t)initVal, (uint32_t)limitVal, info->TestOper())
                                   : EvaluateRelop<int32_t>(initVal, limitVal, info->TestOper());

        if (isTriviallyTrue)
        {
            JITDUMP("  Condition is trivially true on entry (%d %s%s %d)\n", initVal,
                    info->TestTree->IsUnsigned() ? "(uns)" : "", GenTree::OpName(info->TestOper()), limitVal);
            return true;
        }
    }

    // Do we have a zero-trip test?
    if (initBlock->KindIs(BBJ_COND) && IsZeroTripTest(initBlock, info))
    {
        return true;
    }

    return false;
}

//------------------------------------------------------------------------
// IsZeroTripTest: Check whether `initBlock`, a BBJ_COND block that enters the
// loop in one case and not in the other, implies that the loop invariant is
// true on entry.
//
// Returns:
//   True if we could prove that the loop invariant is true on entry through
//   "initBlock".
//
bool FlowGraphNaturalLoop::IsZeroTripTest(BasicBlock* initBlock, NaturalLoopIterInfo* info)
{
    assert(initBlock->KindIs(BBJ_COND));
    GenTree* enteringJTrue = initBlock->lastStmt()->GetRootNode();
    assert(enteringJTrue->OperIs(GT_JTRUE));
    GenTree* relop = enteringJTrue->gtGetOp1();
    if (!relop->OperIsCmpCompare())
    {
        return false;
    }

    // Technically optExtractInitTestIncr only handles the "false"
    // entry case, and preheader creation should ensure that that's the
    // only time we'll see a BBJ_COND init block. However, it does not
    // hurt to let this logic be correct by construction.
    bool enterOnTrue = InitBlockEntersLoopOnTrue(initBlock);

    JITDUMP("  Init block " FMT_BB " enters the loop when condition [%06u] evaluates to %s\n", initBlock->bbNum,
            Compiler::dspTreeID(relop), enterOnTrue ? "true" : "false");

    GenTree*   limitCandidate;
    genTreeOps oper;

    if (relop->gtGetOp1()->OperIsScalarLocal() && (relop->gtGetOp1()->AsLclVarCommon()->GetLclNum() == info->IterVar))
    {
        JITDUMP("    op1 is the iteration variable\n");
        oper           = relop->gtOper;
        limitCandidate = relop->gtGetOp2();
    }
    else if (relop->gtGetOp2()->OperIsScalarLocal() &&
             (relop->gtGetOp2()->AsLclVarCommon()->GetLclNum() == info->IterVar))
    {
        JITDUMP("    op2 is the iteration variable\n");
        oper           = GenTree::SwapRelop(relop->gtOper);
        limitCandidate = relop->gtGetOp1();
    }
    else
    {
        JITDUMP("    Relop does not involve iteration variable\n");
        return false;
    }

    if (!enterOnTrue)
    {
        oper = GenTree::ReverseRelop(oper);
    }

    // Here we want to prove that [iterVar] [oper] [limitCandidate] implies
    // [iterVar] [info->IterOper()] [info->Limit()]. Currently we just do the
    // simple exact checks, but this could be improved. Note that using
    // `GenTree::Compare` for the limits is ok for a "same value" check for the
    // limited shapes of limits we recognize.
    //
    if ((relop->IsUnsigned() != info->TestTree->IsUnsigned()) || (oper != info->TestOper()) ||
        !GenTree::Compare(limitCandidate, info->Limit()))
    {
        JITDUMP("    Condition guarantees V%02u %s%s [%06u], but invariant requires V%02u %s%s [%06u]\n", info->IterVar,
                relop->IsUnsigned() ? "(uns) " : "", GenTree::OpName(oper), Compiler::dspTreeID(limitCandidate),
                info->IterVar, info->TestTree->IsUnsigned() ? "(uns) " : "", GenTree::OpName(info->TestOper()),
                Compiler::dspTreeID(info->Limit()));
        return false;
    }

    JITDUMP("  Condition is established before entry at [%06u]\n", Compiler::dspTreeID(enteringJTrue->gtGetOp1()));
    return true;
}

//------------------------------------------------------------------------
// InitBlockEntersLoopOnTrue: Determine whether a BBJ_COND init block enters the
// loop in the false or true case.
//
// Parameters:
//   initBlock - A BBJ_COND block that is assumed to dominate the loop, and
//   only enter the loop in one of the two cases.
//
// Returns:
//   True if the loop is entered if the condition evaluates to true; otherwise false.
//
// Remarks:
//   Handles only limited cases (optExtractInitTestIncr ensures that we see
//   only limited cases).
//
bool FlowGraphNaturalLoop::InitBlockEntersLoopOnTrue(BasicBlock* initBlock)
{
    assert(initBlock->KindIs(BBJ_COND));

    if (initBlock->FalseTargetIs(GetHeader()))
    {
        return false;
    }

    if (initBlock->TrueTargetIs(GetHeader()))
    {
        return true;
    }

    // `optExtractInitTestIncr` may look at preds of preds to find an init
    // block, so try a little bit harder. Today this always happens since we
    // always have preheaders created in the places we call
    // FlowGraphNaturalLoop::AnalyzeIteration.
    for (FlowEdge* enterEdge : EntryEdges())
    {
        BasicBlock* entering = enterEdge->getSourceBlock();
        if (initBlock->FalseTargetIs(entering))
        {
            return false;
        }
        if (initBlock->TrueTargetIs(entering))
        {
            return true;
        }
    }

    assert(!"Could not find init block enter side");
    return false;
}

//------------------------------------------------------------------------
// GetLexicallyTopMostBlock: Get the lexically top-most block contained within
// the loop.
//
// Returns:
//   First block in block order contained in the loop.
//
// Remarks:
//   Mostly exists as a quirk while transitioning from the old loop
//   representation to the new one.
//
BasicBlock* FlowGraphNaturalLoop::GetLexicallyTopMostBlock()
{
    BasicBlock* top = m_dfsTree->GetCompiler()->fgFirstBB;

    while (!ContainsBlock(top))
    {
        top = top->Next();
        assert(top != nullptr);
    }

    return top;
}

//------------------------------------------------------------------------
// GetLexicallyBottomMostBlock: Get the lexically bottom-most block contained
// within the loop.
//
// Returns:
//   Last block in block order contained in the loop.
//
// Remarks:
//   Mostly exists as a quirk while transitioning from the old loop
//   representation to the new one.
//
BasicBlock* FlowGraphNaturalLoop::GetLexicallyBottomMostBlock()
{
    BasicBlock* bottom = m_dfsTree->GetCompiler()->fgLastBB;

    while (!ContainsBlock(bottom))
    {
        bottom = bottom->Prev();
        assert(bottom != nullptr);
    }

    return bottom;
}

//------------------------------------------------------------------------
// HasDef: Check if a local is defined anywhere in the loop.
//
// Parameters:
//   lclNum - Local to check for a def for.
//
// Returns:
//   True if the local has any def.
//
bool FlowGraphNaturalLoop::HasDef(unsigned lclNum)
{
    Compiler*  comp = m_dfsTree->GetCompiler();
    LclVarDsc* dsc  = comp->lvaGetDesc(lclNum);

    assert(!comp->lvaVarAddrExposed(lclNum));
    // Currently does not handle promoted locals, only fields.
    assert(!dsc->lvPromoted);

    unsigned defLclNum1 = lclNum;
    unsigned defLclNum2 = BAD_VAR_NUM;
    if (dsc->lvIsStructField)
    {
        defLclNum2 = dsc->lvParentLcl;
    }

    bool result = VisitDefs([=](GenTreeLclVarCommon* lcl) {
        if ((lcl->GetLclNum() == defLclNum1) || (lcl->GetLclNum() == defLclNum2))
        {
            return false;
        }

        return true;
    });

    // If we stopped early we found a def.
    return !result;
}

//------------------------------------------------------------------------
// CanDuplicate: Check if this loop can be duplicated.
//
// Parameters:
//   reason - If this function returns false, the reason why.
//
// Returns:
//   True if the loop can be duplicated.
//
// Remarks:
//   Does not support duplicating loops with EH constructs in them.
//   (see CanDuplicateWithEH)
//
bool FlowGraphNaturalLoop::CanDuplicate(INDEBUG(const char** reason))
{
#ifdef DEBUG
    const char* localReason;
    if (reason == nullptr)
    {
        reason = &localReason;
    }
#endif

    Compiler*       comp   = m_dfsTree->GetCompiler();
    BasicBlockVisit result = VisitLoopBlocks([=](BasicBlock* block) {
        if (!BasicBlock::sameEHRegion(block, GetHeader()))
        {
            INDEBUG(*reason = "Loop not entirely within one EH region");
            return BasicBlockVisit::Abort;
        }

        return BasicBlockVisit::Continue;
    });

    return result != BasicBlockVisit::Abort;
}

//------------------------------------------------------------------------
// Duplicate: Duplicate the blocks of this loop, inserting them after `insertAfter`.
//
// Parameters:
//   insertAfter            - [in, out] Block to insert duplicated blocks after; updated to last block inserted.
//   map                    - A map that will have mappings from loop blocks to duplicated blocks added to it.
//   weightScale            - Factor to scale weight of new blocks by
//
void FlowGraphNaturalLoop::Duplicate(BasicBlock** insertAfter, BlockToBlockMap* map, weight_t weightScale)
{
    assert(CanDuplicate(nullptr));

    Compiler* comp = m_dfsTree->GetCompiler();

    VisitLoopBlocks([=](BasicBlock* blk) {
        // Initialize newBlk as BBJ_ALWAYS without jump target, and fix up jump target later
        // with BasicBlock::CopyTarget().
        BasicBlock* newBlk = comp->fgNewBBafter(BBJ_ALWAYS, *insertAfter, /*extendRegion*/ true);
        JITDUMP("Adding " FMT_BB " (copy of " FMT_BB ") after " FMT_BB "\n", newBlk->bbNum, blk->bbNum,
                (*insertAfter)->bbNum);

        BasicBlock::CloneBlockState(comp, newBlk, blk);

        // We're going to create the preds below, which will set the bbRefs properly,
        // so clear out the cloned bbRefs field.
        newBlk->bbRefs = 0;

        newBlk->scaleBBWeight(weightScale);

        *insertAfter = newBlk;
        map->Set(blk, newBlk, BlockToBlockMap::Overwrite);

        return BasicBlockVisit::Continue;
    });

    // Now go through the new blocks, remapping their jump targets within the loop
    // and updating the preds lists.
    VisitLoopBlocks([=](BasicBlock* blk) {
        BasicBlock* newBlk = nullptr;
        bool        b      = map->Lookup(blk, &newBlk);
        assert(b && newBlk != nullptr);

        // Jump target should not be set yet
        assert(!newBlk->HasInitializedTarget());

        // Redirect the new block according to "blockMap".
        // optSetMappedBlockTargets will set newBlk's successors, and add pred edges for the successors.
        comp->optSetMappedBlockTargets(blk, newBlk, map);

        return BasicBlockVisit::Continue;
    });
}

//------------------------------------------------------------------------
// CanDuplicateWithEH: Check if this loop (possibly containing try entries)
//   can be duplicated.
//
// Parameters:
//   reason - If this function returns false, the reason why.
//
// Returns:
//   True if the loop can be duplicated.
//
// Notes:
//   Extends CanDuplicate to cover loops with try region entries.
//
bool FlowGraphNaturalLoop::CanDuplicateWithEH(INDEBUG(const char** reason))
{
#ifdef DEBUG
    const char* localReason;
    if (reason == nullptr)
    {
        reason = &localReason;
    }
#endif

    Compiler*         comp   = m_dfsTree->GetCompiler();
    BasicBlock* const header = GetHeader();

    ArrayStack<BasicBlock*> tryRegionsToClone(comp->getAllocator(CMK_TryRegionClone));

    BasicBlockVisit result = VisitLoopBlocks([=, &tryRegionsToClone](BasicBlock* block) {
        const bool inSameRegionAsHeader = BasicBlock::sameEHRegion(block, header);

        if (inSameRegionAsHeader)
        {
            return BasicBlockVisit::Continue;
        }

        if (comp->bbIsTryBeg(block))
        {
            // Check if this is an "outermost" try within the loop.
            // If so, we have more checking to do later on.
            //
            bool const     headerIsInTry     = header->hasTryIndex();
            unsigned const blockTryIndex     = block->getTryIndex();
            unsigned const enclosingTryIndex = comp->ehTrueEnclosingTryIndex(blockTryIndex);

            if ((headerIsInTry && (enclosingTryIndex == header->getTryIndex())) ||
                (!headerIsInTry && (enclosingTryIndex == EHblkDsc::NO_ENCLOSING_INDEX)))
            {
                // When we clone a try we also clone its handler.
                //
                // This try may be enclosed in a handler whose try begin is in the loop.
                // If so we'll clone this try when we clone (the handler of) that try.
                //
                bool isInHandlerOfInLoopTry = false;
                if (block->hasHndIndex())
                {
                    unsigned const    enclosingHndIndex = block->getHndIndex();
                    BasicBlock* const associatedTryBeg  = comp->ehGetDsc(enclosingHndIndex)->ebdTryBeg;
                    isInHandlerOfInLoopTry              = this->ContainsBlock(associatedTryBeg);
                }

                if (!isInHandlerOfInLoopTry)
                {
                    tryRegionsToClone.Push(block);
                }
            }
        }

        return BasicBlockVisit::Continue;
    });

    // Check any enclosed try regions to make sure they can be cloned
    // (note this is potentially misleading with multiple trys as
    // we are considering cloning each in isolation).
    //
    const unsigned numberOfTryRegions = tryRegionsToClone.Height();
    if ((result != BasicBlockVisit::Abort) && (numberOfTryRegions > 0))
    {
        // Possibly limit to just 1 region.
        //
        JITDUMP(FMT_LP " contains %u top-level try region%s\n", GetIndex(), numberOfTryRegions,
                numberOfTryRegions > 1 ? "s" : "");

        while (tryRegionsToClone.Height() > 0)
        {
            BasicBlock* const tryEntry    = tryRegionsToClone.Pop();
            bool const        canCloneTry = comp->fgCanCloneTryRegion(tryEntry);

            if (!canCloneTry)
            {
                INDEBUG(*reason = "Loop contains uncloneable try region");
                result = BasicBlockVisit::Abort;
                break;
            }
        }
    }

    return result != BasicBlockVisit::Abort;
}

//------------------------------------------------------------------------
// DuplicateWithEH: Duplicate the blocks of this loop, inserting them after `insertAfter`,
//    and also fully clone any try regions
//
// Parameters:
//   insertAfter            - [in, out] Block to insert duplicated blocks after; updated to last block inserted.
//   map                    - A map that will have mappings from loop blocks to duplicated blocks added to it.
//   weightScale            - Factor to scale weight of new blocks by
//
// Notes:
//   Extends Duplicate to cover loops with try region entries.
//
void FlowGraphNaturalLoop::DuplicateWithEH(BasicBlock** insertAfter, BlockToBlockMap* map, weight_t weightScale)
{
    assert(CanDuplicateWithEH(nullptr));

    Compiler* const   comp           = m_dfsTree->GetCompiler();
    bool              clonedTry      = false;
    BasicBlock* const insertionPoint = *insertAfter;

    // If the insertion point is within an EH region, remember all the EH regions
    // current that end at the insertion point, so we can properly extend them
    // when we're done cloning.
    //
    struct RegionEnd
    {
        RegionEnd(unsigned regionIndex, BasicBlock* block, bool isTryEnd)
            : m_regionIndex(regionIndex)
            , m_block(block)
            , m_isTryEnd(isTryEnd)
        {
        }
        unsigned    m_regionIndex;
        BasicBlock* m_block;
        bool        m_isTryEnd;
    };

    ArrayStack<RegionEnd> regionEnds(comp->getAllocator(CMK_TryRegionClone));

    // Record enclosing EH region block references,
    // so we can keep track of what the "before" picture looked like.
    //
    if (insertionPoint->hasTryIndex() || insertionPoint->hasHndIndex())
    {
        bool     inTry  = false;
        unsigned region = comp->ehGetMostNestedRegionIndex(insertionPoint, &inTry);

        if (region != 0)
        {
            // Convert to true region index
            region--;

            while (true)
            {
                EHblkDsc* const ebd = comp->ehGetDsc(region);

                if (inTry)
                {
                    JITDUMP("Noting that enclosing try EH#%02u ends at " FMT_BB "\n", region, ebd->ebdTryLast->bbNum);
                    regionEnds.Emplace(region, ebd->ebdTryLast, true);
                }
                else
                {
                    JITDUMP("Noting that enclsoing handler EH#%02u ends at " FMT_BB "\n", region,
                            ebd->ebdHndLast->bbNum);
                    regionEnds.Emplace(region, ebd->ebdHndLast, false);
                }

                region = comp->ehGetEnclosingRegionIndex(region, &inTry);

                if (region == EHblkDsc::NO_ENCLOSING_INDEX)
                {
                    break;
                }
            }
        }
    }

    // Keep track of how much the EH indices change because of EH region cloning.
    //
    unsigned ehIndexShift = 0;

    // Keep track of which blocks were handled by EH region cloning
    //
    BitVecTraits traits(comp->compBasicBlockID, comp);
    BitVec       visited(BitVecOps::MakeEmpty(&traits));

    VisitLoopBlocks([=, &traits, &visited, &clonedTry, &ehIndexShift](BasicBlock* blk) {
        // Try cloning may have already handled this block
        //
        if (BitVecOps::IsMember(&traits, visited, blk->bbID))
        {
            return BasicBlockVisit::Continue;
        }

        // If this is a try region entry, clone the entire region now.
        // Defer adding edges and extending EH regions until later.
        //
        // Updates map, and insertAfter.
        //
        if (comp->bbIsTryBeg(blk))
        {
            CloneTryInfo info(traits);
            info.Map          = map;
            info.AddEdges     = false;
            info.ProfileScale = weightScale;

            BasicBlock* const clonedBlock = comp->fgCloneTryRegion(blk, info, insertAfter);

            assert(clonedBlock != nullptr);
            BitVecOps::UnionD(&traits, visited, info.Visited);
            ehIndexShift += info.EHIndexShift;
            clonedTry = true;
            return BasicBlockVisit::Continue;
        }
        else
        {
            // We're not expecting to find enclosed EH regions
            //
            assert(!comp->bbIsTryBeg(blk));
            assert(!comp->bbIsHandlerBeg(blk));
            assert(!BitVecOps::IsMember(&traits, visited, blk->bbID));
        }

        // `blk` was not in loop-enclosed try region or companion region.
        //
        // Initialize newBlk as BBJ_ALWAYS without jump target; these are fixed up subsequently.
        //
        // CloneBlockState puts newBlk in the proper EH region. We will fix enclosing region extents
        // once cloning is done.
        //
        BasicBlock* newBlk = comp->fgNewBBafter(BBJ_ALWAYS, *insertAfter, /* extendRegion */ false);
        JITDUMP("Adding " FMT_BB " (copy of " FMT_BB ") after " FMT_BB "\n", newBlk->bbNum, blk->bbNum,
                (*insertAfter)->bbNum);
        BasicBlock::CloneBlockState(comp, newBlk, blk);

        assert(newBlk->bbRefs == 0);
        newBlk->scaleBBWeight(weightScale);
        map->Set(blk, newBlk, BlockToBlockMap::Overwrite);
        *insertAfter = newBlk;

        return BasicBlockVisit::Continue;
    });

    // Note the EH table may have grown, if we cloned try regions. If there was
    // an enclosing EH entry, then its EH table entries will have shifted to
    // higher index values.
    //
    // Update the enclosing EH region ends to reflect the new blocks we added.
    // (here we assume cloned blocks are placed lexically after their originals, so if a
    // region-ending block was cloned, the new region end is the last block cloned).
    //
    // Note we don't consult the block references in EH table here, since they
    // may reflect interim updates to region endpoints (by fgCloneTry). Otherwise
    // we could simply call ehUpdateLastBlocks.
    //
    BasicBlock* const lastClonedBlock = *insertAfter;

    while (regionEnds.Height() > 0)
    {
        RegionEnd       r   = regionEnds.Pop();
        EHblkDsc* const ebd = comp->ehGetDsc(r.m_regionIndex + ehIndexShift);

        if (r.m_block == insertionPoint)
        {
            if (r.m_isTryEnd)
            {
                comp->fgSetTryEnd(ebd, lastClonedBlock);
            }
            else
            {
                comp->fgSetHndEnd(ebd, lastClonedBlock);
            }
        }
        else
        {
            if (r.m_isTryEnd)
            {
                comp->fgSetTryEnd(ebd, r.m_block);
            }
            else
            {
                comp->fgSetHndEnd(ebd, r.m_block);
            }
        }
    }

    // Now go through the new blocks, remapping their jump targets within the loop
    // and updating the preds lists.
    //
    VisitLoopBlocks([=](BasicBlock* blk) {
        BasicBlock* newBlk = nullptr;
        bool        b      = map->Lookup(blk, &newBlk);
        assert(b && newBlk != nullptr);

        JITDUMP("Updating targets: " FMT_BB " mapped to " FMT_BB "\n", blk->bbNum, newBlk->bbNum);

        // Jump target should not be set yet
        assert(!newBlk->HasInitializedTarget());

        // Redirect the new block according to "blockMap".
        // optSetMappedBlockTargets will set newBlk's successors, and add pred edges for the successors.
        comp->optSetMappedBlockTargets(blk, newBlk, map);

        return BasicBlockVisit::Continue;
    });

    // If we cloned any EH regions, we may have some non-loop blocks to process as well.
    //
    if (clonedTry)
    {
        for (BasicBlock* const blk : BlockToBlockMap::KeyIteration(map))
        {
            if (!ContainsBlock(blk))
            {
                BasicBlock* newBlk = nullptr;
                bool        b      = map->Lookup(blk, &newBlk);
                assert(b && newBlk != nullptr);
                assert(!newBlk->HasInitializedTarget());
                comp->optSetMappedBlockTargets(blk, newBlk, map);
            }
        }
    }
}

//------------------------------------------------------------------------
// MayExecuteBlockMultipleTimesPerIteration:
//   Check if the loop may execute a particular loop block multiple times for
//   each iteration.
//
// Parameters:
//   block - The basic block
//
// Returns:
//   True if so. May return true even if the true answer is false.
//
bool FlowGraphNaturalLoop::MayExecuteBlockMultipleTimesPerIteration(BasicBlock* block)
{
    assert(ContainsBlock(block));

    if (ContainsImproperHeader())
    {
        // To be more precise we could check if 'block' can reach itself
        // without going through the header, but this case is rare.
        return true;
    }

    for (FlowGraphNaturalLoop* child = GetChild(); child != nullptr; child = child->GetSibling())
    {
        if (child->ContainsBlock(block))
        {
            return true;
        }
    }

    return false;
}

//------------------------------------------------------------------------
// IsPostDominatedOnLoopIteration:
//   Check whether control will always flow through "postDominator" if starting
//   at "block" and a backedge is taken.
//
// Parameters:
//   block         - The basic block
//   postDominator - Block to query postdominance of
//
// Returns:
//   True if so.
//
bool FlowGraphNaturalLoop::IsPostDominatedOnLoopIteration(BasicBlock* block, BasicBlock* postDominator)
{
    assert(ContainsBlock(block) && ContainsBlock(postDominator));

    unsigned index;
    bool     gotIndex = TryGetLoopBlockBitVecIndex(block, &index);
    assert(gotIndex);

    Compiler*               comp = m_dfsTree->GetCompiler();
    ArrayStack<BasicBlock*> stack(comp->getAllocator(CMK_Loops));

    BitVecTraits traits = LoopBlockTraits();
    BitVec       visited(BitVecOps::MakeEmpty(&traits));

    stack.Push(block);
    BitVecOps::AddElemD(&traits, visited, index);

    auto queueSuccs = [=, &stack, &traits, &visited](BasicBlock* succ) {
        if (succ == m_header)
        {
            // We managed to reach the header without going through "postDominator".
            return BasicBlockVisit::Abort;
        }

        unsigned index;
        if (!TryGetLoopBlockBitVecIndex(succ, &index) || !BitVecOps::IsMember(&traits, m_blocks, index))
        {
            // Block is not inside loop
            return BasicBlockVisit::Continue;
        }

        if (!BitVecOps::TryAddElemD(&traits, visited, index))
        {
            // Block already visited
            return BasicBlockVisit::Continue;
        }

        stack.Push(succ);
        return BasicBlockVisit::Continue;
    };

    while (stack.Height() > 0)
    {
        BasicBlock* block = stack.Pop();
        if (block == postDominator)
        {
            continue;
        }

        if (block->VisitAllSuccs(comp, queueSuccs) == BasicBlockVisit::Abort)
        {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------
// IterConst: Get the constant with which the iterator is modified
//
// Returns:
//   Constant value.
//
int NaturalLoopIterInfo::IterConst()
{
    GenTree* value = IterTree->AsLclVar()->Data();
    return (int)value->gtGetOp2()->AsIntCon()->IconValue();
}

//------------------------------------------------------------------------
// IterOper: Get the type of the operation on the iterator
//
// Returns:
//   Oper
//
genTreeOps NaturalLoopIterInfo::IterOper()
{
    return IterTree->AsLclVar()->Data()->OperGet();
}

//------------------------------------------------------------------------
// IterOperType: Get the type of the operation on the iterator.
//
// Returns:
//   Type, used for overflow instructions.
//
var_types NaturalLoopIterInfo::IterOperType()
{
    assert(genActualType(IterTree) == TYP_INT);
    return IterTree->TypeGet();
}

//------------------------------------------------------------------------
// IsReversed: Returns true if the iterator node is the second operand in the
// loop condition.
//
// Returns:
//   True if so.
//
bool NaturalLoopIterInfo::IsReversed()
{
    return TestTree->gtGetOp2()->OperIsScalarLocal() && (TestTree->gtGetOp2()->AsLclVar()->GetLclNum() == IterVar);
}

//------------------------------------------------------------------------
// TestOper: The type of the comparison between the iterator and the limit
// (GT_LE, GT_GE, etc.)
//
// Returns:
//   Oper.
//
genTreeOps NaturalLoopIterInfo::TestOper()
{
    genTreeOps op = TestTree->OperGet();
    if (IsReversed())
    {
        op = GenTree::SwapRelop(op);
    }
    if (ExitedOnTrue)
    {
        op = GenTree::ReverseRelop(op);
    }
    return op;
}

//------------------------------------------------------------------------
// IsIncreasingLoop: Returns true if the loop iterator increases from low to
// high value.
//
// Returns:
//   True if so.
//
bool NaturalLoopIterInfo::IsIncreasingLoop()
{
    // Increasing loop is the one that has "+=" increment operation and "< or <=" limit check.
    bool isLessThanLimitCheck = GenTree::StaticOperIs(TestOper(), GT_LT, GT_LE);
    return (isLessThanLimitCheck &&
            (((IterOper() == GT_ADD) && (IterConst() > 0)) || ((IterOper() == GT_SUB) && (IterConst() < 0))));
}

//------------------------------------------------------------------------
// IsIncreasingLoop: Returns true if the loop iterator decreases from high to
// low value.
//
// Returns:
//   True if so.
//
bool NaturalLoopIterInfo::IsDecreasingLoop()
{
    // Decreasing loop is the one that has "-=" decrement operation and "> or >=" limit check. If the operation is
    // "+=", make sure the constant is negative to give an effect of decrementing the iterator.
    bool isGreaterThanLimitCheck = GenTree::StaticOperIs(TestOper(), GT_GT, GT_GE);
    return (isGreaterThanLimitCheck &&
            (((IterOper() == GT_ADD) && (IterConst() < 0)) || ((IterOper() == GT_SUB) && (IterConst() > 0))));
}

//------------------------------------------------------------------------
// Iterator: Get the iterator node in the loop test
//
// Returns:
//   Iterator node.
//
GenTree* NaturalLoopIterInfo::Iterator()
{
    return IsReversed() ? TestTree->gtGetOp2() : TestTree->gtGetOp1();
}

//------------------------------------------------------------------------
// Limit: Get the limit node in the loop test.
//
// Returns:
//   Iterator node.
//
GenTree* NaturalLoopIterInfo::Limit()
{
    return IsReversed() ? TestTree->gtGetOp1() : TestTree->gtGetOp2();
}

//------------------------------------------------------------------------
// ConstLimit: Get the constant value of the iterator limit, i.e. when the loop
// condition is "i RELOP const".
//
// Returns:
//   Limit constant.
//
// Remarks:
//   Only valid if HasConstLimit is true.
//
int NaturalLoopIterInfo::ConstLimit()
{
    assert(HasConstLimit);
    GenTree* limit = Limit();
    assert(limit->OperIsConst());
    return (int)limit->AsIntCon()->gtIconVal;
}

//------------------------------------------------------------------------
// VarLimit: Get the local var num used in the loop condition, i.e. when the
// loop condition is "i RELOP lclVar" with a loop invariant local.
//
// Returns:
//   Local var number.
//
// Remarks:
//   Only valid if HasInvariantLocalLimit is true.
//
unsigned NaturalLoopIterInfo::VarLimit()
{
    assert(HasInvariantLocalLimit);

    GenTree* limit = Limit();
    assert(limit->OperIs(GT_LCL_VAR));
    return limit->AsLclVarCommon()->GetLclNum();
}

//------------------------------------------------------------------------
// ArrLenLimit: Get the array length used in the loop condition, i.e. when the
// loop condition is "i RELOP arr.len".
//
// Parameters:
//   comp  - Compiler instance
//   index - [out] Array index information
//
// Returns:
//   True if the array length was extracted.
//
// Remarks:
//   Only valid if HasArrayLengthLimit is true.
//
bool NaturalLoopIterInfo::ArrLenLimit(Compiler* comp, ArrIndex* index)
{
    assert(HasArrayLengthLimit);

    GenTree* limit = Limit();
    assert(limit->OperIs(GT_ARR_LENGTH));

    // Check if we have a.length or a[i][j].length
    if (limit->AsArrLen()->ArrRef()->OperIs(GT_LCL_VAR))
    {
        index->arrLcl = limit->AsArrLen()->ArrRef()->AsLclVarCommon()->GetLclNum();
        index->rank   = 0;
        return true;
    }
    // We have a[i].length, extract a[i] pattern.
    else if (limit->AsArrLen()->ArrRef()->OperIs(GT_COMMA))
    {
        return comp->optReconstructArrIndex(limit->AsArrLen()->ArrRef(), index);
    }
    return false;
}

//------------------------------------------------------------------------
// FlowGraphDominatorTree::IntersectDom:
//   Find common IDom parent, much like least common ancestor.
//
// Parameters:
//   finger1 - A basic block that might share IDom ancestor with finger2.
//   finger2 - A basic block that might share IDom ancestor with finger1.
//
// Returns:
//   A basic block that is the dominator for finger1 and finger2. This can be
//   called while the dominator tree is still being computed, in which case the
//   returned result may not be the "latest" such dominator (but will converge
//   towards it with more iterations over the basic blocks).
//
// Remarks:
//   See "A simple, fast dominance algorithm" by Keith D. Cooper, Timothy J.
//   Harvey, Ken Kennedy.
//
BasicBlock* FlowGraphDominatorTree::IntersectDom(BasicBlock* finger1, BasicBlock* finger2)
{
    assert((finger1 != nullptr) && (finger2 != nullptr));

    while (finger1 != finger2)
    {
        while (finger1->bbPostorderNum < finger2->bbPostorderNum)
        {
            finger1 = finger1->bbIDom;
            assert(finger1 != nullptr);
        }
        while (finger2->bbPostorderNum < finger1->bbPostorderNum)
        {
            finger2 = finger2->bbIDom;
            assert(finger2 != nullptr);
        }
    }
    return finger1;
}

//------------------------------------------------------------------------
// FlowGraphDominatorTree::Intersect:
//   See FlowGraphDominatorTree::IntersectDom.
//
BasicBlock* FlowGraphDominatorTree::Intersect(BasicBlock* block1, BasicBlock* block2)
{
    return IntersectDom(block1, block2);
}

//------------------------------------------------------------------------
// FlowGraphDominatorTree::Dominates:
//   Check if node "dominator" is an ancestor of node "dominated".
//
// Parameters:
//   dominator - Node that may dominate
//   dominated - Node that may be dominated
//
// Returns:
//   True "dominator" dominates "dominated".
//
bool FlowGraphDominatorTree::Dominates(BasicBlock* dominator, BasicBlock* dominated)
{
    assert(m_dfsTree->Contains(dominator) && m_dfsTree->Contains(dominated));

    // What we want to ask here is basically if A is in the middle of the path
    // from B to the root (the entry node) in the dominator tree. Turns out
    // that can be translated as:
    //
    //   A dom B <-> preorder(A) <= preorder(B) && postorder(A) >= postorder(B)
    //
    // where the equality holds when you ask if A dominates itself.
    //
    return (m_preorderNum[dominator->bbPostorderNum] <= m_preorderNum[dominated->bbPostorderNum]) &&
           (m_postorderNum[dominator->bbPostorderNum] >= m_postorderNum[dominated->bbPostorderNum]);
}

#ifdef DEBUG
//------------------------------------------------------------------------
// FlowGraphDominatorTree::Dump: Dump a textual representation of the dominator
// tree.
//
void FlowGraphDominatorTree::Dump()
{
    Compiler* comp = m_dfsTree->GetCompiler();

    for (BasicBlock* block : comp->Blocks())
    {
        if (!m_dfsTree->Contains(block) || (m_domTree[block->bbPostorderNum].firstChild == nullptr))
            continue;

        printf(FMT_BB " : ", block->bbNum);
        for (BasicBlock* child = m_domTree[block->bbPostorderNum].firstChild; child != nullptr;
             child             = m_domTree[child->bbPostorderNum].nextSibling)
        {
            printf(FMT_BB " ", child->bbNum);
        }
        printf("\n");
    }

    printf("\n");
}
#endif

//------------------------------------------------------------------------
// FlowGraphDominatorTree::Build: Compute the dominator tree for the blocks in
// the DFS tree.
//
// Parameters:
//   dfsTree - DFS tree.
//
// Returns:
//   Data structure representing dominator tree. Immediate dominators are
//   marked directly into the BasicBlock structures, in the bbIDom field, so
//   multiple instances cannot be simultaneously used.
//
// Remarks:
//   As a precondition it is required that the flow graph has a unique root.
//   This might require creating a scratch root block in case the first block
//   has backedges or is in a try region.
//
FlowGraphDominatorTree* FlowGraphDominatorTree::Build(const FlowGraphDfsTree* dfsTree)
{
    Compiler*    comp      = dfsTree->GetCompiler();
    BasicBlock** postOrder = dfsTree->GetPostOrder();
    unsigned     count     = dfsTree->GetPostOrderCount();

    // Reset BlockPredsWithEH cache.
    comp->m_blockToEHPreds = nullptr;
    comp->m_dominancePreds = nullptr;

    assert((comp->fgFirstBB->bbPreds == nullptr) && !comp->fgFirstBB->hasTryIndex());
    assert(postOrder[count - 1] == comp->fgFirstBB);
    comp->fgFirstBB->bbIDom = nullptr;

    // First compute immediate dominators.
    unsigned numIters = 0;
    bool     changed;
    do
    {
        changed = false;

        // In reverse post order, except for the entry block (count - 1 is entry BB).
        for (unsigned i = count - 1; i > 0; i--)
        {
            unsigned    poNum = i - 1;
            BasicBlock* block = postOrder[poNum];

            // Intersect DOM, if computed, for all predecessors.
            BasicBlock* bbIDom = nullptr;
            for (FlowEdge* pred = comp->BlockDominancePreds(block); pred; pred = pred->getNextPredEdge())
            {
                BasicBlock* domPred = pred->getSourceBlock();
                if (!dfsTree->Contains(domPred))
                {
                    continue; // Unreachable pred
                }

                if ((numIters <= 0) && (domPred->bbPostorderNum <= poNum))
                {
                    continue; // Pred not yet visited
                }

                if (bbIDom == nullptr)
                {
                    bbIDom = domPred;
                }
                else
                {
                    bbIDom = IntersectDom(bbIDom, domPred);
                }
            }

            assert(bbIDom != nullptr);
            // Did we change the bbIDom value?  If so, we go around the outer loop again.
            if (block->bbIDom != bbIDom)
            {
                changed       = true;
                block->bbIDom = bbIDom;
            }
        }

        numIters++;
    } while (changed && dfsTree->HasCycle());

    // Now build dominator tree.
    DomTreeNode* domTree = new (comp, CMK_DominatorMemory) DomTreeNode[count]{};

    // Build the child and sibling links based on the immediate dominators.
    // Running this loop in post-order means we end up with sibling links in
    // reverse post-order. Skip the root since it has no siblings.
    for (unsigned i = 0; i < count - 1; i++)
    {
        BasicBlock* block  = postOrder[i];
        BasicBlock* parent = block->bbIDom;
        assert(parent != nullptr);
        assert(dfsTree->Contains(block) && dfsTree->Contains(parent));

        domTree[i].nextSibling                     = domTree[parent->bbPostorderNum].firstChild;
        domTree[parent->bbPostorderNum].firstChild = block;
    }

#ifdef DEBUG
    if (comp->verbose)
    {
        printf("After computing the dominance tree:\n");
        for (unsigned i = count; i > 0; i--)
        {
            unsigned poNum = i - 1;
            if (domTree[poNum].firstChild == nullptr)
            {
                continue;
            }

            printf(FMT_BB " :", postOrder[poNum]->bbNum);
            for (BasicBlock* child = domTree[poNum].firstChild; child != nullptr;
                 child             = domTree[child->bbPostorderNum].nextSibling)
            {
                printf(" " FMT_BB, child->bbNum);
            }
            printf("\n");
        }
        printf("\n");
    }
#endif

    // Assign preorder/postorder nums for fast "domnates" queries.
    class NumberDomTreeVisitor : public DomTreeVisitor<NumberDomTreeVisitor>
    {
        unsigned* m_preorderNums;
        unsigned* m_postorderNums;
        unsigned  m_preNum  = 0;
        unsigned  m_postNum = 0;

    public:
        NumberDomTreeVisitor(Compiler* comp, unsigned* preorderNums, unsigned* postorderNums)
            : DomTreeVisitor(comp)
            , m_preorderNums(preorderNums)
            , m_postorderNums(postorderNums)
        {
        }

        void PreOrderVisit(BasicBlock* block)
        {
            m_preorderNums[block->bbPostorderNum] = m_preNum++;
        }

        void PostOrderVisit(BasicBlock* block)
        {
            m_postorderNums[block->bbPostorderNum] = m_postNum++;
        }
    };

    unsigned* preorderNums  = new (comp, CMK_DominatorMemory) unsigned[count];
    unsigned* postorderNums = new (comp, CMK_DominatorMemory) unsigned[count];

    NumberDomTreeVisitor number(comp, preorderNums, postorderNums);
    number.WalkTree(domTree);

    return new (comp, CMK_DominatorMemory) FlowGraphDominatorTree(dfsTree, domTree, preorderNums, postorderNums);
}

FlowGraphDominanceFrontiers::FlowGraphDominanceFrontiers(FlowGraphDominatorTree* domTree)
    : m_domTree(domTree)
    , m_map(domTree->GetDfsTree()->GetCompiler()->getAllocator(CMK_DominatorMemory))
    , m_poTraits(domTree->GetDfsTree()->PostOrderTraits())
    , m_visited(BitVecOps::MakeEmpty(&m_poTraits))
{
}

//------------------------------------------------------------------------
// FlowGraphDominanceFrontiers::Build: Build the dominance frontiers for all
// blocks.
//
// Parameters:
//   domTree - Dominator tree to build dominance frontiers for
//
// Returns:
//   Data structure representing dominance frontiers.
//
// Remarks:
//   Recall that the dominance frontier of a block B is the set of blocks B3
//   such that there exists some B2 s.t. B3 is a successor of B2, and B
//   dominates B2 but not B3. Note that this dominance need not be strict -- B2
//   and B may be the same node.
//
//   In other words, a block B' is in DF(B) if B dominates an immediate
//   predecessor of B', but does not dominate B'. Intuitively, these blocks are
//   the "first" blocks that are no longer dominated by B; these are the places
//   we are interested in inserting phi definitions that may refer to defs in
//   B.
//
//   See "A simple, fast dominance algorithm", by Cooper, Harvey, and Kennedy.
//
FlowGraphDominanceFrontiers* FlowGraphDominanceFrontiers::Build(FlowGraphDominatorTree* domTree)
{
    const FlowGraphDfsTree* dfsTree = domTree->GetDfsTree();
    Compiler*               comp    = dfsTree->GetCompiler();

    FlowGraphDominanceFrontiers* result = new (comp, CMK_DominatorMemory) FlowGraphDominanceFrontiers(domTree);

    for (unsigned i = 0; i < dfsTree->GetPostOrderCount(); i++)
    {
        BasicBlock* block = dfsTree->GetPostOrder(i);

        // Recall that B3 is in the dom frontier of B1 if there exists a B2
        // such that B1 dom B2, !(B1 dom B3), and B3 is an immediate successor
        // of B2.  (Note that B1 might be the same block as B2.)
        // In that definition, we're considering "block" to be B3, and trying
        // to find B1's.  To do so, first we consider the predecessors of "block",
        // searching for candidate B2's -- "block" is obviously an immediate successor
        // of its immediate predecessors.  If there are zero or one preds, then there
        // is no pred, or else the single pred dominates "block", so no B2 exists.
        FlowEdge* blockPreds = comp->BlockPredsWithEH(block);

        // If block has 0/1 predecessor, skip, apart from handler entry blocks
        // that are always in the dominance frontier of its enclosed blocks.
        if (!comp->bbIsHandlerBeg(block) && ((blockPreds == nullptr) || (blockPreds->getNextPredEdge() == nullptr)))
        {
            continue;
        }

        // Otherwise, there are > 1 preds.  Each is a candidate B2 in the definition --
        // *unless* it dominates "block"/B3.

        for (FlowEdge* pred = blockPreds; pred != nullptr; pred = pred->getNextPredEdge())
        {
            BasicBlock* predBlock = pred->getSourceBlock();

            if (!dfsTree->Contains(predBlock))
            {
                continue;
            }

            // If we've found a B2, then consider the possible B1's.  We start with
            // B2, since a block dominates itself, then traverse upwards in the dominator
            // tree, stopping when we reach the root, or the immediate dominator of "block"/B3.
            // (Note that we are guaranteed to encounter this immediate dominator of "block"/B3:
            // a predecessor must be dominated by B3's immediate dominator.)
            // Along this way, make "block"/B3 part of the dom frontier of the B1.
            // When we reach this immediate dominator, the definition no longer applies, since this
            // potential B1 *does* dominate "block"/B3, so we stop.
            for (BasicBlock* b1 = predBlock; (b1 != nullptr) && (b1 != block->bbIDom); // !root && !loop
                 b1             = b1->bbIDom)
            {
                BlkVector& b1DF = *result->m_map.Emplace(b1, comp->getAllocator(CMK_DominatorMemory));
                // It's possible to encounter the same DF multiple times, ensure that we don't add duplicates.
                if (b1DF.empty() || (b1DF.back() != block))
                {
                    b1DF.push_back(block);
                }
            }
        }
    }

    return result;
}

//------------------------------------------------------------------------
// ComputeIteratedDominanceFrontier: Compute the iterated dominance frontier of
// a block. This is the transitive closure of taking dominance frontiers.
//
// Parameters:
//   block  - Block to compute iterated dominance frontier for.
//   result - Vector to add blocks of IDF into.
//
// Remarks:
//   When we create phi definitions we are creating new definitions that
//   themselves induce the creation of more phi nodes. Thus, the transitive
//   closure of DF(B) contains all blocks that may have phi definitions
//   referring to defs in B, or referring to other phis referring to defs in B.
//
void FlowGraphDominanceFrontiers::ComputeIteratedDominanceFrontier(BasicBlock* block, BlkVector* result)
{
    assert(result->empty());

    BlkVector* bDF = m_map.LookupPointer(block);

    if (bDF == nullptr)
    {
        return;
    }

    // Compute IDF(b) - start by adding DF(b) to IDF(b).
    result->reserve(bDF->size());
    BitVecOps::ClearD(&m_poTraits, m_visited);

    for (BasicBlock* f : *bDF)
    {
        BitVecOps::AddElemD(&m_poTraits, m_visited, f->bbPostorderNum);
        result->push_back(f);
    }

    // Now for each block f from IDF(b) add DF(f) to IDF(b). This may result in new
    // blocks being added to IDF(b) and the process repeats until no more new blocks
    // are added. Note that since we keep adding to bIDF we can't use iterators as
    // they may get invalidated. This happens to be a convenient way to avoid having
    // to track newly added blocks in a separate set.
    for (size_t newIndex = 0; newIndex < result->size(); newIndex++)
    {
        BasicBlock* f   = (*result)[newIndex];
        BlkVector*  fDF = m_map.LookupPointer(f);

        if (fDF == nullptr)
        {
            continue;
        }

        for (BasicBlock* ff : *fDF)
        {
            if (BitVecOps::TryAddElemD(&m_poTraits, m_visited, ff->bbPostorderNum))
            {
                result->push_back(ff);
            }
        }
    }
}

#ifdef DEBUG
//------------------------------------------------------------------------
// FlowGraphDominanceFrontiers::Dump: Dump a textual representation of the
// dominance frontiers to jitstdout.
//
void FlowGraphDominanceFrontiers::Dump()
{
    printf("DF:\n");
    for (unsigned i = 0; i < m_domTree->GetDfsTree()->GetPostOrderCount(); ++i)
    {
        BasicBlock* b = m_domTree->GetDfsTree()->GetPostOrder(i);
        printf("Block " FMT_BB " := {", b->bbNum);

        BlkVector* bDF = m_map.LookupPointer(b);
        if (bDF != nullptr)
        {
            int index = 0;
            for (BasicBlock* f : *bDF)
            {
                printf("%s" FMT_BB, (index++ == 0) ? "" : ",", f->bbNum);
            }
        }
        printf("}\n");
    }
}
#endif

//------------------------------------------------------------------------
// BlockToNaturalLoopMap::GetLoop: Map a block back to its most nested
// containing loop.
//
// Parameters:
//   block - The block
//
// Returns:
//   Loop or nullptr if the block is not contained in any loop.
//
FlowGraphNaturalLoop* BlockToNaturalLoopMap::GetLoop(BasicBlock* block)
{
    const FlowGraphDfsTree* dfs = m_loops->GetDfsTree();
    if (!dfs->Contains(block))
    {
        return nullptr;
    }

    unsigned index = m_indices[block->bbPostorderNum];
    if (index == UINT_MAX)
    {
        return nullptr;
    }

    return m_loops->GetLoopByIndex(index);
}

//------------------------------------------------------------------------
// BlockToNaturalLoopMap::Build: Build the map.
//
// Parameters:
//   loops - Data structure describing loops
//
// Returns:
//   The map.
//
BlockToNaturalLoopMap* BlockToNaturalLoopMap::Build(FlowGraphNaturalLoops* loops)
{
    const FlowGraphDfsTree* dfs  = loops->GetDfsTree();
    Compiler*               comp = dfs->GetCompiler();
    unsigned*               indices =
        dfs->GetPostOrderCount() == 0 ? nullptr : (new (comp, CMK_Loops) unsigned[dfs->GetPostOrderCount()]);

    for (unsigned i = 0; i < dfs->GetPostOrderCount(); i++)
    {
        indices[i] = UINT_MAX;
    }

    // Now visit all loops in reverse post order, meaning that we see inner
    // loops last and thus write their indices into the map last.
    for (FlowGraphNaturalLoop* loop : loops->InReversePostOrder())
    {
        loop->VisitLoopBlocks([=](BasicBlock* block) {
            indices[block->bbPostorderNum] = loop->GetIndex();
            return BasicBlockVisit::Continue;
        });
    }

    return new (comp, CMK_Loops) BlockToNaturalLoopMap(loops, indices);
}

#ifdef DEBUG

//------------------------------------------------------------------------
// BlockToNaturalLoopMap::Dump: Dump a textual representation of the map.
//
void BlockToNaturalLoopMap::Dump() const
{
    const FlowGraphDfsTree* dfs        = m_loops->GetDfsTree();
    unsigned                blockCount = dfs->GetPostOrderCount();

    printf("Block -> natural loop map: %u blocks\n", blockCount);
    if (blockCount > 0)
    {
        printf("block : loop index\n");
        for (unsigned i = 0; i < blockCount; i++)
        {
            if (m_indices[i] == UINT_MAX)
            {
                // Just leave the loop space empty if there is no enclosing loop
                printf(FMT_BB " : \n", dfs->GetPostOrder(i)->bbNum);
            }
            else
            {
                printf(FMT_BB " : " FMT_LP "\n", dfs->GetPostOrder(i)->bbNum, m_indices[i]);
            }
        }
    }
}

#endif // DEBUG

//------------------------------------------------------------------------
// BlockReachabilitySets::Build: Build the reachability sets.
//
// Parameters:
//   dfsTree - DFS tree
//
// Returns:
//   The sets.
//
// Remarks:
//   This algorithm consumes O(n^2) memory because we're using dense
//   bitsets to represent reachability.
//
BlockReachabilitySets* BlockReachabilitySets::Build(const FlowGraphDfsTree* dfsTree)
{
    Compiler*    comp            = dfsTree->GetCompiler();
    BitVecTraits postOrderTraits = dfsTree->PostOrderTraits();
    BitVec*      sets            = new (comp, CMK_Reachability) BitVec[dfsTree->GetPostOrderCount()];

    for (unsigned i = 0; i < dfsTree->GetPostOrderCount(); i++)
    {
        sets[i] = BitVecOps::MakeSingleton(&postOrderTraits, i);
    }

    // Find the reachable blocks. Also, set BBF_GC_SAFE_POINT.
    bool     change;
    unsigned changedIterCount = 1;
    do
    {
        change = false;

        for (unsigned i = dfsTree->GetPostOrderCount(); i != 0; i--)
        {
            BasicBlock* const block = dfsTree->GetPostOrder(i - 1);

            for (BasicBlock* const predBlock : block->PredBlocks())
            {
                change |= BitVecOps::UnionDChanged(&postOrderTraits, sets[block->bbPostorderNum],
                                                   sets[predBlock->bbPostorderNum]);
            }
        }

        ++changedIterCount;
    } while (change);

#if COUNT_BASIC_BLOCKS
    computeReachabilitySetsIterationTable.record(changedIterCount);
#endif // COUNT_BASIC_BLOCKS

    BlockReachabilitySets* reachabilitySets = new (comp, CMK_Reachability) BlockReachabilitySets(dfsTree, sets);

#ifdef DEBUG
    if (comp->verbose)
    {
        printf("\nAfter computing reachability sets:\n");
        reachabilitySets->Dump();
    }
#endif

    return reachabilitySets;
}

//------------------------------------------------------------------------
// BlockReachabilitySets::CanReach: Check if "from" can flow to "to" through
// only regular control flow edges.
//
// Parameters:
//   from - Start block
//   to   - Candidate destination block
//
// Returns:
//   True if so.
//
bool BlockReachabilitySets::CanReach(BasicBlock* from, BasicBlock* to)
{
    assert(m_dfsTree->Contains(from));

    if (!m_dfsTree->Contains(to))
    {
        return false;
    }

    BitVecTraits poTraits = m_dfsTree->PostOrderTraits();
    return BitVecOps::IsMember(&poTraits, m_reachabilitySets[to->bbPostorderNum], from->bbPostorderNum);
}

#ifdef DEBUG
//------------------------------------------------------------------------
// BlockReachabilitySets::Dump: Dump the reachability sets to stdout.
//
void BlockReachabilitySets::Dump()
{
    printf("------------------------------------------------\n");
    printf("BBnum  Reachable by \n");
    printf("------------------------------------------------\n");

    Compiler*    comp            = m_dfsTree->GetCompiler();
    BitVecTraits postOrderTraits = m_dfsTree->PostOrderTraits();

    for (BasicBlock* const block : comp->Blocks())
    {
        printf(FMT_BB " : ", block->bbNum);
        if (m_dfsTree->Contains(block))
        {
            BitVecOps::Iter iter(&postOrderTraits, m_reachabilitySets[block->bbPostorderNum]);
            unsigned        poNum = 0;
            const char*     sep   = "";
            while (iter.NextElem(&poNum))
            {
                printf("%s" FMT_BB, sep, m_dfsTree->GetPostOrder(poNum)->bbNum);
                sep = " ";
            }
        }
        else
        {
            printf("[unreachable]");
        }
        printf("\n");
    }
}
#endif
