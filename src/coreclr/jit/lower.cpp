// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

/*XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XX                                                                           XX
XX                               Lower                                       XX
XX                                                                           XX
XX  Preconditions:                                                           XX
XX                                                                           XX
XX  Postconditions (for the nodes currently handled):                        XX
XX    - All operands requiring a register are explicit in the graph          XX
XX                                                                           XX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
*/

#include "jitpch.h"
#ifdef _MSC_VER
#pragma hdrstop
#endif

#include "lower.h"

#if !defined(TARGET_64BIT)
#include "decomposelongs.h"
#endif // !defined(TARGET_64BIT)

//------------------------------------------------------------------------
// MakeSrcContained: Make "childNode" a contained node
//
// Arguments:
//    parentNode - is a non-leaf node that can contain its 'childNode'
//    childNode  - is an op that will now be contained by its parent.
//
// Notes:
//    If 'childNode' it has any existing sources, they will now be sources for the parent.
//
void Lowering::MakeSrcContained(GenTree* parentNode, GenTree* childNode) const
{
    assert(!parentNode->OperIsLeaf());
    assert(childNode->canBeContained());

    childNode->SetContained();
    assert(childNode->isContained());

#ifdef DEBUG
    if (IsContainableMemoryOp(childNode))
    {
        // Verify caller of this method checked safety.
        //
        const bool isSafeToContainMem = IsSafeToContainMem(parentNode, childNode);

        if (!isSafeToContainMem)
        {
            JITDUMP("** Unsafe mem containment of [%06u] in [%06u}\n", comp->dspTreeID(childNode),
                    comp->dspTreeID(parentNode));
            assert(isSafeToContainMem);
        }
    }
#endif
}

//------------------------------------------------------------------------
// MakeSrcRegOptional: Make "childNode" a regOptional node
//
// Arguments:
//    parentNode - is a non-leaf node that can regOptional its 'childNode'
//    childNode  - is an op that will now be regOptional to its parent.
//
void Lowering::MakeSrcRegOptional(GenTree* parentNode, GenTree* childNode) const
{
    assert(!parentNode->OperIsLeaf());

    childNode->SetRegOptional();
    assert(childNode->IsRegOptional());

#ifdef DEBUG
    // Verify caller of this method checked safety.
    //
    const bool isSafeToMarkRegOptional = IsSafeToMarkRegOptional(parentNode, childNode);

    if (!isSafeToMarkRegOptional)
    {
        JITDUMP("** Unsafe regOptional of [%06u] in [%06u}\n", comp->dspTreeID(childNode), comp->dspTreeID(parentNode));
        assert(isSafeToMarkRegOptional);
    }
#endif
}

//------------------------------------------------------------------------
// TryMakeSrcContainedOrRegOptional: Tries to make "childNode" a contained or regOptional node
//
// Arguments:
//    parentNode - is a non-leaf node that can contain or regOptional its 'childNode'
//    childNode  - is an op that will now be contained or regOptional to its parent.
//
void Lowering::TryMakeSrcContainedOrRegOptional(GenTree* parentNode, GenTree* childNode) const
{
    // HWIntrinsic nodes should use TryGetContainableHWIntrinsicOp and its relevant handling
    assert(!parentNode->OperIsHWIntrinsic());

    if (IsContainableMemoryOp(childNode) && IsSafeToContainMem(parentNode, childNode))
    {
        MakeSrcContained(parentNode, childNode);
    }
    else if (IsSafeToMarkRegOptional(parentNode, childNode))
    {
        MakeSrcRegOptional(parentNode, childNode);
    }
}

//------------------------------------------------------------------------
// CheckImmedAndMakeContained: Checks if the 'childNode' is a containable immediate
//    and, if so, makes it contained.
//
// Arguments:
//    parentNode - is any non-leaf node
//    childNode  - is an child op of 'parentNode'
//
// Return value:
//     true if we are able to make childNode a contained immediate
//
bool Lowering::CheckImmedAndMakeContained(GenTree* parentNode, GenTree* childNode)
{
    assert(!parentNode->OperIsLeaf());
    // If childNode is a containable immediate
    if (IsContainableImmed(parentNode, childNode))
    {
        // then make it contained within the parentNode
        MakeSrcContained(parentNode, childNode);
        return true;
    }
    return false;
}

//------------------------------------------------------------------------
// IsInvariantInRange: Check if a node is invariant in the specified range. In
// other words, can 'node' be moved to right before 'endExclusive' without its
// computation changing values?
//
// Arguments:
//    node         -  The node.
//    endExclusive -  The exclusive end of the range to check invariance for.
//
// Returns:
//    True if 'node' can be evaluated at any point between its current
//    location and 'endExclusive' without giving a different result; otherwise
//    false.
//
bool Lowering::IsInvariantInRange(GenTree* node, GenTree* endExclusive) const
{
    return m_scratchSideEffects.IsLirInvariantInRange(comp, node, endExclusive);
}

//------------------------------------------------------------------------
// IsInvariantInRange: Check if a node is invariant in the specified range,
// ignoring conflicts with one particular node.
//
// Arguments:
//    node         - The node.
//    endExclusive - The exclusive end of the range to check invariance for.
//    ignoreNode   - A node to ignore interference checks with, for example
//                   because it will retain its relative order with 'node'.
//
// Returns:
//    True if 'node' can be evaluated at any point between its current location
//    and 'endExclusive' without giving a different result; otherwise false.
//
bool Lowering::IsInvariantInRange(GenTree* node, GenTree* endExclusive, GenTree* ignoreNode) const
{
    return m_scratchSideEffects.IsLirInvariantInRange(comp, node, endExclusive, ignoreNode);
}

//------------------------------------------------------------------------
// IsRangeInvariantInRange: Check if a range of nodes are invariant in the
// specified range.
//
// Arguments:
//    rangeStart   - The first node.
//    rangeEnd     - The last node.
//    endExclusive - The exclusive end of the range to check invariance for.
//    ignoreNode   - A node to ignore interference checks with, for example
//                   because it will retain its relative order with 'node'.
//
// Returns:
//    True if the range can be evaluated at any point between its current location
//    and 'endExclusive' without giving a different result; otherwise false.
//
// Remarks:
//    Note that the range is treated as a unit and no pairwise interference
//    checks between nodes in the range are performed.
//
bool Lowering::IsRangeInvariantInRange(GenTree* rangeStart,
                                       GenTree* rangeEnd,
                                       GenTree* endExclusive,
                                       GenTree* ignoreNode) const
{
    return m_scratchSideEffects.IsLirRangeInvariantInRange(comp, rangeStart, rangeEnd, endExclusive, ignoreNode);
}

//------------------------------------------------------------------------
// IsSafeToContainMem: Checks for conflicts between childNode and parentNode,
// and returns 'true' iff memory operand childNode can be contained in parentNode.
//
// Arguments:
//    parentNode - any non-leaf node
//    childNode  - some node that is an input to `parentNode`
//
// Return value:
//    true if it is safe to make childNode a contained memory operand.
//
bool Lowering::IsSafeToContainMem(GenTree* parentNode, GenTree* childNode) const
{
    return IsInvariantInRange(childNode, parentNode);
}

//------------------------------------------------------------------------
// IsSafeToContainMem: Checks for conflicts between childNode and grandParentNode
// and returns 'true' iff memory operand childNode can be contained in grandParentNode.
//
// Arguments:
//    grandParentNode - any non-leaf node
//    parentNode - parent of `childNode` and an input to `grandParentNode`
//    childNode  - some node that is an input to `parentNode`
//
// Return value:
//    true if it is safe to make childNode a contained memory operand.
//
bool Lowering::IsSafeToContainMem(GenTree* grandparentNode, GenTree* parentNode, GenTree* childNode) const
{
    return IsInvariantInRange(childNode, grandparentNode, parentNode);
}

//------------------------------------------------------------------------
// IsSafeToMarkRegOptional: Check whether it is safe to mark 'childNode' as
// reg-optional in 'parentNode'.
//
// Arguments:
//    parentNode - parent of 'childNode'
//    childNode  - some node that is an input to `parentNode`
//
// Return value:
//    True if it is safe to mark childNode as reg-optional; otherwise false.
//
// Remarks:
//    Unlike containment, reg-optionality can only rarely introduce new
//    conflicts, because reg-optionality mostly does not cause the child node
//    to be evaluated at a new point in time:
//
//    1. For LIR edges (i.e. anything that isn't GT_LCL_VAR) reg-optionality
//       indicates that if the edge was spilled to a temp at its def, the parent
//       node can use it directly from its spill location without reloading it
//       into a register first. This is always safe as as spill temps cannot
//       interfere.
//
//       For example, an indirection can be marked reg-optional even if there
//       is interference between it and its parent; the indirection will still
//       be evaluated at its original position, but if the value is spilled to
//       stack, then reg-optionality can allow using the value from the spill
//       location directly. Similarly, GT_LCL_FLD nodes are never register
//       candidates and can be handled the same way.
//
//    2. For GT_LCL_VAR reg-optionality indicates that the node can use the
//       local directly from its home location. IR invariants guarantee that the
//       local is not defined between its LIR location and the parent node (see
//       CheckLclVarSemanticsHelper). That means the only case where it could
//       interfere is due to it being address exposed. So this is the only unsafe
//       case.
//
bool Lowering::IsSafeToMarkRegOptional(GenTree* parentNode, GenTree* childNode) const
{
    if (!childNode->OperIs(GT_LCL_VAR))
    {
        // LIR edges never interfere. This includes GT_LCL_FLD, see the remarks above.
        return true;
    }

    LclVarDsc* dsc = comp->lvaGetDesc(childNode->AsLclVarCommon());
    if (!dsc->IsAddressExposed())
    {
        // Safe by IR invariants (no stores occur between parent and node).
        return true;
    }

    // We expect this to have interference as otherwise we could have marked it
    // contained instead of reg-optional.
    return false;
}

//------------------------------------------------------------------------
// LowerRange:
//   Lower the specified range of nodes.
//
// Arguments:
//   firstNode - First node to lower
//   lastNode  - Last node to lower
//
void Lowering::LowerRange(GenTree* firstNode, GenTree* lastNode)
{
    assert(lastNode != nullptr);

    // Multiple possible behaviors of LowerNode are possible:
    // 1. The node being lowered may be removed
    // 2. The node being lowered may be replaced by a new region of nodes and
    //    ask lowering to go back to those nodes
    // 3. The node being lowered may have its user removed
    //
    // The solution here does not actually try to handle the 3rd case.
    //
    GenTree* stopNode = lastNode->gtNext;

    for (GenTree* cur = firstNode; cur != stopNode;)
    {
        cur = LowerNode(cur);
    }
}

//------------------------------------------------------------------------
// IsProfitableToSetZeroFlag: Checks if it's profitable to optimize an shift
// and rotate operations to set the zero flag.
//
// Arguments:
//    op - The operation node to check.
//
// Return value:
//    true if it's profitable to set the zero flag; otherwise false.
//
bool Lowering::IsProfitableToSetZeroFlag(GenTree* op) const
{
#ifdef TARGET_XARCH
    if (op->OperIs(GT_LSH, GT_RSH, GT_RSZ, GT_ROR, GT_ROL))
    {
        // BMI2 instructions (SHLX, SARX, SHRX, RORX) do not set zero flag.
        if (!op->AsOp()->gtGetOp2()->OperIsConst())
        {
            return false;
        }
    }

#endif // TARGET_XARCH

    return true;
}

//------------------------------------------------------------------------
// LowerNode: this is the main entry point for Lowering.
//
// Arguments:
//    node - the node we are lowering.
//
// Returns:
//    next node in the transformed node sequence that needs to be lowered.
//
GenTree* Lowering::LowerNode(GenTree* node)
{
    assert(node != nullptr);
    switch (node->gtOper)
    {
        case GT_NULLCHECK:
        case GT_IND:
        {
            return LowerIndir(node->AsIndir());
        }

        case GT_STOREIND:
            return LowerStoreIndirCommon(node->AsStoreInd());

        case GT_ADD:
        {
            GenTree* next = LowerAdd(node->AsOp());
            if (next != nullptr)
            {
                return next;
            }
        }
        break;

#if !defined(TARGET_64BIT)
        case GT_ADD_LO:
        case GT_ADD_HI:
        case GT_SUB_LO:
        case GT_SUB_HI:
#endif
        case GT_SUB:
        case GT_AND:
        case GT_OR:
        case GT_XOR:
        {
            if (comp->opts.OptimizationEnabled())
            {
                GenTree* nextNode = nullptr;
                if (node->OperIs(GT_AND) && TryLowerAndNegativeOne(node->AsOp(), &nextNode))
                {
                    return nextNode;
                }
                assert(nextNode == nullptr);

                nextNode = node->gtNext;
                if (node->OperIs(GT_AND, GT_OR, GT_XOR) && TryFoldBinop(node->AsOp()))
                {
                    return nextNode;
                }
            }

            return LowerBinaryArithmetic(node->AsOp());
        }

        case GT_MUL:
        case GT_MULHI:
#if defined(TARGET_X86) || defined(TARGET_ARM64)
        case GT_MUL_LONG:
#endif
            return LowerMul(node->AsOp());

        case GT_UDIV:
        case GT_UMOD:
            if (!LowerUnsignedDivOrMod(node->AsOp()))
            {
                ContainCheckDivOrMod(node->AsOp());
            }
            break;

        case GT_DIV:
        case GT_MOD:
            return LowerSignedDivOrMod(node);

        case GT_SWITCH:
            return LowerSwitch(node);

        case GT_CALL:
        {
            GenTree* newNode = LowerCall(node);
            if (newNode != nullptr)
            {
                return newNode;
            }
#ifdef TARGET_ARM64
            m_ffrTrashed = true;
#endif
        }
        break;

        case GT_LT:
        case GT_LE:
        case GT_GT:
        case GT_GE:
        case GT_EQ:
        case GT_NE:
        case GT_TEST_EQ:
        case GT_TEST_NE:
        case GT_CMP:
            return LowerCompare(node);

        case GT_JTRUE:
            return LowerJTrue(node->AsOp());

        case GT_NEG:
#ifdef TARGET_ARM64
        {
            GenTree* next;
            if (TryLowerNegToMulLongOp(node->AsOp(), &next))
            {
                return next;
            }
            ContainCheckNeg(node->AsOp());
        }
#endif
        break;
        case GT_NOT:
#ifdef TARGET_ARM64
            ContainCheckNot(node->AsOp());
#endif
            break;
        case GT_SELECT:
            return LowerSelect(node->AsConditional());

        case GT_SELECTCC:
            ContainCheckSelect(node->AsOp());
            break;

        case GT_JMP:
            LowerJmpMethod(node);
            break;

        case GT_SWIFT_ERROR_RET:
        case GT_RETURN:
            LowerRet(node->AsOp());
            break;

        case GT_RETURNTRAP:
            ContainCheckReturnTrap(node->AsOp());
            break;

        case GT_CAST:
        {
            GenTree* nextNode = node->gtNext;
            if (TryRemoveCast(node->AsCast()))
            {
                return nextNode;
            }

            LowerCast(node);
            break;
        }

        case GT_BITCAST:
        {
            GenTree* next = node->gtNext;
            if (!TryRemoveBitCast(node->AsUnOp()))
            {
                ContainCheckBitCast(node->AsUnOp());
            }
            return next;
        }

#if defined(TARGET_XARCH) || defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
        case GT_BOUNDS_CHECK:
            ContainCheckBoundsChk(node->AsBoundsChk());
            break;
#endif // defined(TARGET_XARCH) || defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)

        case GT_ROL:
        case GT_ROR:
        {
            GenTree* next = node->gtNext;
            if (comp->opts.OptimizationEnabled() && TryFoldBinop(node->AsOp()))
            {
                return next;
            }

            LowerRotate(node);
            break;
        }

#ifndef TARGET_64BIT
        case GT_LSH_HI:
        case GT_RSH_LO:
            ContainCheckShiftRotate(node->AsOp());
            break;
#endif // !TARGET_64BIT

        case GT_LSH:
        case GT_RSH:
        case GT_RSZ:
        {
            GenTree* next = node->gtNext;
            if (comp->opts.OptimizationEnabled() && TryFoldBinop(node->AsOp()))
            {
                return next;
            }

#if defined(TARGET_XARCH) || defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
            LowerShift(node->AsOp());
#else
            ContainCheckShiftRotate(node->AsOp());
#endif
            break;
        }

        case GT_STORE_BLK:
            if (node->AsBlk()->Data()->IsCall())
            {
                LowerStoreSingleRegCallStruct(node->AsBlk());
                break;
            }
            LowerBlockStoreCommon(node->AsBlk());
            break;

        case GT_LCLHEAP:
            LowerLclHeap(node);
            break;

#ifdef TARGET_XARCH
        case GT_INTRINSIC:
            ContainCheckIntrinsic(node->AsOp());
            break;
#endif // TARGET_XARCH

#ifdef FEATURE_HW_INTRINSICS
        case GT_HWINTRINSIC:
            return LowerHWIntrinsic(node->AsHWIntrinsic());
#endif // FEATURE_HW_INTRINSICS

        case GT_LCL_FLD:
        {
            // We should only encounter this for lclVars that are lvDoNotEnregister.
            verifyLclFldDoNotEnregister(node->AsLclVarCommon()->GetLclNum());
            break;
        }

        case GT_LCL_VAR:
        {
            GenTreeLclVar* lclNode = node->AsLclVar();
            WidenSIMD12IfNecessary(lclNode);
            LclVarDsc* varDsc = comp->lvaGetDesc(lclNode);

            // The consumer of this node must check compatibility of the fields.
            // This merely checks whether it is possible for this to be a multireg node.
            if (lclNode->IsMultiRegLclVar())
            {
                if (!varDsc->lvPromoted ||
                    (comp->lvaGetPromotionType(varDsc) != Compiler::PROMOTION_TYPE_INDEPENDENT) ||
                    (varDsc->lvFieldCnt > MAX_MULTIREG_COUNT))
                {
                    lclNode->ClearMultiReg();
                    if (lclNode->TypeIs(TYP_STRUCT))
                    {
                        comp->lvaSetVarDoNotEnregister(lclNode->GetLclNum() DEBUGARG(DoNotEnregisterReason::BlockOp));
                    }
                }
            }
            break;
        }

        case GT_STORE_LCL_VAR:
            WidenSIMD12IfNecessary(node->AsLclVarCommon());
            FALLTHROUGH;

        case GT_STORE_LCL_FLD:
            return LowerStoreLocCommon(node->AsLclVarCommon());

#if defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
        case GT_CMPXCHG:
            RISCV64_ONLY(CheckImmedAndMakeContained(node, node->AsCmpXchg()->Data()));
            CheckImmedAndMakeContained(node, node->AsCmpXchg()->Comparand());
            break;

        case GT_XORR:
        case GT_XAND:
        case GT_XADD:
        case GT_XCHG:
            CheckImmedAndMakeContained(node, node->AsOp()->gtOp2);
            break;
#elif defined(TARGET_XARCH)
        case GT_XADD:
            if (node->IsUnusedValue())
            {
                node->ClearUnusedValue();
                // Make sure the types are identical, since the node type is changed to VOID
                // CodeGen relies on op2's type to determine the instruction size.
                // Note that the node type cannot be a small int but the data operand can.
                assert(genActualType(node->gtGetOp2()->TypeGet()) == node->TypeGet());
                node->SetOper(GT_LOCKADD);
                node->gtType = TYP_VOID;
                CheckImmedAndMakeContained(node, node->gtGetOp2());
            }
            break;
#endif

        case GT_KEEPALIVE:
            node->gtGetOp1()->SetRegOptional();
            break;

        case GT_LCL_ADDR:
        {
            const GenTreeLclVarCommon* lclAddr = node->AsLclVarCommon();
            const LclVarDsc*           varDsc  = comp->lvaGetDesc(lclAddr);
            if (!varDsc->lvDoNotEnregister)
            {
                // TODO-Cleanup: this is definitely not the best place for this detection,
                // but for now it is the easiest. Move it to morph.
                comp->lvaSetVarDoNotEnregister(lclAddr->GetLclNum() DEBUGARG(DoNotEnregisterReason::LclAddrNode));
            }
        }
        break;

#if defined(FEATURE_HW_INTRINSICS) && defined(TARGET_XARCH)
        case GT_BSWAP:
        case GT_BSWAP16:
            LowerBswapOp(node->AsOp());
            break;
#endif // FEATURE_HW_INTRINSICS && TARGET_XARCH

        case GT_ARR_LENGTH:
        case GT_MDARR_LENGTH:
        case GT_MDARR_LOWER_BOUND:
            return LowerArrLength(node->AsArrCommon());

        case GT_ASYNC_CONTINUATION:
            return LowerAsyncContinuation(node);

        case GT_RETURN_SUSPEND:
            LowerReturnSuspend(node);
            break;

#if defined(FEATURE_HW_INTRINSICS) && defined(TARGET_ARM64)
        case GT_CNS_MSK:
            return LowerCnsMask(node->AsMskCon());
#endif // FEATURE_HW_INTRINSICS && TARGET_ARM64

        default:
            break;
    }

    return node->gtNext;
}

//------------------------------------------------------------------------
// LowerArrLength: lower an array length
//
// Arguments:
//    node - the array length node we are lowering.
//
// Returns:
//    next node that needs to be lowered.
//
// Notes:
//    If base array is nullptr, this effectively
//    turns into a nullcheck.
//
GenTree* Lowering::LowerArrLength(GenTreeArrCommon* node)
{
    GenTree* const arr       = node->ArrRef();
    int            lenOffset = 0;

    switch (node->OperGet())
    {
        case GT_ARR_LENGTH:
        {
            lenOffset = node->AsArrLen()->ArrLenOffset();
            noway_assert(lenOffset == OFFSETOF__CORINFO_Array__length ||
                         lenOffset == OFFSETOF__CORINFO_String__stringLen);
            break;
        }

        case GT_MDARR_LENGTH:
            lenOffset = (int)comp->eeGetMDArrayLengthOffset(node->AsMDArr()->Rank(), node->AsMDArr()->Dim());
            break;

        case GT_MDARR_LOWER_BOUND:
            lenOffset = (int)comp->eeGetMDArrayLowerBoundOffset(node->AsMDArr()->Rank(), node->AsMDArr()->Dim());
            break;

        default:
            unreached();
    }

    // Create the expression `*(array_addr + lenOffset)`

    GenTree* addr;
    noway_assert(arr->gtNext == node);

    if (arr->OperIs(GT_CNS_INT) && (arr->AsIntCon()->gtIconVal == 0))
    {
        // If the array is NULL, then we should get a NULL reference
        // exception when computing its length.  We need to maintain
        // an invariant where there is no sum of two constants node, so
        // let's simply return an indirection of NULL.

        addr = arr;
    }
    else
    {
        GenTree* con = comp->gtNewIconNode(lenOffset, TYP_I_IMPL);
        addr         = comp->gtNewOperNode(GT_ADD, TYP_BYREF, arr, con);
        BlockRange().InsertAfter(arr, con, addr);
    }

    // Change to a GT_IND.
    node->ChangeOper(GT_IND);
    node->AsIndir()->Addr() = addr;

    return arr->gtNext;
}

/**  -- Switch Lowering --
 * The main idea of switch lowering is to keep transparency of the register requirements of this node
 * downstream in LSRA.  Given that the switch instruction is inherently a control statement which in the JIT
 * is represented as a simple tree node, at the time we actually generate code for it we end up
 * generating instructions that actually modify the flow of execution that imposes complicated
 * register requirement and lifetimes.
 *
 * So, for the purpose of LSRA, we want to have a more detailed specification of what a switch node actually
 * means and more importantly, which and when do we need a register for each instruction we want to issue
 * to correctly allocate them downstream.
 *
 * For this purpose, this procedure performs switch lowering in two different ways:
 *
 * a) Represent the switch statement as a zero-index jump table construct.  This means that for every destination
 *    of the switch, we will store this destination in an array of addresses and the code generator will issue
 *    a data section where this array will live and will emit code that based on the switch index, will indirect and
 *    jump to the destination specified in the jump table.
 *
 *    For this transformation we introduce a new GT node called GT_SWITCH_TABLE that is a specialization of the switch
 *    node for jump table based switches.
 *    The overall structure of a GT_SWITCH_TABLE is:
 *
 *    GT_SWITCH_TABLE
 *           |_________ localVar   (a temporary local that holds the switch index)
 *           |_________ jumpTable  (this is a special node that holds the address of the jump table array)
 *
 *     Now, the way we morph a GT_SWITCH node into this lowered switch table node form is the following:
 *
 *    Input:     GT_SWITCH (inside a basic block whose Branch Type is BBJ_SWITCH)
 *                    |_____ expr (an arbitrarily complex GT_NODE that represents the switch index)
 *
 *    This gets transformed into the following statements inside a BBJ_COND basic block (the target would be
 *    the default case of the switch in case the conditional is evaluated to true).
 *
 *     ----- original block, transformed
 *     GT_STORE_LCL_VAR tempLocal (a new temporary local variable used to store the switch index)
 *        |_____ expr      (the index expression)
 *
 *     GT_JTRUE
 *        |_____ GT_COND
 *                 |_____ GT_GE
 *                           |___ Int_Constant  (This constant is the index of the default case
 *                                               that happens to be the highest index in the jump table).
 *                           |___ tempLocal     (The local variable were we stored the index expression).
 *
 *     ----- new basic block
 *     GT_SWITCH_TABLE
 *        |_____ tempLocal
 *        |_____ jumpTable (a new jump table node that now LSRA can allocate registers for explicitly
 *                          and LinearCodeGen will be responsible to generate downstream).
 *
 *     This way there are no implicit temporaries.

 * b) For small-sized switches, we will actually morph them into a series of conditionals of the form
 *     if (case falls into the default){ goto jumpTable[size]; // last entry in the jump table is the default case }
 *     (For the default case conditional, we'll be constructing the exact same code as the jump table case one).
 *     else if (case == firstCase){ goto jumpTable[1]; }
 *     else if (case == secondCase) { goto jumptable[2]; } and so on.
 *
 *     This transformation is of course made in JIT-IR, not downstream to CodeGen level, so this way we no longer
 *     require internal temporaries to maintain the index we're evaluating plus we're using existing code from
 *     LinearCodeGen to implement this instead of implement all the control flow constructs using InstrDscs and
 *     InstrGroups downstream.
 */

GenTree* Lowering::LowerSwitch(GenTree* node)
{
    assert(node->OperIs(GT_SWITCH));

    // The first step is to build the default case conditional construct that is
    // shared between both kinds of expansion of the switch node.

    // To avoid confusion, we'll alias m_block to originalSwitchBB
    // that represents the node we're morphing.
    BasicBlock* originalSwitchBB = m_block;
    LIR::Range& switchBBRange    = LIR::AsRange(originalSwitchBB);

    // jumpCnt is the number of elements in the jump table array.
    // jumpTab is the actual pointer to the jump table array.
    // targetCnt is the number of unique targets in the jump table array.
    // uniqueSuccs is the array of the switch's unique successors.
    const unsigned   jumpCnt     = originalSwitchBB->GetSwitchTargets()->GetCaseCount();
    FlowEdge** const jumpTab     = originalSwitchBB->GetSwitchTargets()->GetCases();
    unsigned         targetCnt   = originalSwitchBB->GetSwitchTargets()->GetSuccCount();
    FlowEdge** const uniqueSuccs = originalSwitchBB->GetSwitchTargets()->GetSuccs();

// GT_SWITCH must be a top-level node with no use.
#ifdef DEBUG
    {
        LIR::Use use;
        assert(!switchBBRange.TryGetUse(node, &use));
    }
#endif

    JITDUMP("Lowering switch " FMT_BB ", %d cases\n", originalSwitchBB->bbNum, jumpCnt);

    // Handle a degenerate case: if the switch has only a default case, just convert it
    // to an unconditional branch. This should only happen in minopts or with debuggable
    // code.
    if (targetCnt == 1)
    {
        JITDUMP("Lowering switch " FMT_BB ": single target; converting to BBJ_ALWAYS\n", originalSwitchBB->bbNum);
        noway_assert(comp->opts.OptimizationDisabled());
        originalSwitchBB->SetKindAndTargetEdge(BBJ_ALWAYS, jumpTab[0]);

        // Remove extra predecessor links if there was more than one case.
        const unsigned dupCount = originalSwitchBB->GetTargetEdge()->getDupCount();
        originalSwitchBB->GetTargetEdge()->decrementDupCount(dupCount - 1);
        originalSwitchBB->GetTarget()->bbRefs -= (dupCount - 1);

        // We have to get rid of the GT_SWITCH node but a child might have side effects so just assign
        // the result of the child subtree to a temp.
        GenTree* rhs = node->AsOp()->gtOp1;

        unsigned lclNum               = comp->lvaGrabTemp(true DEBUGARG("Lowering is creating a new local variable"));
        comp->lvaTable[lclNum].lvType = rhs->TypeGet();

        GenTreeLclVar* store = comp->gtNewStoreLclVarNode(lclNum, rhs);

        switchBBRange.InsertAfter(node, store);
        switchBBRange.Remove(node);

        return store;
    }

    noway_assert(jumpCnt >= 2);

    // Spill the argument to the switch node into a local so that it can be used later.
    LIR::Use use(switchBBRange, &(node->AsOp()->gtOp1), node);
    ReplaceWithLclVar(use);

    // GT_SWITCH(indexExpression) is now two statements:
    //   1. a statement containing temp = indexExpression
    //   2. and a statement with GT_SWITCH(temp)

    assert(node->OperIs(GT_SWITCH));
    GenTree* temp = node->AsOp()->gtOp1;
    assert(temp->OperIs(GT_LCL_VAR));
    unsigned  tempLclNum  = temp->AsLclVarCommon()->GetLclNum();
    var_types tempLclType = temp->TypeGet();

    BasicBlock* defaultBB   = jumpTab[jumpCnt - 1]->getDestinationBlock();
    BasicBlock* followingBB = originalSwitchBB->Next();

    /* Is the number of cases right for a test and jump switch? */
    const bool fFirstCaseFollows = (followingBB == jumpTab[0]->getDestinationBlock());
    const bool fDefaultFollows   = (followingBB == defaultBB);

    unsigned minSwitchTabJumpCnt = 2; // table is better than just 2 cmp/jcc

    // This means really just a single cmp/jcc (aka a simple if/else)
    if (fFirstCaseFollows || fDefaultFollows)
    {
        minSwitchTabJumpCnt++;
    }

#if defined(TARGET_ARM)
    // On ARM for small switch tables we will
    // generate a sequence of compare and branch instructions
    // because the code to load the base of the switch
    // table is huge and hideous due to the relocation... :(
    minSwitchTabJumpCnt += 2;
#endif // TARGET_ARM

    // Once we have the temporary variable, we construct the conditional branch for
    // the default case.  As stated above, this conditional is being shared between
    // both GT_SWITCH lowering code paths.
    // This condition is of the form: if (temp > jumpTableLength - 2){ goto jumpTable[jumpTableLength - 1]; }
    GenTree* gtDefaultCaseCond = comp->gtNewOperNode(GT_GT, TYP_INT, comp->gtNewLclvNode(tempLclNum, tempLclType),
                                                     comp->gtNewIconNode(jumpCnt - 2, genActualType(tempLclType)));

    // Make sure we perform an unsigned comparison, just in case the switch index in 'temp'
    // is now less than zero 0 (that would also hit the default case).
    gtDefaultCaseCond->gtFlags |= GTF_UNSIGNED;

    GenTree* gtDefaultCaseJump = comp->gtNewOperNode(GT_JTRUE, TYP_VOID, gtDefaultCaseCond);
    gtDefaultCaseJump->gtFlags = node->gtFlags;

    LIR::Range condRange = LIR::SeqTree(comp, gtDefaultCaseJump);
    switchBBRange.InsertAtEnd(std::move(condRange));

    BasicBlock* afterDefaultCondBlock = comp->fgSplitBlockAfterNode(originalSwitchBB, condRange.LastNode());

    // afterDefaultCondBlock is now the switch, and all the switch targets have it as a predecessor.
    // originalSwitchBB is now a BBJ_ALWAYS, and there is a predecessor edge in afterDefaultCondBlock
    // representing the fall-through flow from originalSwitchBB.
    assert(originalSwitchBB->KindIs(BBJ_ALWAYS));
    assert(originalSwitchBB->TargetIs(afterDefaultCondBlock));
    assert(originalSwitchBB->JumpsToNext());
    assert(afterDefaultCondBlock->KindIs(BBJ_SWITCH));
    assert(afterDefaultCondBlock->GetSwitchTargets()->HasDefaultCase());
    assert(afterDefaultCondBlock->isEmpty()); // Nothing here yet.

    // The GT_SWITCH code is still in originalSwitchBB (it will be removed later).

    // Fix the pred for the default case: the default block target still has originalSwitchBB
    // as a predecessor, but the fgSplitBlockAfterStatement() moved all predecessors to point
    // to afterDefaultCondBlock.

    // Note defaultEdge may also be the edge for some switch cases. We only probe edges,
    // so assume each possibility is equally likely.
    FlowEdge* const defaultEdge       = jumpTab[jumpCnt - 1];
    weight_t const  defaultLikelihood = defaultEdge->getLikelihood() / defaultEdge->getDupCount();
    comp->fgRemoveRefPred(defaultEdge);
    FlowEdge* const trueEdge = comp->fgAddRefPred(defaultBB, originalSwitchBB);
    trueEdge->setLikelihood(defaultLikelihood);
    defaultEdge->setLikelihood(defaultEdge->getLikelihood() - defaultLikelihood);

    // Turn originalSwitchBB into a BBJ_COND.
    FlowEdge* const falseEdge        = originalSwitchBB->GetTargetEdge();
    weight_t const  switchLikelihood = 1.0 - defaultLikelihood;
    falseEdge->setLikelihood(switchLikelihood);
    originalSwitchBB->SetCond(trueEdge, falseEdge);
    afterDefaultCondBlock->inheritWeight(originalSwitchBB);
    afterDefaultCondBlock->scaleBBWeight(switchLikelihood);

    bool useJumpSequence = jumpCnt < minSwitchTabJumpCnt;

    if (TargetOS::IsUnix && TargetArchitecture::IsArm32)
    {
        // Force using an inlined jumping instead switch table generation.
        // Switch jump table is generated with incorrect values in NativeAOT case,
        // so any large switch will crash after loading to PC any such value.
        // I think this is due to the fact that we use absolute addressing
        // instead of relative. But in NativeAOT is used as a rule relative
        // addressing when we generate an executable.
        // See also https://github.com/dotnet/runtime/issues/8683
        // Also https://github.com/dotnet/coreclr/pull/13197
        useJumpSequence = useJumpSequence || comp->IsTargetAbi(CORINFO_NATIVEAOT_ABI);
    }

    // If we originally had 2 unique successors, check to see whether there is a unique
    // non-default case, in which case we can eliminate the switch altogether.
    // Note that the single unique successor case is handled above.
    if ((targetCnt == 2) && (defaultEdge->getDupCount() == 0))
    {
        // The default case was peeled off, and we have only one other unique successor.
        // Jump directly to this remaining successor.
        FlowEdge* const uniqueSucc = (uniqueSuccs[0] == defaultEdge) ? uniqueSuccs[1] : uniqueSuccs[0];
        assert(uniqueSucc != defaultEdge);
        afterDefaultCondBlock->SetKindAndTargetEdge(BBJ_ALWAYS, uniqueSucc);
        const unsigned dupCount = uniqueSucc->getDupCount();
        uniqueSucc->decrementDupCount(dupCount - 1);
        uniqueSucc->getDestinationBlock()->bbRefs -= (dupCount - 1);
    }
    // If the number of possible destinations is small enough, we proceed to expand the switch
    // into a series of conditional branches, otherwise we follow the jump table based switch
    // transformation.
    else if (useJumpSequence || comp->compStressCompile(Compiler::STRESS_SWITCH_CMP_BR_EXPANSION, 50))
    {
        // Lower the switch into a series of compare and branch IR trees.
        //
        // In this case we will morph the node in the following way:
        // 1. Generate a JTRUE statement to evaluate the default case. (This happens above.)
        // 2. Start splitting the switch basic block into subsequent basic blocks, each of which will contain
        //    a statement that is responsible for performing a comparison of the table index and conditional
        //    branch if equal.

        JITDUMP("Lowering switch " FMT_BB ": using compare/branch expansion\n", originalSwitchBB->bbNum);

        // We'll use 'afterDefaultCondBlock' for the first conditional. After that, we'll add new
        // blocks. If we end up not needing it at all (say, if all the non-default cases just fall through),
        // we'll delete it.
        bool        fUsedAfterDefaultCondBlock = false;
        BasicBlock* currentBlock               = afterDefaultCondBlock;
        LIR::Range* currentBBRange             = &LIR::AsRange(currentBlock);

        // Walk to entries 0 to jumpCnt - 1. If a case target follows, ignore it and let it fall through.
        // If no case target follows, the last one doesn't need to be a compare/branch: it can be an
        // unconditional branch.
        bool fAnyTargetFollows = false;

        // We need to track how much of the original switch's likelihood has already been
        // tested for.  We'll use this to adjust the likelihood of the branches we're adding.
        // So far we've tested for the default case, so we'll start with that.
        weight_t totalTestLikelihood = defaultLikelihood;
        for (unsigned i = 0; i < jumpCnt - 1; ++i)
        {
            assert(currentBlock != nullptr);

            // Remove the switch from the predecessor list of this case target's block.
            // We'll add the proper new predecessor edge later.
            FlowEdge* const   oldEdge     = jumpTab[i];
            BasicBlock* const targetBlock = oldEdge->getDestinationBlock();

            // Compute the likelihood that this test is successful.
            // Divide by number of cases still sharing this edge (reduces likelihood)
            // Divide by likelihood of reaching this test (increases likelihood).
            // But if there is little chance of reaching this test, set the likelihood to 0.5
            //
            weight_t const edgeLikelihood          = oldEdge->getLikelihood();
            weight_t const caseLikelihood          = edgeLikelihood / oldEdge->getDupCount();
            bool const     unlikelyToReachThisCase = Compiler::fgProfileWeightsEqual(totalTestLikelihood, 1.0, 0.001);
            weight_t const adjustedCaseLikelihood =
                unlikelyToReachThisCase ? 0.5 : min(1.0, caseLikelihood / (1.0 - totalTestLikelihood));
            comp->fgRemoveRefPred(oldEdge);

            // Decrement the likelihood on the old edge, so if other cases are sharing it,
            // they get the right values later.
            //
            oldEdge->setLikelihood(edgeLikelihood - caseLikelihood);

            if (targetBlock == followingBB)
            {
                // This case label follows the switch; let it fall through.
                fAnyTargetFollows = true;
                continue;
            }

            // We need a block to put in the new compare and/or branch.
            // If we haven't used the afterDefaultCondBlock yet, then use that.
            //
            if (fUsedAfterDefaultCondBlock)
            {
                BasicBlock*     newBlock  = comp->fgNewBBafter(BBJ_ALWAYS, currentBlock, true);
                FlowEdge* const falseEdge = comp->fgAddRefPred(newBlock, currentBlock); // The fall-through predecessor.

                // We set the true edge likelihood earlier, use that to figure out the false edge likelihood
                // and the block weight.
                //
                FlowEdge* const trueEdge        = currentBlock->GetTrueEdge();
                weight_t const  falseLikelihood = 1.0 - trueEdge->getLikelihood();
                falseEdge->setLikelihood(falseLikelihood);
                currentBlock->SetFalseEdge(falseEdge);
                newBlock->inheritWeight(currentBlock);
                newBlock->scaleBBWeight(falseLikelihood);
                currentBlock   = newBlock;
                currentBBRange = &LIR::AsRange(currentBlock);
            }
            else
            {
                assert(currentBlock == afterDefaultCondBlock);

                // If the first switch case we peel off has the same target as
                // other cases (that is, it has nonzero dup count, it's simpler to
                // just make a new here block, so that as we peel off cases,
                // we're not sharing edges with the original switch.
                //
                // That is, the call to fgAddRefPred below always creates a new edge.
                //
                if (oldEdge->getDupCount() > 0)
                {
                    BasicBlock* const newBlock = comp->fgNewBBafter(BBJ_ALWAYS, currentBlock, true);
                    FlowEdge* const   newEdge  = comp->fgAddRefPred(newBlock, currentBlock);
                    newBlock->inheritWeight(currentBlock);
                    currentBlock   = newBlock;
                    currentBBRange = &LIR::AsRange(currentBlock);
                    afterDefaultCondBlock->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);
                }

                fUsedAfterDefaultCondBlock = true;
            }

            // Update the total test case likelihood.
            totalTestLikelihood += caseLikelihood;

            // Wire up the predecessor list for the "branch" case.
            FlowEdge* const newEdge = comp->fgAddRefPred(targetBlock, currentBlock, oldEdge);
            // This should truly be a new edge.
            assert(newEdge->getDupCount() == 1);

            if (!fAnyTargetFollows && (i == jumpCnt - 2))
            {
                // We're processing the last one, and there is no fall through from any case
                // to the following block, so we can use an unconditional branch to the final
                // case: there is no need to compare against the case index, since it's
                // guaranteed to be taken (since the default case was handled first, above).

                currentBlock->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);
            }
            else
            {
                // Otherwise, it's a conditional branch. Set the branch kind, then add the
                // condition statement.
                // We will set the false edge in a later iteration of the loop, or after.
                currentBlock->SetCond(newEdge);
                newEdge->setLikelihood(adjustedCaseLikelihood);

                // Now, build the conditional statement for the current case that is
                // being evaluated:
                // GT_JTRUE
                //   |__ GT_COND
                //          |____GT_EQ
                //                 |____ (switchIndex) (The temp variable)
                //                 |____ (ICon)        (The actual case constant)
                GenTree* gtCaseCond = comp->gtNewOperNode(GT_EQ, TYP_INT, comp->gtNewLclvNode(tempLclNum, tempLclType),
                                                          comp->gtNewIconNode(i, genActualType(tempLclType)));
                GenTree* gtCaseBranch = comp->gtNewOperNode(GT_JTRUE, TYP_VOID, gtCaseCond);
                LIR::Range caseRange  = LIR::SeqTree(comp, gtCaseBranch);
                currentBBRange->InsertAtEnd(std::move(caseRange));
            }
        }

        if (fAnyTargetFollows)
        {
            // There is a fall-through to the following block. In the loop
            // above, we deleted all the predecessor edges from the switch.
            // In this case, we need to add one back.
            FlowEdge* const falseEdge = comp->fgAddRefPred(currentBlock->Next(), currentBlock);
            currentBlock->SetFalseEdge(falseEdge);
            FlowEdge* const trueEdge        = currentBlock->GetTrueEdge();
            weight_t const  falseLikelihood = 1.0 - trueEdge->getLikelihood();
            falseEdge->setLikelihood(falseLikelihood);

            // The following block weight should remain unchanged. All we've done
            // is alter the various paths that can reach it.
        }

        if (!fUsedAfterDefaultCondBlock)
        {
            // All the cases were fall-through! We don't need this block.
            // Convert it from BBJ_SWITCH to BBJ_ALWAYS and unset the BBF_DONT_REMOVE flag
            // so fgRemoveBlock() doesn't complain.
            JITDUMP("Lowering switch " FMT_BB ": all switch cases were fall-through\n", originalSwitchBB->bbNum);
            assert(currentBlock == afterDefaultCondBlock);
            assert(currentBlock->KindIs(BBJ_SWITCH));
            FlowEdge* const newEdge = comp->fgAddRefPred(currentBlock->Next(), currentBlock);
            currentBlock->SetKindAndTargetEdge(BBJ_ALWAYS, newEdge);
            currentBlock->RemoveFlags(BBF_DONT_REMOVE);
            comp->fgRemoveBlock(currentBlock, /* unreachable */ false); // It's an empty block.
        }

        // Update flow into switch targets
        if (afterDefaultCondBlock->hasProfileWeight())
        {
            bool profileInconsistent = false;
            for (unsigned i = 0; i < targetCnt; i++)
            {
                BasicBlock* const targetBlock = uniqueSuccs[i]->getDestinationBlock();
                targetBlock->setBBProfileWeight(targetBlock->computeIncomingWeight());
                profileInconsistent |= (targetBlock->NumSucc() > 0);
            }

            if (profileInconsistent)
            {
                JITDUMP("Switch lowering: Flow out of " FMT_BB " needs to be propagated. Data %s inconsistent.\n",
                        afterDefaultCondBlock->bbNum, comp->fgPgoConsistent ? "is now" : "was already");
                comp->fgPgoConsistent = false;
            }
        }
    }
    else
    {
        // At this point the default case has already been handled and we need to generate a jump
        // table based switch or a bit test based switch at the end of afterDefaultCondBlock. Both
        // switch variants need the switch value so create the necessary LclVar node here.
        GenTree*    switchValue      = comp->gtNewLclvNode(tempLclNum, tempLclType);
        LIR::Range& switchBlockRange = LIR::AsRange(afterDefaultCondBlock);
        switchBlockRange.InsertAtEnd(switchValue);

        // Try generating a bit test based switch first,
        // if that's not possible a jump table based switch will be generated.
        if (!TryLowerSwitchToBitTest(jumpTab, jumpCnt, targetCnt, afterDefaultCondBlock, switchValue,
                                     defaultLikelihood))
        {
            JITDUMP("Lowering switch " FMT_BB ": using jump table expansion\n", originalSwitchBB->bbNum);

#ifdef TARGET_64BIT
            if (tempLclType != TYP_I_IMPL)
            {
                // SWITCH_TABLE expects the switch value (the index into the jump table) to be TYP_I_IMPL.
                // Note that the switch value is unsigned so the cast should be unsigned as well.
                switchValue = comp->gtNewCastNode(TYP_I_IMPL, switchValue, true, TYP_U_IMPL);
                switchBlockRange.InsertAtEnd(switchValue);
            }
#endif

            GenTree* switchTable = comp->gtNewJmpTableNode();
            GenTree* switchJump  = comp->gtNewOperNode(GT_SWITCH_TABLE, TYP_VOID, switchValue, switchTable);
            switchBlockRange.InsertAfter(switchValue, switchTable, switchJump);

            // This block no longer has a default switch case.
            // If no other cases branch to this successor, remove it from the unique successor table.
            if (defaultEdge->getDupCount() == 0)
            {
                for (unsigned i = 0; i < targetCnt; i++)
                {
                    FlowEdge* const edge = uniqueSuccs[i];
                    if (edge == defaultEdge)
                    {
                        afterDefaultCondBlock->GetSwitchTargets()->RemoveSucc(i);
                        break;
                    }
                }

                assert(targetCnt == (afterDefaultCondBlock->GetSwitchTargets()->GetSuccCount() + 1));
                targetCnt--;
            }

            afterDefaultCondBlock->GetSwitchTargets()->RemoveDefaultCase();

            // We need to scale up the likelihood of the remaining switch edges, now that we've peeled off
            // the default case. But if the remaining likelihood is zero (default likelihood was 1.0),
            // we don't know the case likelihoods. Instead, divide likelihood evenly among all cases.
            //
            if (Compiler::fgProfileWeightsEqual(defaultLikelihood, 1.0, 0.001))
            {
                JITDUMP("Zero weight switch block " FMT_BB ", distributing likelihoods equally per case\n",
                        afterDefaultCondBlock->bbNum);
                // jumpCnt-1 here because we peeled the default after copying this value.
                weight_t const newLikelihood       = 1.0 / (jumpCnt - 1);
                bool           profileInconsistent = false;
                for (unsigned i = 0; i < targetCnt; i++)
                {
                    FlowEdge* const edge          = uniqueSuccs[i];
                    weight_t const  oldEdgeWeight = edge->getLikelyWeight();
                    edge->setLikelihood(newLikelihood * edge->getDupCount());
                    weight_t const newEdgeWeight = edge->getLikelyWeight();

                    if (afterDefaultCondBlock->hasProfileWeight())
                    {
                        BasicBlock* const targetBlock = edge->getDestinationBlock();
                        targetBlock->increaseBBProfileWeight(newEdgeWeight - oldEdgeWeight);
                        profileInconsistent |= (targetBlock->NumSucc() > 0);
                    }
                }

                if (profileInconsistent)
                {
                    JITDUMP("Switch lowering: Flow out of " FMT_BB " needs to be propagated. Data %s inconsistent.\n",
                            afterDefaultCondBlock->bbNum, comp->fgPgoConsistent ? "is now" : "was already");
                    comp->fgPgoConsistent = false;
                }
            }
            else
            {
                weight_t const scaleFactor = 1.0 / (1.0 - defaultLikelihood);
                JITDUMP("Scaling switch block " FMT_BB " likelihoods by " FMT_WT "\n", afterDefaultCondBlock->bbNum,
                        scaleFactor);
                for (unsigned i = 0; i < targetCnt; i++)
                {
                    FlowEdge* const edge          = uniqueSuccs[i];
                    weight_t        newLikelihood = scaleFactor * edge->getLikelihood();

                    if (newLikelihood > 1.0)
                    {
                        // tolerate small overflows
                        assert(Compiler::fgProfileWeightsEqual(newLikelihood, 1.0, 0.001));
                        newLikelihood = 1.0;
                    }
                    edge->setLikelihood(newLikelihood);
                }
            }
        }
    }

    GenTree* next = node->gtNext;

    // Get rid of the GT_SWITCH(temp).
    switchBBRange.Remove(node->AsOp()->gtOp1);
    switchBBRange.Remove(node);

    comp->fgInvalidateDfsTree();

    return next;
}

//------------------------------------------------------------------------
// TryLowerSwitchToBitTest: Attempts to transform a jump table switch into a bit test.
//
// Arguments:
//    jumpTable - The jump table
//    jumpCount - The number of blocks in the jump table
//    targetCount - The number of distinct blocks in the jump table
//    bbSwitch - The switch block
//    switchValue - A LclVar node that provides the switch value
//    defaultLikelihood - likelihood control flow took the default case (already checked)
//
// Return value:
//    true if the switch has been lowered to a bit test
//
// Notes:
//    If the jump table contains less than 32 (64 on 64-bit targets) entries and there
//    are at most 2 distinct jump targets then the jump table can be converted to a word
//    of bits where a 0 bit corresponds to one jump target and a 1 bit corresponds to the
//    other jump target. Instead of the indirect jump a BT-JCC sequence is used to jump
//    to the appropriate target:
//        mov eax, 245 ; jump table converted to a "bit table"
//        bt  eax, ebx ; ebx is supposed to contain the switch value
//        jc target1
//      target0:
//        ...
//      target1:
//    Such code is both shorter and faster (in part due to the removal of a memory load)
//    than the traditional jump table base code. And of course, it also avoids the need
//    to emit the jump table itself that can reach up to 256 bytes (for 64 entries).
//
bool Lowering::TryLowerSwitchToBitTest(FlowEdge*   jumpTable[],
                                       unsigned    jumpCount,
                                       unsigned    targetCount,
                                       BasicBlock* bbSwitch,
                                       GenTree*    switchValue,
                                       weight_t    defaultLikelihood)
{
    assert(jumpCount >= 2);
    assert(targetCount >= 2);
    assert(bbSwitch->KindIs(BBJ_SWITCH));
    assert(switchValue->OperIs(GT_LCL_VAR));

    //
    // Quick check to see if it's worth going through the jump table. The bit test switch supports
    // up to 2 targets but targetCount also includes the default block so we need to allow 3 targets.
    // We'll ensure that there are only 2 targets when building the bit table.
    //

    if (targetCount > 3)
    {
        return false;
    }

    //
    // The number of bits in the bit table is the same as the number of jump table entries. But the
    // jump table also includes the default target (at the end) so we need to ignore it. The default
    // has already been handled by a JTRUE(GT(switchValue, jumpCount - 2)) that LowerSwitch generates.
    //

    const unsigned bitCount = jumpCount - 1;

    if (bitCount > (genTypeSize(TYP_I_IMPL) * 8))
    {
        return false;
    }

    //
    // Build a bit table where a bit set to 0 corresponds to bbCase0 and a bit set to 1 corresponds to
    // bbCase1. Simply use the first block in the jump table as bbCase1, later we can invert the bit
    // table and/or swap the blocks if it's beneficial.
    //

    FlowEdge* case0Edge = nullptr;
    FlowEdge* case1Edge = jumpTable[0];
    size_t    bitTable  = 1;

    for (unsigned bitIndex = 1; bitIndex < bitCount; bitIndex++)
    {
        if (jumpTable[bitIndex] == case1Edge)
        {
            bitTable |= (size_t(1) << bitIndex);
        }
        else if (case0Edge == nullptr)
        {
            case0Edge = jumpTable[bitIndex];
        }
        else if (jumpTable[bitIndex] != case0Edge)
        {
            // If it's neither case0Edge nor case`Edge then it means we have 3 targets. There can't be more
            // than 3 because of the check at the start of the function.
            assert(targetCount == 3);
            return false;
        }
    }

    BasicBlock* bbCase0 = case0Edge->getDestinationBlock();
    BasicBlock* bbCase1 = case1Edge->getDestinationBlock();

    JITDUMP("Lowering switch " FMT_BB " to bit test\n", bbSwitch->bbNum);

#if defined(TARGET_64BIT) && defined(TARGET_XARCH)
    //
    // See if we can avoid a 8 byte immediate on 64 bit targets. If all upper 32 bits are 1
    // then inverting the bit table will make them 0 so that the table now fits in 32 bits.
    // Note that this does not change the number of bits in the bit table, it just takes
    // advantage of the fact that loading a 32 bit immediate into a 64 bit register zero
    // extends the immediate value to 64 bit.
    //

    if (~bitTable <= UINT32_MAX)
    {
        bitTable = ~bitTable;
        std::swap(bbCase0, bbCase1);
    }
#endif

    //
    // Set successor edge dup counts to 1 each
    //

    bbCase0->bbRefs -= (case0Edge->getDupCount() - 1);
    bbCase1->bbRefs -= (case1Edge->getDupCount() - 1);
    case0Edge->decrementDupCount(case0Edge->getDupCount() - 1);
    case1Edge->decrementDupCount(case1Edge->getDupCount() - 1);

    // If defaultLikelihood is not ~ 1.0
    //   up-scale case likelihoods by 1.0 / (1.0 - defaultLikelihood)
    // else switch block weight should be zero
    //   edge likelihoods are unknown, use 0.5
    //
    bool const likelyToReachSwitch = !Compiler::fgProfileWeightsEqual(defaultLikelihood, 1.0, 0.001);

    if (likelyToReachSwitch)
    {
        weight_t const scaleFactor = 1.0 / (1.0 - defaultLikelihood);
        case0Edge->setLikelihood(min(1.0, scaleFactor * case0Edge->getLikelihood()));
        case1Edge->setLikelihood(min(1.0, scaleFactor * case1Edge->getLikelihood()));
    }
    else
    {
        case0Edge->setLikelihood(0.5);
        case1Edge->setLikelihood(0.5);
    }

    bbSwitch->SetCond(case1Edge, case0Edge);

    //
    // Update profile
    //
    if (bbSwitch->hasProfileWeight())
    {
        bbCase0->setBBProfileWeight(bbCase0->computeIncomingWeight());
        bbCase1->setBBProfileWeight(bbCase1->computeIncomingWeight());

        if ((bbCase0->NumSucc() > 0) || (bbCase1->NumSucc() > 0))
        {
            JITDUMP("TryLowerSwitchToBitTest: Flow out of " FMT_BB " needs to be propagated. Data %s inconsistent.\n",
                    bbSwitch->bbNum, comp->fgPgoConsistent ? "is now" : "was already");
            comp->fgPgoConsistent = false;
        }
    }

    var_types bitTableType = (bitCount <= (genTypeSize(TYP_INT) * 8)) ? TYP_INT : TYP_LONG;
    GenTree*  bitTableIcon = comp->gtNewIconNode(bitTable, bitTableType);

#ifdef TARGET_XARCH
    //
    // Append BT(bitTable, switchValue) and JCC(condition) to the switch block.
    //
    GenTree* bitTest = comp->gtNewOperNode(GT_BT, TYP_VOID, bitTableIcon, switchValue);
    bitTest->gtFlags |= GTF_SET_FLAGS;
    GenTreeCC* jcc = comp->gtNewCC(GT_JCC, TYP_VOID, GenCondition::C);
    LIR::AsRange(bbSwitch).InsertAfter(switchValue, bitTableIcon, bitTest, jcc);
#else  // TARGET_XARCH
    //
    // Fallback to AND(RSZ(bitTable, switchValue), 1)
    //
    GenTree* tstCns = comp->gtNewIconNode(1, bitTableType);
    GenTree* shift  = comp->gtNewOperNode(GT_RSZ, bitTableType, bitTableIcon, switchValue);
    GenTree* one    = comp->gtNewIconNode(1, bitTableType);
    GenTree* andOp  = comp->gtNewOperNode(GT_AND, bitTableType, shift, one);
    GenTree* cmp    = comp->gtNewOperNode(GT_EQ, TYP_INT, andOp, tstCns);
    GenTree* jcc    = comp->gtNewOperNode(GT_JTRUE, TYP_VOID, cmp);
    LIR::AsRange(bbSwitch).InsertAfter(switchValue, bitTableIcon, shift, tstCns, one);
    LIR::AsRange(bbSwitch).InsertAfter(one, andOp, cmp, jcc);
#endif // !TARGET_XARCH
    return true;
}

//------------------------------------------------------------------------
// LowerArg:
//   Lower one argument of a call. This entails inserting putarg nodes between
//   the call and the argument. This is the point at which the source is
//   consumed and the value transitions from control of the register allocator
//   to the calling convention.
//
// Arguments:
//    call    - The call node
//    callArg - Call argument
//
void Lowering::LowerArg(GenTreeCall* call, CallArg* callArg)
{
    GenTree** ppArg = &callArg->NodeRef();
    GenTree*  arg   = *ppArg;
    assert(arg != nullptr);

    JITDUMP("Lowering arg: \n");
    DISPTREERANGE(BlockRange(), arg);
    assert(arg->IsValue());

    // If we hit this we are probably double-lowering.
    assert(!arg->OperIsPutArg());

    const ABIPassingInformation& abiInfo = callArg->AbiInfo;
    JITDUMP("Passed in ");
    DBEXEC(comp->verbose, abiInfo.Dump());

#if !defined(TARGET_64BIT)
    if (comp->opts.compUseSoftFP && arg->TypeIs(TYP_DOUBLE))
    {
        // Unlike TYP_LONG we do no decomposition for doubles, yet we maintain
        // it as a primitive type until lowering. So we need to get it into the
        // right form here.

        unsigned argLclNum = comp->lvaGrabTemp(false DEBUGARG("double arg on softFP"));
        GenTree* store     = comp->gtNewTempStore(argLclNum, arg);
        GenTree* low       = comp->gtNewLclFldNode(argLclNum, TYP_INT, 0);
        GenTree* high      = comp->gtNewLclFldNode(argLclNum, TYP_INT, 4);
        GenTree* longNode  = new (comp, GT_LONG) GenTreeOp(GT_LONG, TYP_LONG, low, high);
        BlockRange().InsertAfter(arg, store, low, high, longNode);

        *ppArg = arg = longNode;

        comp->lvaSetVarDoNotEnregister(argLclNum DEBUGARG(DoNotEnregisterReason::LocalField));

        JITDUMP("Transformed double-typed arg on softFP to LONG node\n");
    }

    if (varTypeIsLong(arg))
    {
        assert(callArg->AbiInfo.CountRegsAndStackSlots() == 2);

        noway_assert(arg->OperIs(GT_LONG));
        GenTreeFieldList* fieldList = new (comp, GT_FIELD_LIST) GenTreeFieldList();
        fieldList->AddFieldLIR(comp, arg->gtGetOp1(), 0, TYP_INT);
        fieldList->AddFieldLIR(comp, arg->gtGetOp2(), 4, TYP_INT);
        BlockRange().InsertBefore(arg, fieldList);

        BlockRange().Remove(arg);
        *ppArg = arg = fieldList;

        JITDUMP("Transformed long arg on 32-bit to FIELD_LIST node\n");
    }
#endif

#if FEATURE_ARG_SPLIT
    // Structs can be split into register(s) and stack on some targets
    if (compFeatureArgSplit() && abiInfo.IsSplitAcrossRegistersAndStack())
    {
        SplitArgumentBetweenRegistersAndStack(call, callArg);
        LowerArg(call, callArg);
        return;
    }
    else
#endif // FEATURE_ARG_SPLIT
    {
        if (abiInfo.HasAnyRegisterSegment())
        {
            if (arg->OperIs(GT_FIELD_LIST) || (abiInfo.NumSegments > 1))
            {
                if (!arg->OperIs(GT_FIELD_LIST))
                {
                    // Primitive arg, but the ABI requires it to be split into
                    // registers. Insert the field list here.
                    GenTreeFieldList* fieldList = comp->gtNewFieldList();
                    fieldList->AddFieldLIR(comp, arg, 0, genActualType(arg->TypeGet()));
                    BlockRange().InsertAfter(arg, fieldList);
                    arg = *ppArg = fieldList;
                }

                LowerArgFieldList(callArg, arg->AsFieldList());
                arg = *ppArg;
            }
            else
            {
                assert(abiInfo.HasExactlyOneRegisterSegment());
                InsertPutArgReg(ppArg, abiInfo.Segment(0));
                arg = *ppArg;
            }
        }
        else
        {
            assert(abiInfo.NumSegments == 1);
            const ABIPassingSegment& stackSeg             = abiInfo.Segment(0);
            const bool               putInIncomingArgArea = call->IsFastTailCall();

            GenTree* putArg =
                new (comp, GT_PUTARG_STK) GenTreePutArgStk(GT_PUTARG_STK, TYP_VOID, arg, stackSeg.GetStackOffset(),
                                                           stackSeg.GetStackSize(), call, putInIncomingArgArea);

            BlockRange().InsertAfter(arg, putArg);
            *ppArg = arg = putArg;
        }
    }

    if (arg->OperIsPutArgStk())
    {
        LowerPutArgStk(arg->AsPutArgStk());
    }

    DISPTREERANGE(BlockRange(), arg);
}

//------------------------------------------------------------------------
// SplitArgumentBetweenRegistersAndStack:
//   Split an argument that is passed in both registers and stack into two
//   separate arguments, one for registers and one for stack.
//
// Parameters:
//   call    - The call node
//   callArg - Call argument
//
// Remarks:
//   The argument is changed to be its stack part, and a new argument is
//   inserted after it representing its registers.
//
void Lowering::SplitArgumentBetweenRegistersAndStack(GenTreeCall* call, CallArg* callArg)
{
    GenTree** ppArg = &callArg->NodeRef();
    GenTree*  arg   = *ppArg;

    assert(arg->OperIs(GT_BLK, GT_FIELD_LIST) || arg->OperIsLocalRead());
    assert(!call->IsFastTailCall());

    const ABIPassingInformation& abiInfo = callArg->AbiInfo;
    assert(abiInfo.IsSplitAcrossRegistersAndStack());

#ifdef DEBUG
    for (unsigned i = 0; i < abiInfo.NumSegments; i++)
    {
        assert((i < abiInfo.NumSegments - 1) == abiInfo.Segment(i).IsPassedInRegister());
    }
#endif

    unsigned                 numRegs  = abiInfo.NumSegments - 1;
    const ABIPassingSegment& stackSeg = abiInfo.Segment(abiInfo.NumSegments - 1);

    JITDUMP("Dividing split arg [%06u] with %u registers, %u stack space into two arguments\n",
            Compiler::dspTreeID(arg), numRegs, stackSeg.Size);

    ClassLayout* registersLayout = SliceLayout(callArg->GetSignatureLayout(), 0, stackSeg.Offset);
    ClassLayout* stackLayout     = SliceLayout(callArg->GetSignatureLayout(), stackSeg.Offset,
                                               callArg->GetSignatureLayout()->GetSize() - stackSeg.Offset);

    GenTree* stackNode     = nullptr;
    GenTree* registersNode = nullptr;

    if (arg->OperIsFieldList())
    {
        JITDUMP("Argument is a FIELD_LIST\n", numRegs, stackSeg.Size);

        GenTreeFieldList::Use* splitPoint = nullptr;
        // Split the field list into its register and stack parts.
        for (GenTreeFieldList::Use& use : arg->AsFieldList()->Uses())
        {
            if (use.GetOffset() >= stackSeg.Offset)
            {
                splitPoint = &use;
                JITDUMP("Found split point at offset %u\n", splitPoint->GetOffset());
                break;
            }

            if (use.GetOffset() + genTypeSize(use.GetType()) > stackSeg.Offset)
            {
                // Field overlaps partially into the stack segment, cannot
                // handle this without spilling.
                break;
            }
        }

        if (splitPoint == nullptr)
        {
            JITDUMP("No clean split point found, spilling FIELD_LIST\n", splitPoint->GetOffset());

            unsigned int newLcl =
                StoreFieldListToNewLocal(comp->typGetObjLayout(callArg->GetSignatureClassHandle()), arg->AsFieldList());
            stackNode     = comp->gtNewLclFldNode(newLcl, TYP_STRUCT, stackSeg.Offset, stackLayout);
            registersNode = comp->gtNewLclFldNode(newLcl, TYP_STRUCT, 0, registersLayout);
            BlockRange().InsertBefore(arg, stackNode);
            BlockRange().InsertBefore(arg, registersNode);
        }
        else
        {
            stackNode     = comp->gtNewFieldList();
            registersNode = comp->gtNewFieldList();

            BlockRange().InsertBefore(arg, stackNode);
            BlockRange().InsertBefore(arg, registersNode);

            for (GenTreeFieldList::Use& use : arg->AsFieldList()->Uses())
            {
                if (&use == splitPoint)
                {
                    break;
                }

                registersNode->AsFieldList()->AddFieldLIR(comp, use.GetNode(), use.GetOffset(), use.GetType());
            }

            for (GenTreeFieldList::Use* use = splitPoint; use != nullptr; use = use->GetNext())
            {
                stackNode->AsFieldList()->AddFieldLIR(comp, use->GetNode(), use->GetOffset() - stackSeg.Offset,
                                                      use->GetType());
            }
        }

        BlockRange().Remove(arg);
    }
    else if (arg->OperIs(GT_BLK))
    {
        JITDUMP("Argument is a BLK\n", numRegs, stackSeg.Size);

        GenTree*       blkAddr = arg->AsBlk()->Addr();
        target_ssize_t offset  = 0;
        comp->gtPeelOffsets(&blkAddr, &offset);

        LIR::Use addrUse;
        bool     gotUse = BlockRange().TryGetUse(blkAddr, &addrUse);
        assert(gotUse);

        unsigned addrLcl;
        if (addrUse.Def()->OperIsScalarLocal() &&
            !comp->lvaGetDesc(addrUse.Def()->AsLclVarCommon())->IsAddressExposed() &&
            IsInvariantInRange(addrUse.Def(), arg))
        {
            JITDUMP("Reusing LCL_VAR\n", numRegs, stackSeg.Size);
            addrLcl = addrUse.Def()->AsLclVarCommon()->GetLclNum();
        }
        else
        {
            JITDUMP("Spilling address\n", numRegs, stackSeg.Size);
            addrLcl = addrUse.ReplaceWithLclVar(comp);
        }

        auto createAddr = [=](unsigned offs) {
            GenTree* addr = comp->gtNewLclVarNode(addrLcl);
            offs += (unsigned)offset;
            if (offs != 0)
            {
                GenTree* addrOffs = comp->gtNewIconNode((ssize_t)offs, TYP_I_IMPL);
                addr = comp->gtNewOperNode(GT_ADD, varTypeIsGC(addr) ? TYP_BYREF : TYP_I_IMPL, addr, addrOffs);
            }

            return addr;
        };

        GenTree* addr = createAddr(stackSeg.Offset);
        stackNode     = comp->gtNewBlkIndir(stackLayout, addr, arg->gtFlags & GTF_IND_COPYABLE_FLAGS);
        BlockRange().InsertBefore(arg, LIR::SeqTree(comp, stackNode));
        LowerRange(addr, stackNode);

        registersNode = comp->gtNewFieldList();
        BlockRange().InsertBefore(arg, registersNode);

        for (unsigned i = 0; i < numRegs; i++)
        {
            const ABIPassingSegment& seg = abiInfo.Segment(i);

            GenTree* addr  = createAddr(seg.Offset);
            GenTree* indir = comp->gtNewIndir(seg.GetRegisterType(callArg->GetSignatureLayout()), addr,
                                              arg->gtFlags & GTF_IND_COPYABLE_FLAGS);
            registersNode->AsFieldList()->AddFieldLIR(comp, indir, seg.Offset, indir->TypeGet());
            BlockRange().InsertBefore(registersNode, LIR::SeqTree(comp, indir));
            LowerRange(addr, indir);
        }

        BlockRange().Remove(arg, /* markOperandsUnused */ true);
    }
    else
    {
        assert(arg->OperIsLocalRead());

        JITDUMP("Argument is a local\n", numRegs, stackSeg.Size);

        GenTreeLclVarCommon* lcl = arg->AsLclVarCommon();

        stackNode =
            comp->gtNewLclFldNode(lcl->GetLclNum(), TYP_STRUCT, lcl->GetLclOffs() + stackSeg.Offset, stackLayout);
        BlockRange().InsertBefore(arg, stackNode);

        registersNode = comp->gtNewFieldList();
        BlockRange().InsertBefore(arg, registersNode);

        for (unsigned i = 0; i < numRegs; i++)
        {
            const ABIPassingSegment& seg = abiInfo.Segment(i);
            GenTree*                 fldNode =
                comp->gtNewLclFldNode(lcl->GetLclNum(), seg.GetRegisterType(callArg->GetSignatureLayout()),
                                      lcl->GetLclOffs() + seg.Offset);
            registersNode->AsFieldList()->AddFieldLIR(comp, fldNode, seg.Offset, fldNode->TypeGet());
            BlockRange().InsertBefore(registersNode, fldNode);
        }

        BlockRange().Remove(arg);
    }

    JITDUMP("New stack node is:\n");
    DISPTREERANGE(BlockRange(), stackNode);

    JITDUMP("New registers node is:\n");
    DISPTREERANGE(BlockRange(), registersNode);

    ABIPassingSegment     newStackSeg = ABIPassingSegment::OnStack(stackSeg.GetStackOffset(), 0, stackSeg.Size);
    ABIPassingInformation newStackAbi = ABIPassingInformation::FromSegment(comp, false, newStackSeg);

    ABIPassingInformation newRegistersAbi(comp, numRegs);
    for (unsigned i = 0; i < numRegs; i++)
    {
        newRegistersAbi.Segment(i) = abiInfo.Segment(i);
    }

    callArg->AbiInfo = newStackAbi;
    *ppArg = arg = stackNode;

    NewCallArg newRegisterArgAdd = NewCallArg::Struct(registersNode, TYP_STRUCT, registersLayout);
    CallArg*   newRegisterArg    = call->gtArgs.InsertAfter(comp, callArg, newRegisterArgAdd);

    newRegisterArg->AbiInfo = newRegistersAbi;

    if (callArg->GetLateNode() != nullptr)
    {
        newRegisterArg->SetLateNext(callArg->GetLateNext());
        callArg->SetLateNext(newRegisterArg);

        newRegisterArg->SetLateNode(registersNode);
        newRegisterArg->SetEarlyNode(nullptr);
    }

    JITDUMP("Added a new call arg. New call is:\n");
    DISPTREERANGE(BlockRange(), call);
}

//------------------------------------------------------------------------
// SliceLayout:
//   Slice a class layout into the specified range.
//
// Parameters:
//   layout - The layout
//   offset - Start offset of the slice
//   size   - Size of the slice
//
// Returns:
//   New layout of size 'size'
//
ClassLayout* Lowering::SliceLayout(ClassLayout* layout, unsigned offset, unsigned size)
{
    ClassLayoutBuilder builder(comp, size);
    INDEBUG(builder.SetName(comp->printfAlloc("%s[%03u..%03u)", layout->GetClassName(), offset, offset + size),
                            comp->printfAlloc("%s[%03u..%03u)", layout->GetShortClassName(), offset, offset + size)));

    if (((offset % TARGET_POINTER_SIZE) == 0) && ((size % TARGET_POINTER_SIZE) == 0) && layout->HasGCPtr())
    {
        for (unsigned i = 0; i < size; i += TARGET_POINTER_SIZE)
        {
            builder.SetGCPtrType(i / TARGET_POINTER_SIZE, layout->GetGCPtrType((offset + i) / TARGET_POINTER_SIZE));
        }
    }
    else
    {
        assert(!layout->HasGCPtr());
    }

    builder.AddPadding(SegmentList::Segment(0, size));

    for (const SegmentList::Segment& nonPadding : layout->GetNonPadding(comp))
    {
        if ((nonPadding.End <= offset) || (nonPadding.Start >= offset + size))
        {
            continue;
        }

        unsigned start = nonPadding.Start <= offset ? 0 : (nonPadding.Start - offset);
        unsigned end   = nonPadding.End >= (offset + size) ? size : (nonPadding.End - offset);

        builder.RemovePadding(SegmentList::Segment(start, end));
    }
    return comp->typGetCustomLayout(builder);
}

//------------------------------------------------------------------------
// InsertBitCastIfNecessary:
//   Insert a bitcast if a primitive argument being passed in a register is not
//   evaluated in the right type of register.
//
// Arguments:
//    argNode         - Edge for the argument
//    registerSegment - Register that the argument is going into
//
void Lowering::InsertBitCastIfNecessary(GenTree** argNode, const ABIPassingSegment& registerSegment)
{
    if (varTypeUsesIntReg(*argNode) == genIsValidIntReg(registerSegment.GetRegister()))
    {
        return;
    }

    JITDUMP("Argument node [%06u] needs to be passed in %s; inserting bitcast\n", Compiler::dspTreeID(*argNode),
            getRegName(registerSegment.GetRegister()));

    // Due to padding the node may be smaller than the register segment. In
    // such cases we cut off the end of the segment to get an appropriate
    // register type for the bitcast.
    ABIPassingSegment cutRegisterSegment = registerSegment;
    unsigned          argNodeSize        = genTypeSize(genActualType(*argNode));
    if (registerSegment.Size > argNodeSize)
    {
        cutRegisterSegment =
            ABIPassingSegment::InRegister(registerSegment.GetRegister(), registerSegment.Offset, argNodeSize);
    }

    var_types bitCastType = cutRegisterSegment.GetRegisterType();

    GenTreeUnOp* bitCast = comp->gtNewBitCastNode(bitCastType, *argNode);
    BlockRange().InsertAfter(*argNode, bitCast);

    *argNode = bitCast;
    if (!TryRemoveBitCast(bitCast))
    {
        ContainCheckBitCast(bitCast);
    }
}

//------------------------------------------------------------------------
// InsertPutArgReg:
//   Insert a PUTARG_REG node for the specified edge. If the argument node does
//   not fit the register type, then also insert a bitcast.
//
// Arguments:
//    argNode         - Edge for the argument
//    registerSegment - Register that the argument is going into
//
void Lowering::InsertPutArgReg(GenTree** argNode, const ABIPassingSegment& registerSegment)
{
    assert(registerSegment.IsPassedInRegister());

    InsertBitCastIfNecessary(argNode, registerSegment);
    GenTree* putArg = comp->gtNewPutArgReg(genActualType(*argNode), *argNode, registerSegment.GetRegister());
    BlockRange().InsertAfter(*argNode, putArg);
    *argNode = putArg;
}

//------------------------------------------------------------------------
// LowerArgsForCall:
//   Lower the arguments of a call node.
//
// Arguments:
//    call - Call node
//
void Lowering::LowerArgsForCall(GenTreeCall* call)
{
    JITDUMP("Args:\n======\n");
    for (CallArg& arg : call->gtArgs.EarlyArgs())
    {
        LowerArg(call, &arg);
    }

    JITDUMP("\nLate args:\n======\n");
    for (CallArg& arg : call->gtArgs.LateArgs())
    {
        LowerArg(call, &arg);
    }

#if defined(TARGET_X86) && defined(FEATURE_IJW)
    LowerSpecialCopyArgs(call);
#endif // defined(TARGET_X86) && defined(FEATURE_IJW)

    LegalizeArgPlacement(call);
}

#if defined(TARGET_X86) && defined(FEATURE_IJW)
//------------------------------------------------------------------------
// LowerSpecialCopyArgs: Lower special copy arguments for P/Invoke IL stubs
//
// Arguments:
//    call - the call node
//
// Notes:
//    This method is used for P/Invoke IL stubs on x86 to handle arguments with special copy semantics.
//    In particular, this method implements copy-constructor semantics for managed-to-unmanaged IL stubs
//    for C++/CLI. In this case, the managed argument is passed by (managed or unmanaged) pointer in the
//    P/Invoke signature with a speial modreq, but is passed to the unmanaged function by value.
//    The value passed to the unmanaged function must be created through a copy-constructor call copying from
//    the original source argument.
//    We assume that the IL stub will be generated such that the following holds true:
//       - If an argument to the IL stub has the special modreq, then its corresponding argument to the
//         unmanaged function will be passed as the same argument index. Therefore, we can introduce the copy call
//         from the original source argument to the argument slot in the unmanaged call.
void Lowering::LowerSpecialCopyArgs(GenTreeCall* call)
{
    // We only need to use the special copy helper on P/Invoke IL stubs
    // for the unmanaged call.
    if (comp->opts.jitFlags->IsSet(JitFlags::JIT_FLAG_IL_STUB) && comp->compMethodRequiresPInvokeFrame() &&
        call->IsUnmanaged() && comp->compHasSpecialCopyArgs())
    {
        // Unmanaged calling conventions on Windows x86 are passed in reverse order
        // of managed args, so we need to count down the number of args.
        // If the call is thiscall, we need to account for the this parameter,
        // which will be first in the list.
        // The this parameter is always passed in registers, so we can ignore it.
        unsigned argIndex = call->gtArgs.CountUserArgs() - 1;
        assert(call->gtArgs.CountUserArgs() == comp->info.compILargsCount);
        for (CallArg& arg : call->gtArgs.Args())
        {
            if (!arg.IsUserArg())
            {
                continue;
            }

            if (call->GetUnmanagedCallConv() == CorInfoCallConvExtension::Thiscall &&
                argIndex == call->gtArgs.CountUserArgs() - 1)
            {
                assert(arg.GetNode()->OperIs(GT_PUTARG_REG));
                continue;
            }

            unsigned paramLclNum = comp->compMapILargNum(argIndex);
            assert(paramLclNum < comp->info.compArgsCount);

            // check if parameter at the same index as the IL argument is marked as requiring special copy, assuming
            // that it is being passed 1:1 to the pinvoke
            if (comp->argRequiresSpecialCopy(paramLclNum) && (arg.GetSignatureType() == TYP_STRUCT))
            {
                assert(arg.GetNode()->OperIs(GT_PUTARG_STK));
                InsertSpecialCopyArg(arg.GetNode()->AsPutArgStk(), arg.GetSignatureClassHandle(), paramLclNum);
            }

            argIndex--;
        }
    }
}

//------------------------------------------------------------------------
// InsertSpecialCopyArg: Insert a call to the special copy helper to copy from the (possibly value pointed-to by) local
// lclnum to the argument slot represented by putArgStk
//
// Arguments:
//    putArgStk - the PutArgStk node representing the stack slot of the argument
//    argType - the struct type of the argument
//    lclNum - the local to use as the source for the special copy helper
//
// Notes:
//   This method assumes that lclNum is either a by-ref to a struct of type argType
//   or a struct of type argType.
//   We use this to preserve special copy semantics for interop calls where we pass in a byref to a struct into a
//   P/Invoke with a special modreq and the native function expects to recieve the struct by value with the argument
//   being passed in having been created by the special copy helper.
//
void Lowering::InsertSpecialCopyArg(GenTreePutArgStk* putArgStk, CORINFO_CLASS_HANDLE argType, unsigned lclNum)
{
    assert(putArgStk != nullptr);
    GenTree* dest = comp->gtNewPhysRegNode(REG_SPBASE, TYP_I_IMPL);

    GenTree*  src;
    var_types lclType = comp->lvaGetRealType(lclNum);

    if (lclType == TYP_BYREF || lclType == TYP_I_IMPL)
    {
        src = comp->gtNewLclVarNode(lclNum, lclType);
    }
    else
    {
        assert(lclType == TYP_STRUCT);
        src = comp->gtNewLclAddrNode(lclNum, 0, TYP_I_IMPL);
    }

    GenTree* destPlaceholder = comp->gtNewZeroConNode(dest->TypeGet());
    GenTree* srcPlaceholder  = comp->gtNewZeroConNode(src->TypeGet());

    GenTreeCall* call =
        comp->gtNewCallNode(CT_USER_FUNC, comp->info.compCompHnd->getSpecialCopyHelper(argType), TYP_VOID);

    call->gtArgs.PushBack(comp, NewCallArg::Primitive(destPlaceholder));
    call->gtArgs.PushBack(comp, NewCallArg::Primitive(srcPlaceholder));

    comp->fgMorphArgs(call);

    LIR::Range callRange      = LIR::SeqTree(comp, call);
    GenTree*   callRangeStart = callRange.FirstNode();
    GenTree*   callRangeEnd   = callRange.LastNode();

    BlockRange().InsertAfter(putArgStk, std::move(callRange));
    BlockRange().InsertAfter(putArgStk, dest);
    BlockRange().InsertAfter(putArgStk, src);

    LIR::Use destUse;
    LIR::Use srcUse;
    BlockRange().TryGetUse(destPlaceholder, &destUse);
    BlockRange().TryGetUse(srcPlaceholder, &srcUse);
    destUse.ReplaceWith(dest);
    srcUse.ReplaceWith(src);
    destPlaceholder->SetUnusedValue();
    srcPlaceholder->SetUnusedValue();

    LowerRange(callRangeStart, callRangeEnd);

    // Finally move all GT_PUTARG_* nodes
    // Re-use the existing logic for CFG call args here
    MoveCFGCallArgs(call);

    BlockRange().Remove(destPlaceholder);
    BlockRange().Remove(srcPlaceholder);
}
#endif // defined(TARGET_X86) && defined(FEATURE_IJW)

//------------------------------------------------------------------------
// LegalizeArgPlacement: Move arg placement nodes (PUTARG_*) into a legal
// ordering after they have been created.
//
// Arguments:
//   call - GenTreeCall node that has had PUTARG_* nodes created for arguments.
//
// Remarks:
//   PUTARG_* nodes are created and inserted right after the definitions of the
//   argument values. However, there are constraints on how the PUTARG nodes
//   can appear:
//
//   - No other GT_CALL nodes are allowed between a PUTARG_REG node and the
//   call. For FEATURE_FIXED_OUT_ARGS this condition is also true for
//   PUTARG_STK.
//   - For !FEATURE_FIXED_OUT_ARGS, the PUTARG_STK nodes must come in push
//   order.
//
//   Morph has mostly already solved this problem, but transformations on LIR
//   can make the ordering we end up with here illegal. This function legalizes
//   the placement while trying to minimize the distance between an argument
//   definition and its corresponding placement node.
//
void Lowering::LegalizeArgPlacement(GenTreeCall* call)
{
    size_t numMarked = MarkCallPutArgAndFieldListNodes(call);

    // We currently do not try to resort the PUTARG_STK nodes, but rather just
    // assert here that they are ordered.
#if defined(DEBUG) && !FEATURE_FIXED_OUT_ARGS
    unsigned nextPushOffset = UINT_MAX;
#endif

    GenTree* cur = call->gtPrev;
    while (numMarked > 0)
    {
        assert(cur != nullptr);

        if ((cur->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            numMarked--;
            cur->gtLIRFlags &= ~LIR::Flags::Mark;

#if defined(DEBUG) && !FEATURE_FIXED_OUT_ARGS
            if (cur->OperIs(GT_PUTARG_STK))
            {
                // For !FEATURE_FIXED_OUT_ARGS (only x86) byte offsets are
                // subtracted from the top of the stack frame; so last pushed
                // arg has highest offset.
                assert(nextPushOffset > cur->AsPutArgStk()->getArgOffset());
                nextPushOffset = cur->AsPutArgStk()->getArgOffset();
            }
#endif
        }

        if (cur->IsCall())
        {
            break;
        }

        cur = cur->gtPrev;
    }

    if (numMarked == 0)
    {
        // Already legal; common case
        return;
    }

    JITDUMP("Call [%06u] has %zu PUTARG nodes that interfere with [%06u]; will move them after it\n",
            Compiler::dspTreeID(call), numMarked, Compiler::dspTreeID(cur));

    // We found interference; remaining PUTARG nodes need to be moved after
    // this point.
    GenTree* insertionPoint = cur;

    while (numMarked > 0)
    {
        assert(cur != nullptr);

        GenTree* prev = cur->gtPrev;
        if ((cur->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            numMarked--;
            cur->gtLIRFlags &= ~LIR::Flags::Mark;

            // For FEATURE_FIXED_OUT_ARGS: all PUTARG nodes must be moved after the interfering call
            // For !FEATURE_FIXED_OUT_ARGS: only PUTARG_REG nodes must be moved after the interfering call
            if (FEATURE_FIXED_OUT_ARGS || cur->OperIs(GT_FIELD_LIST, GT_PUTARG_REG))
            {
                JITDUMP("Relocating [%06u] after [%06u]\n", Compiler::dspTreeID(cur),
                        Compiler::dspTreeID(insertionPoint));

                BlockRange().Remove(cur);
                BlockRange().InsertAfter(insertionPoint, cur);
            }
        }

        cur = prev;
    }

    JITDUMP("Final result after legalization:\n");
    DISPTREERANGE(BlockRange(), call);
}

// helper that create a node representing a relocatable physical address computation
GenTree* Lowering::AddrGen(ssize_t addr)
{
    // this should end up in codegen as : instGen_Set_Reg_To_Imm(EA_HANDLE_CNS_RELOC, reg, addr)
    GenTree* result = comp->gtNewIconHandleNode(addr, GTF_ICON_FTN_ADDR);
    return result;
}

// variant that takes a void*
GenTree* Lowering::AddrGen(void* addr)
{
    return AddrGen((ssize_t)addr);
}

//------------------------------------------------------------------------
// LowerCallMemset: Replaces the following memset-like special intrinsics:
//
//    SpanHelpers.Fill<T>(ref dstRef, CNS_SIZE, CNS_VALUE)
//    CORINFO_HELP_MEMSET(ref dstRef, CNS_VALUE, CNS_SIZE)
//    SpanHelpers.ClearWithoutReferences(ref dstRef, CNS_SIZE)
//
//  with a GT_STORE_BLK node:
//
//    *  STORE_BLK struct<CNS_SIZE> (init) (Unroll)
//    +--*  LCL_VAR   byref  dstRef
//    \--*  CNS_INT   int    0
//
// Arguments:
//    tree - GenTreeCall node to replace with STORE_BLK
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::LowerCallMemset(GenTreeCall* call, GenTree** next)
{
    assert(call->IsSpecialIntrinsic(comp, NI_System_SpanHelpers_Fill) ||
           call->IsSpecialIntrinsic(comp, NI_System_SpanHelpers_ClearWithoutReferences) ||
           call->IsHelperCall(comp, CORINFO_HELP_MEMSET));

    JITDUMP("Considering Memset-like call [%06d] for unrolling.. ", comp->dspTreeID(call))

    if (comp->info.compHasNextCallRetAddr)
    {
        JITDUMP("compHasNextCallRetAddr=true so we won't be able to remove the call - bail out.\n");
        return false;
    }

    GenTree* dstRefArg = call->gtArgs.GetUserArgByIndex(0)->GetNode();
    GenTree* lengthArg;
    GenTree* valueArg;

    // Fill<T>'s length is not in bytes, so we need to scale it depending on the signature
    unsigned lengthScale;

    if (call->IsSpecialIntrinsic(comp, NI_System_SpanHelpers_Fill))
    {
        // void SpanHelpers::Fill<T>(ref T refData, nuint numElements, T value)
        //
        assert(call->gtArgs.CountUserArgs() == 3);
        lengthArg             = call->gtArgs.GetUserArgByIndex(1)->GetNode();
        CallArg* valueCallArg = call->gtArgs.GetUserArgByIndex(2);
        valueArg              = valueCallArg->GetNode();

        // Get that <T> from the signature
        lengthScale = genTypeSize(valueCallArg->GetSignatureType());
        // NOTE: structs and TYP_REF will be ignored by the "Value is not a constant" check
        // Some of those cases can be enabled in future, e.g. s
    }
    else if (call->IsHelperCall(comp, CORINFO_HELP_MEMSET))
    {
        // void CORINFO_HELP_MEMSET(ref T refData, byte value, nuint numElements)
        //
        assert(call->gtArgs.CountUserArgs() == 3);
        lengthArg   = call->gtArgs.GetUserArgByIndex(2)->GetNode();
        valueArg    = call->gtArgs.GetUserArgByIndex(1)->GetNode();
        lengthScale = 1; // it's always in bytes
    }
    else
    {
        // void SpanHelpers::ClearWithoutReferences(ref byte b, nuint byteLength)
        //
        assert(call->IsSpecialIntrinsic(comp, NI_System_SpanHelpers_ClearWithoutReferences));
        assert(call->gtArgs.CountUserArgs() == 2);

        // Simple zeroing
        lengthArg   = call->gtArgs.GetUserArgByIndex(1)->GetNode();
        valueArg    = comp->gtNewZeroConNode(TYP_INT);
        lengthScale = 1; // it's always in bytes
    }

    if (!lengthArg->IsIntegralConst())
    {
        JITDUMP("Length is not a constant - bail out.\n");
        return false;
    }

    if (!valueArg->IsCnsIntOrI() || !valueArg->TypeIs(TYP_INT))
    {
        JITDUMP("Value is not a constant - bail out.\n");
        return false;
    }

    // If value is not zero, we can only unroll for single-byte values
    if (!valueArg->IsIntegralConst(0) && (lengthScale != 1))
    {
        JITDUMP("Value is not unroll-friendly - bail out.\n");
        return false;
    }

    // Convert lenCns to bytes
    ssize_t lenCns = lengthArg->AsIntCon()->IconValue();
    if (CheckedOps::MulOverflows((target_ssize_t)lenCns, (target_ssize_t)lengthScale, CheckedOps::Signed))
    {
        // lenCns overflows
        JITDUMP("lenCns * lengthScale overflows - bail out.\n")
        return false;
    }
    lenCns *= (ssize_t)lengthScale;

    // TODO-CQ: drop the whole thing in case of lenCns = 0
    if ((lenCns <= 0) || (lenCns > (ssize_t)comp->getUnrollThreshold(Compiler::UnrollKind::Memset)))
    {
        JITDUMP("Size is either 0 or too big to unroll - bail out.\n")
        return false;
    }

    JITDUMP("Accepted for unrolling!\nOld tree:\n");
    DISPTREERANGE(BlockRange(), call);

    if (!valueArg->IsIntegralConst(0))
    {
        // Non-zero (byte) value, wrap value with GT_INIT_VAL
        GenTree* initVal = valueArg;
        valueArg         = comp->gtNewOperNode(GT_INIT_VAL, TYP_INT, initVal);
        BlockRange().InsertAfter(initVal, valueArg);
    }

    GenTreeBlk* storeBlk =
        comp->gtNewStoreBlkNode(comp->typGetBlkLayout((unsigned)lenCns), dstRefArg, valueArg, GTF_IND_UNALIGNED);
    storeBlk->gtBlkOpKind = GenTreeBlk::BlkOpKindUnroll;

    // Insert/Remove trees into LIR
    BlockRange().InsertBefore(call, storeBlk);
    if (call->IsSpecialIntrinsic(comp, NI_System_SpanHelpers_ClearWithoutReferences))
    {
        // Value didn't exist in LIR previously
        BlockRange().InsertBefore(storeBlk, valueArg);
    }

    // Remove the call and mark everything as unused ...
    BlockRange().Remove(call, true);
    // ... except the args we're going to re-use
    dstRefArg->ClearUnusedValue();
    valueArg->ClearUnusedValue();
    if (valueArg->OperIs(GT_INIT_VAL))
    {
        valueArg->gtGetOp1()->ClearUnusedValue();
    }

    JITDUMP("\nNew tree:\n");
    DISPTREERANGE(BlockRange(), storeBlk);
    *next = storeBlk;
    return true;
}

//------------------------------------------------------------------------
// LowerCallMemmove: Replace Buffer.Memmove(DST, SRC, CNS_SIZE) with a GT_STORE_BLK:
//    Do the same for CORINFO_HELP_MEMCPY(DST, SRC, CNS_SIZE)
//
//    *  STORE_BLK struct<CNS_SIZE> (copy) (Unroll)
//    +--*  LCL_VAR   byref  dst
//    \--*  IND       struct
//       \--*  LCL_VAR   byref  src
//
// Arguments:
//    tree - GenTreeCall node to replace with STORE_BLK
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::LowerCallMemmove(GenTreeCall* call, GenTree** next)
{
    JITDUMP("Considering Memmove [%06d] for unrolling.. ", comp->dspTreeID(call))
    assert(call->IsHelperCall(comp, CORINFO_HELP_MEMCPY) ||
           (comp->lookupNamedIntrinsic(call->gtCallMethHnd) == NI_System_SpanHelpers_Memmove));

    assert(call->gtArgs.CountUserArgs() == 3);

    if (comp->info.compHasNextCallRetAddr)
    {
        JITDUMP("compHasNextCallRetAddr=true so we won't be able to remove the call - bail out.\n")
        return false;
    }

    GenTree* lengthArg = call->gtArgs.GetUserArgByIndex(2)->GetNode();
    if (lengthArg->IsIntegralConst())
    {
        ssize_t cnsSize = lengthArg->AsIntCon()->IconValue();
        JITDUMP("Size=%ld.. ", (LONG)cnsSize);
        // TODO-CQ: drop the whole thing in case of 0
        if ((cnsSize > 0) && (cnsSize <= (ssize_t)comp->getUnrollThreshold(Compiler::UnrollKind::Memmove)))
        {
            JITDUMP("Accepted for unrolling!\nOld tree:\n")
            DISPTREE(call);

            GenTree* dstAddr = call->gtArgs.GetUserArgByIndex(0)->GetNode();
            GenTree* srcAddr = call->gtArgs.GetUserArgByIndex(1)->GetNode();

            // TODO-CQ: Try to create an addressing mode
            GenTreeIndir* srcBlk = comp->gtNewIndir(TYP_STRUCT, srcAddr);
            srcBlk->SetContained();

            GenTreeBlk* storeBlk = new (comp, GT_STORE_BLK)
                GenTreeBlk(GT_STORE_BLK, TYP_STRUCT, dstAddr, srcBlk, comp->typGetBlkLayout((unsigned)cnsSize));
            storeBlk->gtFlags |= (GTF_IND_UNALIGNED | GTF_ASG | GTF_EXCEPT | GTF_GLOB_REF);

            // TODO-CQ: Use GenTreeBlk::BlkOpKindUnroll here if srcAddr and dstAddr don't overlap, thus, we can
            // unroll this memmove as memcpy - it doesn't require lots of temp registers
            storeBlk->gtBlkOpKind = call->IsHelperCall(comp, CORINFO_HELP_MEMCPY) ? GenTreeBlk::BlkOpKindUnroll
                                                                                  : GenTreeBlk::BlkOpKindUnrollMemmove;

            BlockRange().InsertBefore(call, srcBlk);
            BlockRange().InsertBefore(call, storeBlk);
            BlockRange().Remove(lengthArg);
            BlockRange().Remove(call);

            // Remove all non-user args (e.g. r2r cell)
            for (CallArg& arg : call->gtArgs.Args())
            {
                if (arg.IsArgAddedLate())
                {
                    arg.GetNode()->SetUnusedValue();
                }
            }

            JITDUMP("\nNew tree:\n")
            DISPTREE(storeBlk);
            // TODO: This skips lowering srcBlk and storeBlk.
            *next = storeBlk->gtNext;
            return true;
        }
        else
        {
            JITDUMP("Size is either 0 or too big to unroll.\n")
        }
    }
    else
    {
        JITDUMP("size is not a constant.\n")
    }
    return false;
}

//------------------------------------------------------------------------
// LowerCallMemcmp: Replace SpanHelpers.SequenceEqual)(left, right, CNS_SIZE)
//    with a series of merged comparisons (via GT_IND nodes)
//
// Arguments:
//    tree - GenTreeCall node to unroll as memcmp
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::LowerCallMemcmp(GenTreeCall* call, GenTree** next)
{
    JITDUMP("Considering Memcmp [%06d] for unrolling.. ", comp->dspTreeID(call))
    assert(comp->lookupNamedIntrinsic(call->gtCallMethHnd) == NI_System_SpanHelpers_SequenceEqual);
    assert(call->gtArgs.CountUserArgs() == 3);
    assert(TARGET_POINTER_SIZE == 8);

    if (!comp->opts.OptimizationEnabled())
    {
        JITDUMP("Optimizations aren't allowed - bail out.\n")
        return false;
    }

    if (comp->info.compHasNextCallRetAddr)
    {
        JITDUMP("compHasNextCallRetAddr=true so we won't be able to remove the call - bail out.\n")
        return false;
    }

    GenTree* lengthArg = call->gtArgs.GetUserArgByIndex(2)->GetNode();
    if (lengthArg->IsIntegralConst())
    {
        ssize_t cnsSize = lengthArg->AsIntCon()->IconValue();
        JITDUMP("Size=%ld.. ", (LONG)cnsSize);
        // TODO-CQ: drop the whole thing in case of 0
        if (cnsSize > 0)
        {
            GenTree* lArg = call->gtArgs.GetUserArgByIndex(0)->GetNode();
            GenTree* rArg = call->gtArgs.GetUserArgByIndex(1)->GetNode();

            ssize_t MaxUnrollSize = 16;

#ifdef FEATURE_SIMD
#ifdef TARGET_XARCH
            if (comp->compOpportunisticallyDependsOn(InstructionSet_AVX512))
            {
                MaxUnrollSize = 128;
            }
            else if (comp->compOpportunisticallyDependsOn(InstructionSet_AVX2))
            {
                // We need AVX2 for NI_Vector256_op_Equality, fallback to Vector128 if only AVX is available
                MaxUnrollSize = 64;
            }
            else
#endif // TARGET_XARCH
            {
                MaxUnrollSize = 32;
            }
#endif // FEATURE_SIMD

            if (cnsSize <= MaxUnrollSize)
            {
                unsigned  loadWidth = 1 << BitOperations::Log2((unsigned)cnsSize);
                var_types loadType;
                if (loadWidth == 1)
                {
                    loadType = TYP_UBYTE;
                }
                else if (loadWidth == 2)
                {
                    loadType = TYP_USHORT;
                }
                else if (loadWidth == 4)
                {
                    loadType = TYP_INT;
                }
                else if ((loadWidth == 8) || (MaxUnrollSize == 16))
                {
                    loadWidth = 8;
                    loadType  = TYP_LONG;
                }
#ifdef FEATURE_SIMD
                else if ((loadWidth == 16) || (MaxUnrollSize == 32))
                {
                    loadWidth = 16;
                    loadType  = TYP_SIMD16;
                }
#ifdef TARGET_XARCH
                else if ((loadWidth == 32) || (MaxUnrollSize == 64))
                {
                    loadWidth = 32;
                    loadType  = TYP_SIMD32;
                }
                else if ((loadWidth == 64) || (MaxUnrollSize == 128))
                {
                    loadWidth = 64;
                    loadType  = TYP_SIMD64;
                }
#endif // TARGET_XARCH
#endif // FEATURE_SIMD
                else
                {
                    unreached();
                }
                var_types actualLoadType = genActualType(loadType);

                GenTree* result = nullptr;

                auto newBinaryOp = [](Compiler* comp, genTreeOps oper, var_types type, GenTree* op1,
                                      GenTree* op2) -> GenTree* {
#ifdef FEATURE_SIMD
                    if (varTypeIsSIMD(op1))
                    {
                        if (GenTree::OperIsCmpCompare(oper))
                        {
                            assert(type == TYP_INT);
                            return comp->gtNewSimdCmpOpAllNode(oper, TYP_INT, op1, op2, CORINFO_TYPE_NATIVEUINT,
                                                               genTypeSize(op1));
                        }
                        return comp->gtNewSimdBinOpNode(oper, op1->TypeGet(), op1, op2, CORINFO_TYPE_NATIVEUINT,
                                                        genTypeSize(op1));
                    }
#endif
                    return comp->gtNewOperNode(oper, type, op1, op2);
                };

                // loadWidth == cnsSize means a single load is enough for both args
                if (loadWidth == (unsigned)cnsSize)
                {
                    // We're going to emit something like the following:
                    //
                    // bool result = *(int*)leftArg == *(int*)rightArg
                    //
                    // ^ in the given example we unroll for length=4
                    //
                    GenTree* lIndir = comp->gtNewIndir(loadType, lArg);
                    GenTree* rIndir = comp->gtNewIndir(loadType, rArg);
                    result          = newBinaryOp(comp, GT_EQ, TYP_INT, lIndir, rIndir);

                    BlockRange().InsertBefore(call, lIndir, rIndir, result);
                    *next = lIndir;
                }
                else
                {
                    // First, make both args multi-use:
                    LIR::Use lArgUse;
                    LIR::Use rArgUse;
                    bool     lFoundUse = BlockRange().TryGetUse(lArg, &lArgUse);
                    bool     rFoundUse = BlockRange().TryGetUse(rArg, &rArgUse);
                    assert(lFoundUse && rFoundUse);
                    GenTree* lArgClone = comp->gtNewLclvNode(lArgUse.ReplaceWithLclVar(comp), genActualType(lArg));
                    GenTree* rArgClone = comp->gtNewLclvNode(rArgUse.ReplaceWithLclVar(comp), genActualType(rArg));
                    BlockRange().InsertBefore(call, lArgClone, rArgClone);

                    *next = lArgClone;

                    GenTree* l1Indir   = comp->gtNewIndir(loadType, lArgUse.Def());
                    GenTree* r1Indir   = comp->gtNewIndir(loadType, rArgUse.Def());
                    GenTree* l2Offs    = comp->gtNewIconNode(cnsSize - loadWidth, TYP_I_IMPL);
                    GenTree* l2AddOffs = newBinaryOp(comp, GT_ADD, lArg->TypeGet(), lArgClone, l2Offs);
                    GenTree* l2Indir   = comp->gtNewIndir(loadType, l2AddOffs);
                    GenTree* r2Offs    = comp->gtNewIconNode(cnsSize - loadWidth, TYP_I_IMPL);
                    GenTree* r2AddOffs = newBinaryOp(comp, GT_ADD, rArg->TypeGet(), rArgClone, r2Offs);
                    GenTree* r2Indir   = comp->gtNewIndir(loadType, r2AddOffs);

                    BlockRange().InsertAfter(rArgClone, l1Indir, l2Offs, l2AddOffs, l2Indir);
                    BlockRange().InsertAfter(l2Indir, r1Indir, r2Offs, r2AddOffs, r2Indir);

#ifdef TARGET_ARM64
                    if (!varTypeIsSIMD(loadType))
                    {
                        // ARM64 will get efficient ccmp codegen if we emit the normal thing:
                        //
                        // bool result = (*(int*)leftArg == *(int)rightArg) & (*(int*)(leftArg + 1) == *(int*)(rightArg
                        // +
                        // 1))

                        GenTree* eq1 = newBinaryOp(comp, GT_EQ, TYP_INT, l1Indir, r1Indir);
                        GenTree* eq2 = newBinaryOp(comp, GT_EQ, TYP_INT, l2Indir, r2Indir);
                        result       = newBinaryOp(comp, GT_AND, TYP_INT, eq1, eq2);

                        BlockRange().InsertAfter(r2Indir, eq1, eq2, result);
                    }
#endif

                    if (result == nullptr)
                    {
                        // We're going to emit something like the following:
                        //
                        // bool result = ((*(int*)leftArg ^ *(int*)rightArg) |
                        //                (*(int*)(leftArg + 1) ^ *((int*)(rightArg + 1)))) == 0;
                        //
                        // ^ in the given example we unroll for length=5
                        //
                        // In IR:
                        //
                        // *  EQ        int
                        // +--*  OR        int
                        // |  +--*  XOR       int
                        // |  |  +--*  IND       int
                        // |  |  |  \--*  LCL_VAR   byref  V1
                        // |  |  \--*  IND       int
                        // |  |     \--*  LCL_VAR   byref  V2
                        // |  \--*  XOR       int
                        // |     +--*  IND       int
                        // |     |  \--*  ADD       byref
                        // |     |     +--*  LCL_VAR   byref  V1
                        // |     |     \--*  CNS_INT   int    1
                        // |     \--*  IND       int
                        // |        \--*  ADD       byref
                        // |           +--*  LCL_VAR   byref  V2
                        // |           \--*  CNS_INT   int    1
                        // \--*  CNS_INT   int    0
                        //
                        // TODO-CQ: Do this as a general optimization similar to TryLowerAndOrToCCMP.

                        GenTree* lXor     = newBinaryOp(comp, GT_XOR, actualLoadType, l1Indir, r1Indir);
                        GenTree* rXor     = newBinaryOp(comp, GT_XOR, actualLoadType, l2Indir, r2Indir);
                        GenTree* resultOr = newBinaryOp(comp, GT_OR, actualLoadType, lXor, rXor);
                        GenTree* zeroCns  = comp->gtNewZeroConNode(actualLoadType);
                        result            = newBinaryOp(comp, GT_EQ, TYP_INT, resultOr, zeroCns);

                        BlockRange().InsertAfter(r2Indir, lXor, rXor, resultOr, zeroCns);
                        BlockRange().InsertAfter(zeroCns, result);
                    }
                }

                JITDUMP("\nUnrolled to:\n");
                DISPTREE(result);

                LIR::Use use;
                if (BlockRange().TryGetUse(call, &use))
                {
                    use.ReplaceWith(result);
                }
                else
                {
                    result->SetUnusedValue();
                }
                BlockRange().Remove(lengthArg);
                BlockRange().Remove(call);

                // Remove all non-user args (e.g. r2r cell)
                for (CallArg& arg : call->gtArgs.Args())
                {
                    if (!arg.IsUserArg())
                    {
                        arg.GetNode()->SetUnusedValue();
                    }
                }
                return true;
            }
        }
        else
        {
            JITDUMP("Size is either 0 or too big to unroll.\n")
        }
    }
    else
    {
        JITDUMP("size is not a constant.\n")
    }
    return false;
}

// do lowering steps for a call
// this includes:
//   - adding the placement nodes (either stack or register variety) for arguments
//   - lowering the expression that calculates the target address
//   - adding nodes for other operations that occur after the call sequence starts and before
//        control transfer occurs (profiling and tail call helpers, pinvoke incantations)
//
GenTree* Lowering::LowerCall(GenTree* node)
{
    GenTreeCall* call = node->AsCall();

    JITDUMP("lowering call (before):\n");
    DISPTREERANGE(BlockRange(), call);
    JITDUMP("\n");

    // All runtime lookups are expected to be expanded in fgExpandRuntimeLookups
    assert(!call->IsRuntimeLookupHelperCall(comp) ||
           (call->gtCallDebugFlags & GTF_CALL_MD_RUNTIME_LOOKUP_EXPANDED) != 0);

    // Also, always expand static cctor helper for NativeAOT, see
    // https://github.com/dotnet/runtime/issues/68278#issuecomment-1543322819
    if (comp->IsTargetAbi(CORINFO_NATIVEAOT_ABI) && comp->IsStaticHelperEligibleForExpansion(call))
    {
        assert(call->gtInitClsHnd == nullptr);
    }

#if defined(TARGET_AMD64) || defined(TARGET_ARM64)
    GenTree* nextNode = nullptr;
    if (call->IsSpecialIntrinsic())
    {
        switch (comp->lookupNamedIntrinsic(call->gtCallMethHnd))
        {
            case NI_System_SpanHelpers_Memmove:
                if (LowerCallMemmove(call, &nextNode))
                {
                    return nextNode;
                }
                break;

            case NI_System_SpanHelpers_SequenceEqual:
                if (LowerCallMemcmp(call, &nextNode))
                {
                    return nextNode;
                }
                break;

            case NI_System_SpanHelpers_Fill:
            case NI_System_SpanHelpers_ClearWithoutReferences:
                if (LowerCallMemset(call, &nextNode))
                {
                    return nextNode;
                }
                break;

            default:
                break;
        }
    }

    // Try to lower CORINFO_HELP_MEMCPY to unrollable STORE_BLK
    if (call->IsHelperCall(comp, CORINFO_HELP_MEMCPY) && LowerCallMemmove(call, &nextNode))
    {
        return nextNode;
    }

    // Try to lower CORINFO_HELP_MEMSET to unrollable STORE_BLK
    if (call->IsHelperCall(comp, CORINFO_HELP_MEMSET) && LowerCallMemset(call, &nextNode))
    {
        return nextNode;
    }
#endif

    call->ClearOtherRegs();
    LowerArgsForCall(call);

    // note that everything generated from this point might run AFTER the outgoing args are placed
    GenTree* controlExpr          = nullptr;
    bool     callWasExpandedEarly = false;

    // for x86, this is where we record ESP for checking later to make sure stack is balanced

    // Check for Delegate.Invoke(). If so, we inline it. We get the
    // target-object and target-function from the delegate-object, and do
    // an indirect call.
    if (call->IsDelegateInvoke())
    {
        controlExpr = LowerDelegateInvoke(call);
    }
    else
    {
        //  Virtual and interface calls
        switch (call->gtFlags & GTF_CALL_VIRT_KIND_MASK)
        {
            case GTF_CALL_VIRT_STUB:
                controlExpr = LowerVirtualStubCall(call);
                break;

            case GTF_CALL_VIRT_VTABLE:
                assert(call->IsVirtualVtable());
                if (!call->IsExpandedEarly())
                {
                    assert(call->gtControlExpr == nullptr);
                    controlExpr = LowerVirtualVtableCall(call);
                }
                else
                {
                    callWasExpandedEarly = true;
                    controlExpr          = call->gtControlExpr;
                }
                break;

            case GTF_CALL_NONVIRT:
                if (call->IsUnmanaged())
                {
                    controlExpr = LowerNonvirtPinvokeCall(call);
                }
                else if (call->gtCallType == CT_INDIRECT)
                {
                    controlExpr = LowerIndirectNonvirtCall(call);
                }
                else
                {
                    controlExpr = LowerDirectCall(call);
                }
                break;

            default:
                noway_assert(!"strange call type");
                break;
        }
    }

    // Indirect calls should always go through GenTreeCall::gtCallAddr and
    // should never have a control expression as well.
    assert((call->gtCallType != CT_INDIRECT) || (controlExpr == nullptr));

    if (call->IsTailCallViaJitHelper())
    {
        // Either controlExpr or gtCallAddr must contain real call target.
        if (controlExpr == nullptr)
        {
            assert(call->gtCallType == CT_INDIRECT);
            assert(call->gtCallAddr != nullptr);
            controlExpr = call->gtCallAddr;
        }

        controlExpr = LowerTailCallViaJitHelper(call, controlExpr);
    }

    // Check if we need to thread a newly created controlExpr into the LIR
    //
    if ((controlExpr != nullptr) && !callWasExpandedEarly)
    {
        LIR::Range controlExprRange = LIR::SeqTree(comp, controlExpr);

        JITDUMP("results of lowering call:\n");
        DISPRANGE(controlExprRange);

        ContainCheckRange(controlExprRange);

        BlockRange().InsertBefore(call, std::move(controlExprRange));
        call->gtControlExpr = controlExpr;

#ifdef TARGET_RISCV64
        // If controlExpr is a constant, we should contain it inside the call so that we can move the lower 12-bits of
        // the value to call instruction's (JALR) offset.
        if (controlExpr->IsCnsIntOrI() && !controlExpr->AsIntCon()->ImmedValNeedsReloc(comp) && !call->IsFastTailCall())
        {
            MakeSrcContained(call, controlExpr);
        }
#endif // TARGET_RISCV64
    }

    if (comp->opts.IsCFGEnabled())
    {
        LowerCFGCall(call);
    }

    if (call->IsFastTailCall())
    {
        // Lower fast tail call can introduce new temps to set up args correctly for Callee.
        // This involves patching LCL_VAR and LCL_VAR_ADDR nodes holding Caller stack args
        // and replacing them with a new temp. Control expr also can contain nodes that need
        // to be patched.
        // Therefore lower fast tail call must be done after controlExpr is inserted into LIR.
        // There is one side effect which is flipping the order of PME and control expression
        // since LowerFastTailCall calls InsertPInvokeMethodEpilog.
        LowerFastTailCall(call);
    }
    else
    {
        if (!call->IsHelperCall(comp, CORINFO_HELP_VALIDATE_INDIRECT_CALL))
        {
            RequireOutgoingArgSpace(call, call->gtArgs.OutgoingArgsStackSize());
        }
    }

    if (varTypeIsStruct(call))
    {
        LowerCallStruct(call);
    }

    ContainCheckCallOperands(call);
    JITDUMP("lowering call (after):\n");
    DISPTREERANGE(BlockRange(), call);
    JITDUMP("\n");
    return nullptr;
}

// Inserts profiler hook, GT_PROF_HOOK for a tail call node.
//
// AMD64:
// We need to insert this after all nested calls, but before all the arguments to this call have been set up.
// To do this, we look for the first GT_PUTARG_STK or GT_PUTARG_REG, and insert the hook immediately before
// that. If there are no args, then it should be inserted before the call node.
//
// For example:
//              *  stmtExpr  void  (top level) (IL 0x000...0x010)
// arg0 SETUP   |  /--*  argPlace  ref    REG NA $c5
// this in rcx  |  |     /--*  argPlace  ref    REG NA $c1
//              |  |     |  /--*  call      ref    System.Globalization.CultureInfo.get_InvariantCulture $c2
// arg1 SETUP   |  |     +--*  st.lclVar ref    V02 tmp1          REG NA $c2
//              |  |     |  /--*  lclVar    ref    V02 tmp1         u : 2 (last use) REG NA $c2
// arg1 in rdx  |  |     +--*  putarg_reg ref    REG NA
//              |  |     |  /--*  lclVar    ref    V00 arg0         u : 2 (last use) REG NA $80
// this in rcx  |  |     +--*  putarg_reg ref    REG NA
//              |  |  /--*  call nullcheck ref    System.String.ToLower $c5
//              |  |  {  *  stmtExpr  void  (embedded)(IL 0x000... ? ? ? )
//              |  |  {  \--*  prof_hook void   REG NA
// arg0 in rcx  |  +--*  putarg_reg ref    REG NA
// control expr |  +--*  const(h)  long   0x7ffe8e910e98 ftn REG NA
//              \--*  call      void   System.Runtime.Remoting.Identity.RemoveAppNameOrAppGuidIfNecessary $VN.Void
//
// In this case, the GT_PUTARG_REG src is a nested call. We need to put the instructions after that call
// (as shown). We assume that of all the GT_PUTARG_*, only the first one can have a nested call.
//
// X86:
// Insert the profiler hook immediately before the call. The profiler hook will preserve
// all argument registers (ECX, EDX), but nothing else.
//
// Params:
//    callNode        - tail call node
//    insertionPoint  - if non-null, insert the profiler hook before this point.
//                      If null, insert the profiler hook before args are setup
//                      but after all arg side effects are computed.
//
void Lowering::InsertProfTailCallHook(GenTreeCall* call, GenTree* insertionPoint)
{
    assert(call->IsTailCall());
    assert(comp->compIsProfilerHookNeeded());

#if defined(TARGET_X86)

    if (insertionPoint == nullptr)
    {
        insertionPoint = call;
    }

#else // !defined(TARGET_X86)

    if (insertionPoint == nullptr)
    {
        insertionPoint = FindEarliestPutArg(call);

        if (insertionPoint == nullptr)
        {
            insertionPoint = call;
        }
    }

#endif // !defined(TARGET_X86)

    assert(insertionPoint != nullptr);
    JITDUMP("Inserting profiler tail call before [%06u]\n", comp->dspTreeID(insertionPoint));

    GenTree* profHookNode = new (comp, GT_PROF_HOOK) GenTree(GT_PROF_HOOK, TYP_VOID);
    BlockRange().InsertBefore(insertionPoint, profHookNode);
}

//------------------------------------------------------------------------
// FindEarliestPutArg: Find the earliest direct PUTARG operand of a call node in
// linear order.
//
// Arguments:
//    call - the call
//
// Returns:
//    A PUTARG_* node that is the earliest of the call, or nullptr if the call
//    has no arguments.
//
GenTree* Lowering::FindEarliestPutArg(GenTreeCall* call)
{
    size_t numMarkedNodes = MarkCallPutArgAndFieldListNodes(call);

    if (numMarkedNodes <= 0)
    {
        return nullptr;
    }

    GenTree* node = call;
    do
    {
        node = node->gtPrev;

        assert((node != nullptr) && "Reached beginning of basic block while looking for marked nodes");

        if ((node->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            node->gtLIRFlags &= ~LIR::Flags::Mark;
            numMarkedNodes--;
        }
    } while (numMarkedNodes > 0);

    assert(node->OperIsPutArg());
    return node;
}

//------------------------------------------------------------------------
// MarkCallPutArgNodes: Mark all operand FIELD_LIST and PUTARG nodes
// corresponding to a call.
//
// Arguments:
//   call - the call
//
// Returns:
//   The number of nodes marked.
//
// Remarks:
//   FIELD_LIST operands are marked too, and their PUTARG operands are in turn
//   marked as well.
//
size_t Lowering::MarkCallPutArgAndFieldListNodes(GenTreeCall* call)
{
    size_t numMarkedNodes = 0;
    for (CallArg& arg : call->gtArgs.Args())
    {
        if (arg.GetEarlyNode() != nullptr)
        {
            numMarkedNodes += MarkPutArgAndFieldListNodes(arg.GetEarlyNode());
        }

        if (arg.GetLateNode() != nullptr)
        {
            numMarkedNodes += MarkPutArgAndFieldListNodes(arg.GetLateNode());
        }
    }

    return numMarkedNodes;
}

//------------------------------------------------------------------------
// MarkPutArgAndFieldListNodes: Mark all operand FIELD_LIST and PUTARG nodes
// with a LIR mark.
//
// Arguments:
//   node - the node (either a FIELD_LIST or PUTARG operand)
//
// Returns:
//   The number of marks added.
//
// Remarks:
//   FIELD_LIST operands are marked too, and their PUTARG operands are in turn
//   marked as well.
//
size_t Lowering::MarkPutArgAndFieldListNodes(GenTree* node)
{
    assert(node->OperIsPutArg() || node->OperIsFieldList());

    assert((node->gtLIRFlags & LIR::Flags::Mark) == 0);
    node->gtLIRFlags |= LIR::Flags::Mark;

    size_t result = 1;
    if (node->OperIsFieldList())
    {
        for (GenTreeFieldList::Use& operand : node->AsFieldList()->Uses())
        {
            assert(operand.GetNode()->OperIsPutArg());
            result += MarkPutArgAndFieldListNodes(operand.GetNode());
        }
    }

    return result;
}

//------------------------------------------------------------------------
// LowerFastTailCall: Lower a call node dispatched as a fast tailcall (epilog +
// jmp).
//
// Arguments:
//    call - the call node that is being dispatched as a fast tailcall.
//
// Assumptions:
//    call must be non-null.
//
// Notes:
//     For fast tail calls it is necessary to set up stack args in the incoming
//     arg stack space area. When args passed also come from this area we may
//     run into problems because we may end up overwriting the stack slot before
//     using it. For example, for foo(a, b) { return bar(b, a); }, if a and b
//     are on incoming arg stack space in foo they need to be swapped in this
//     area for the call to bar. This function detects this situation and
//     introduces a temp when an outgoing argument would overwrite a later-used
//     incoming argument.
//
//     This function also handles inserting necessary profiler hooks and pinvoke
//     method epilogs in case there are inlined pinvokes.
void Lowering::LowerFastTailCall(GenTreeCall* call)
{
#if FEATURE_FASTTAILCALL
    // Tail call restrictions i.e. conditions under which tail prefix is ignored.
    // Most of these checks are already done by importer or fgMorphTailCall().
    // This serves as a double sanity check.
    assert((comp->info.compFlags & CORINFO_FLG_SYNCH) == 0); // tail calls from synchronized methods
    assert(!comp->opts.IsReversePInvoke());                  // tail calls reverse pinvoke
    assert(!call->IsUnmanaged());                            // tail calls to unamanaged methods
    assert(!comp->compLocallocUsed);                         // tail call from methods that also do localloc

#ifdef TARGET_AMD64
    assert(!comp->getNeedsGSSecurityCookie()); // jit64 compat: tail calls from methods that need GS check
#endif                                         // TARGET_AMD64

    // We expect to see a call that meets the following conditions
    assert(call->IsFastTailCall());

    // VM cannot use return address hijacking when A() and B() tail call each
    // other in mutual recursion.  Therefore, this block is reachable through
    // a GC-safe point or the whole method is marked as fully interruptible.
    //
    // TODO-Cleanup:
    // optReachWithoutCall() depends on the fact that loop headers blocks
    // will have a block number > fgLastBB.  These loop headers gets added
    // after dominator computation and get skipped by OptReachWithoutCall().
    // The below condition cannot be asserted in lower because fgSimpleLowering()
    // can add a new basic block for range check failure which becomes
    // fgLastBB with block number > loop header block number.
    // assert(comp->compCurBB->HasFlag(BBF_GC_SAFE_POINT) ||
    //         !comp->optReachWithoutCall(comp->fgFirstBB, comp->compCurBB) || comp->GetInterruptible());

    // If PInvokes are in-lined, we have to remember to execute PInvoke method epilog anywhere that
    // a method returns.  This is a case of caller method has both PInvokes and tail calls.
    if (comp->compMethodRequiresPInvokeFrame())
    {
        InsertPInvokeMethodEpilog(comp->compCurBB DEBUGARG(call));
    }

    // Args for tail call are setup in incoming arg area.  The gc-ness of args of
    // caller and callee (which being tail called) may not match.  Therefore, everything
    // from arg setup until the epilog need to be non-interruptible by GC.  This is
    // achieved by inserting GT_START_NONGC before the very first GT_PUTARG_STK node
    // of call is setup.  Note that once a stack arg is setup, it cannot have nested
    // calls subsequently in execution order to setup other args, because the nested
    // call could over-write the stack arg that is setup earlier.
    ArrayStack<GenTree*> putargs(comp->getAllocator(CMK_ArrayStack));

    for (CallArg& arg : call->gtArgs.Args())
    {
        if (arg.GetNode()->OperIs(GT_PUTARG_STK))
        {
            putargs.Push(arg.GetNode());
        }
    }

    GenTree* startNonGCNode = nullptr;
    if (!putargs.Empty())
    {
        GenTree* firstPutargStk   = putargs.Bottom(0);
        GenTree* firstPutargStkOp = firstPutargStk->gtGetOp1();
        for (int i = 1; i < putargs.Height(); i++)
        {
            firstPutargStk   = LIR::FirstNode(firstPutargStk, putargs.Bottom(i));
            firstPutargStkOp = LIR::FirstNode(firstPutargStkOp, putargs.Bottom(i)->gtGetOp1());
        }
        // Since this is a fast tailcall each PUTARG_STK will place the argument in the
        // _incoming_ arg space area. This will effectively overwrite our already existing
        // incoming args that live in that area. If we have later uses of those args, this
        // is a problem. We introduce a defensive copy into a temp here of those args that
        // potentially may cause problems.
        for (int i = 0; i < putargs.Height(); i++)
        {
            GenTreePutArgStk* put = putargs.Bottom(i)->AsPutArgStk();

            unsigned int overwrittenStart = put->getArgOffset();
            unsigned int overwrittenEnd   = overwrittenStart + put->GetStackByteSize();

            for (unsigned callerArgLclNum = 0; callerArgLclNum < comp->info.compArgsCount; callerArgLclNum++)
            {
                LclVarDsc* callerArgDsc = comp->lvaGetDesc(callerArgLclNum);

                if (callerArgDsc->lvIsRegArg)
                {
                    continue;
                }

                const ABIPassingInformation& abiInfo = comp->lvaGetParameterABIInfo(callerArgLclNum);
                assert(abiInfo.HasExactlyOneStackSegment());
                const ABIPassingSegment& seg = abiInfo.Segment(0);

                unsigned argStart = seg.GetStackOffset();
                unsigned argEnd   = argStart + seg.GetStackSize();

                // If ranges do not overlap then this PUTARG_STK will not mess up the arg.
                if ((overwrittenEnd <= argStart) || (overwrittenStart >= argEnd))
                {
                    continue;
                }

                // Codegen cannot handle a partially overlapping copy. For
                // example, if we have
                // bar(S16 stack, S32 stack2)
                // foo(S32 stack, S32 stack2) { bar(..., stack) }
                // then we may end up having to move 'stack' in foo 16 bytes
                // ahead. It is possible that this PUTARG_STK is the only use,
                // in which case we will need to introduce a temp, so look for
                // uses starting from it. Note that we assume that in-place
                // copies are OK.
                GenTree* lookForUsesFrom = put->gtNext;
                if (overwrittenStart != argStart)
                {
                    lookForUsesFrom = firstPutargStkOp;
                }

                RehomeArgForFastTailCall(callerArgLclNum, firstPutargStkOp, lookForUsesFrom, call);
                // The above call can introduce temps and invalidate the pointer.
                callerArgDsc = comp->lvaGetDesc(callerArgLclNum);

                // For promoted locals we have more work to do as its fields could also have been invalidated.
                if (!callerArgDsc->lvPromoted)
                {
                    continue;
                }

                unsigned int fieldsFirst = callerArgDsc->lvFieldLclStart;
                unsigned int fieldsEnd   = fieldsFirst + callerArgDsc->lvFieldCnt;
                for (unsigned int j = fieldsFirst; j < fieldsEnd; j++)
                {
                    RehomeArgForFastTailCall(j, firstPutargStkOp, lookForUsesFrom, call);
                }
            }
        }

        // Now insert GT_START_NONGC node before we evaluate the first PUTARG_STK.
        // Note that if there are no args to be setup on stack, no need to
        // insert GT_START_NONGC node.
        startNonGCNode = new (comp, GT_START_NONGC) GenTree(GT_START_NONGC, TYP_VOID);
        BlockRange().InsertBefore(firstPutargStk, startNonGCNode);

        // Gc-interruptability in the following case:
        //     foo(a, b, c, d, e) { bar(a, b, c, d, e); }
        //     bar(a, b, c, d, e) { foo(a, b, d, d, e); }
        //
        // Since the instruction group starting from the instruction that sets up first
        // stack arg to the end of the tail call is marked as non-gc interruptible,
        // this will form a non-interruptible tight loop causing gc-starvation. To fix
        // this we insert GT_NO_OP as embedded stmt before GT_START_NONGC, if the method
        // has a single basic block and is not a GC-safe point.  The presence of a single
        // nop outside non-gc interruptible region will prevent gc starvation.
        if ((comp->fgBBcount == 1) && !comp->compCurBB->HasFlag(BBF_GC_SAFE_POINT))
        {
            assert(comp->fgFirstBB == comp->compCurBB);
            GenTree* noOp = new (comp, GT_NO_OP) GenTree(GT_NO_OP, TYP_VOID);
            BlockRange().InsertBefore(startNonGCNode, noOp);
        }
    }

    // Insert GT_PROF_HOOK node to emit profiler tail call hook. This should be
    // inserted before the args are setup but after the side effects of args are
    // computed. That is, GT_PROF_HOOK node needs to be inserted before GT_START_NONGC
    // node if one exists.
    if (comp->compIsProfilerHookNeeded())
    {
        InsertProfTailCallHook(call, startNonGCNode);
    }

#else // !FEATURE_FASTTAILCALL

    // Platform does not implement fast tail call mechanism. This cannot be
    // reached because we always choose to do a tailcall via helper on those
    // platforms (or no tailcall at all).
    unreached();
#endif
}
//
//------------------------------------------------------------------------
// RehomeArgForFastTailCall: Introduce temps for args that may be overwritten
// during fast tailcall sequence.
//
// Arguments:
//    lclNum - the lcl num of the arg that will be overwritten.
//    insertTempBefore - the node at which to copy the arg into a temp.
//    lookForUsesStart - the node where to start scanning and replacing uses of
//                       the arg specified by lclNum.
//    callNode - the call node that is being dispatched as a fast tailcall.
//
// Assumptions:
//    all args must be non-null.
//
// Notes:
//     This function scans for uses of the arg specified by lclNum starting
//     from the lookForUsesStart node. If it finds any uses it introduces a temp
//     for this argument and updates uses to use this instead. In the situation
//     where it introduces a temp it can thus invalidate pointers to other
//     locals.
//
void Lowering::RehomeArgForFastTailCall(unsigned int lclNum,
                                        GenTree*     insertTempBefore,
                                        GenTree*     lookForUsesStart,
                                        GenTreeCall* callNode)
{
    unsigned int tmpLclNum = BAD_VAR_NUM;
    for (GenTree* treeNode = lookForUsesStart; treeNode != callNode; treeNode = treeNode->gtNext)
    {
        if (!treeNode->OperIsLocal() && !treeNode->OperIs(GT_LCL_ADDR))
        {
            continue;
        }

        GenTreeLclVarCommon* lcl = treeNode->AsLclVarCommon();

        if (lcl->GetLclNum() != lclNum)
        {
            continue;
        }

        // Create tmp and use it in place of callerArgDsc
        if (tmpLclNum == BAD_VAR_NUM)
        {
            tmpLclNum = comp->lvaGrabTemp(true DEBUGARG("Fast tail call lowering is creating a new local variable"));

            LclVarDsc* callerArgDsc          = comp->lvaGetDesc(lclNum);
            var_types  tmpTyp                = genActualType(callerArgDsc->TypeGet());
            comp->lvaTable[tmpLclNum].lvType = tmpTyp;
            // TODO-CQ: I don't see why we should copy doNotEnreg.
            comp->lvaTable[tmpLclNum].lvDoNotEnregister = callerArgDsc->lvDoNotEnregister;
#ifdef DEBUG
            comp->lvaTable[tmpLclNum].SetDoNotEnregReason(callerArgDsc->GetDoNotEnregReason());
#endif // DEBUG

            GenTree* value;
#ifdef TARGET_ARM
            if (tmpTyp == TYP_LONG)
            {
                GenTree* loResult = comp->gtNewLclFldNode(lclNum, TYP_INT, 0);
                GenTree* hiResult = comp->gtNewLclFldNode(lclNum, TYP_INT, 4);
                value             = new (comp, GT_LONG) GenTreeOp(GT_LONG, TYP_LONG, loResult, hiResult);
            }
            else
#endif // TARGET_ARM
            {
                value = comp->gtNewLclvNode(lclNum, tmpTyp);
            }

            if (tmpTyp == TYP_STRUCT)
            {
                comp->lvaSetStruct(tmpLclNum, comp->lvaGetDesc(lclNum)->GetLayout(), false);
            }
            GenTreeLclVar* storeLclVar = comp->gtNewStoreLclVarNode(tmpLclNum, value);
            BlockRange().InsertBefore(insertTempBefore, LIR::SeqTree(comp, storeLclVar));
            ContainCheckRange(value, storeLclVar);
            LowerNode(storeLclVar);
        }

        lcl->SetLclNum(tmpLclNum);
    }
}

//------------------------------------------------------------------------
// LowerTailCallViaJitHelper: lower a call via the tailcall JIT helper. Morph
// has already inserted tailcall helper special arguments. This function inserts
// actual data for some placeholders. This function is only used on Windows x86.
//
// Lower
//      tail.call(<function args>, int numberOfOldStackArgs, int dummyNumberOfNewStackArgs, int flags, void* dummyArg)
// as
//      JIT_TailCall(<function args>, int numberOfOldStackArgsWords, int numberOfNewStackArgsWords, int flags, void*
//      callTarget)
// Note that the special arguments are on the stack, whereas the function arguments follow the normal convention.
//
// Also inserts PInvoke method epilog if required.
//
// Arguments:
//    call         -  The call node
//    callTarget   -  The real call target. This is used to replace the dummyArg during lowering.
//
// Return Value:
//    Returns control expression tree for making a call to helper Jit_TailCall.
//
GenTree* Lowering::LowerTailCallViaJitHelper(GenTreeCall* call, GenTree* callTarget)
{
    // Tail call restrictions i.e. conditions under which tail prefix is ignored.
    // Most of these checks are already done by importer or fgMorphTailCall().
    // This serves as a double sanity check.
    assert((comp->info.compFlags & CORINFO_FLG_SYNCH) == 0); // tail calls from synchronized methods
    assert(!call->IsUnmanaged());                            // tail calls to unamanaged methods
    assert(!comp->compLocallocUsed);                         // tail call from methods that also do localloc

    // We expect to see a call that meets the following conditions
    assert(call->IsTailCallViaJitHelper());
    assert(callTarget != nullptr);

    // The TailCall helper call never returns to the caller and is not GC interruptible.
    // Therefore the block containing the tail call should be a GC safe point to avoid
    // GC starvation. It is legal for the block to be unmarked iff the entry block is a
    // GC safe point, as the entry block trivially dominates every reachable block.
    assert(comp->compCurBB->HasFlag(BBF_GC_SAFE_POINT) || comp->fgFirstBB->HasFlag(BBF_GC_SAFE_POINT));

    // If PInvokes are in-lined, we have to remember to execute PInvoke method epilog anywhere that
    // a method returns.  This is a case of caller method has both PInvokes and tail calls.
    if (comp->compMethodRequiresPInvokeFrame())
    {
        InsertPInvokeMethodEpilog(comp->compCurBB DEBUGARG(call));
    }

    // Remove gtCallAddr from execution order if present.
    if (call->gtCallType == CT_INDIRECT)
    {
        assert(call->gtCallAddr != nullptr);

        bool               isClosed;
        LIR::ReadOnlyRange callAddrRange = BlockRange().GetTreeRange(call->gtCallAddr, &isClosed);
        assert(isClosed);

        BlockRange().Remove(std::move(callAddrRange));
    }

    // The callTarget tree needs to be sequenced.
    LIR::Range callTargetRange = LIR::SeqTree(comp, callTarget);

    // Verify the special args are what we expect, and replace the dummy args with real values.
    // We need to figure out the size of the outgoing stack arguments, not including the special args.
    // The number of 4-byte words is passed to the helper for the incoming and outgoing argument sizes.
    // This number is exactly the next slot number in the call's argument info struct.
    unsigned  nNewStkArgsBytes = call->gtArgs.OutgoingArgsStackSize();
    const int wordSize         = 4;
    unsigned  nNewStkArgsWords = nNewStkArgsBytes / wordSize;
    assert(nNewStkArgsWords >= 4); // There must be at least the four special stack args.
    nNewStkArgsWords -= 4;

    unsigned numArgs = call->gtArgs.CountArgs();

    // arg 0 == callTarget.
    CallArg* argEntry = call->gtArgs.GetArgByIndex(numArgs - 1);
    assert(argEntry != nullptr);
    GenTree* arg0 = argEntry->GetEarlyNode()->AsPutArgStk()->gtGetOp1();

    ContainCheckRange(callTargetRange);
    BlockRange().InsertAfter(arg0, std::move(callTargetRange));

    bool               isClosed;
    LIR::ReadOnlyRange secondArgRange = BlockRange().GetTreeRange(arg0, &isClosed);
    assert(isClosed);
    BlockRange().Remove(std::move(secondArgRange));

    argEntry->GetEarlyNode()->AsPutArgStk()->gtOp1 = callTarget;

    // arg 1 == flags
    argEntry = call->gtArgs.GetArgByIndex(numArgs - 2);
    assert(argEntry != nullptr);
    GenTree* arg1 = argEntry->GetEarlyNode()->AsPutArgStk()->gtGetOp1();
    assert(arg1->OperIs(GT_CNS_INT));

    ssize_t tailCallHelperFlags = 1 |                                  // always restore EDI,ESI,EBX
                                  (call->IsVirtualStub() ? 0x2 : 0x0); // Stub dispatch flag
    arg1->AsIntCon()->gtIconVal = tailCallHelperFlags;

    // arg 2 == numberOfNewStackArgsWords
    argEntry = call->gtArgs.GetArgByIndex(numArgs - 3);
    assert(argEntry != nullptr);
    GenTree* arg2 = argEntry->GetEarlyNode()->AsPutArgStk()->gtGetOp1();
    assert(arg2->OperIs(GT_CNS_INT));

    arg2->AsIntCon()->gtIconVal = nNewStkArgsWords;

#ifdef DEBUG
    // arg 3 == numberOfOldStackArgsWords
    argEntry = call->gtArgs.GetArgByIndex(numArgs - 4);
    assert(argEntry != nullptr);
    GenTree* arg3 = argEntry->GetEarlyNode()->AsPutArgStk()->gtGetOp1();
    assert(arg3->OperIs(GT_CNS_INT));
#endif // DEBUG

    // Transform this call node into a call to Jit tail call helper.
    call->gtCallType    = CT_HELPER;
    call->gtCallMethHnd = comp->eeFindHelper(CORINFO_HELP_TAILCALL);
    call->gtFlags &= ~GTF_CALL_VIRT_KIND_MASK;

    // Lower this as if it were a pure helper call.
    call->gtCallMoreFlags &= ~(GTF_CALL_M_TAILCALL | GTF_CALL_M_TAILCALL_VIA_JIT_HELPER);
    GenTree* result = LowerDirectCall(call);

    // Now add back tail call flags for identifying this node as tail call dispatched via helper.
    call->gtCallMoreFlags |= GTF_CALL_M_TAILCALL | GTF_CALL_M_TAILCALL_VIA_JIT_HELPER;

#ifdef PROFILING_SUPPORTED
    // Insert profiler tail call hook if needed.
    // Since we don't know the insertion point, pass null for second param.
    if (comp->compIsProfilerHookNeeded())
    {
        InsertProfTailCallHook(call, nullptr);
    }
#endif // PROFILING_SUPPORTED

    return result;
}

//------------------------------------------------------------------------
// LowerCFGCall: Potentially lower a call to use control-flow guard. This
// expands indirect calls into either a validate+call sequence or to a dispatch
// helper taking the original target in a special register.
//
// Arguments:
//    call         -  The call node
//
void Lowering::LowerCFGCall(GenTreeCall* call)
{
    assert(!call->IsHelperCall(comp, CORINFO_HELP_DISPATCH_INDIRECT_CALL));
    if (call->IsHelperCall(comp, CORINFO_HELP_VALIDATE_INDIRECT_CALL))
    {
        return;
    }

    GenTree* callTarget = call->gtCallType == CT_INDIRECT ? call->gtCallAddr : call->gtControlExpr;
    if (callTarget == nullptr)
    {
        assert((call->gtCallType != CT_INDIRECT) && (!call->IsVirtual() || call->IsVirtualStubRelativeIndir()));
        if (!call->IsVirtual())
        {
            // Direct call with stashed address
            return;
        }

        // This is a VSD call with the call target being null because we are
        // supposed to load it from the indir cell. Due to CFG we will need
        // this address twice, and at least on ARM64 we do not want to
        // materialize the constant both times.
        CallArg* indirCellArg = call->gtArgs.FindWellKnownArg(WellKnownArg::VirtualStubCell);
        assert((indirCellArg != nullptr) && indirCellArg->GetNode()->OperIs(GT_PUTARG_REG));

        GenTreeOp* putArgNode = indirCellArg->GetNode()->AsOp();
        LIR::Use   indirCellArgUse(BlockRange(), &putArgNode->gtOp1, putArgNode);

        // On non-xarch, we create a local even for constants. On xarch cloning
        // the constant is better since it can be contained in the load below.
        bool cloneConsts = false;
#ifdef TARGET_XARCH
        cloneConsts = true;
#endif

        GenTree* indirCellClone;

        if (indirCellArgUse.Def()->OperIs(GT_LCL_VAR) || (cloneConsts && indirCellArgUse.Def()->IsCnsIntOrI()))
        {
            indirCellClone = comp->gtClone(indirCellArgUse.Def());
        }
        else
        {
            unsigned newLcl = indirCellArgUse.ReplaceWithLclVar(comp);
            indirCellClone  = comp->gtNewLclvNode(newLcl, TYP_I_IMPL);
        }

        callTarget                  = Ind(indirCellClone);
        LIR::Range controlExprRange = LIR::SeqTree(comp, callTarget);
        ContainCheckRange(controlExprRange);

        BlockRange().InsertBefore(call, std::move(controlExprRange));
        call->gtControlExpr = callTarget;
    }
    else
    {
        if (callTarget->IsIntegralConst())
        {
            // This is a direct call, no CFG check is necessary.
            return;
        }
    }

    CFGCallKind cfgKind = call->GetCFGCallKind();

    switch (cfgKind)
    {
        case CFGCallKind::ValidateAndCall:
        {
            // To safely apply CFG we need to generate a very specific pattern:
            // in particular, it is a safety issue to allow the JIT to reload
            // the call target from memory between calling
            // CORINFO_HELP_VALIDATE_INDIRECT_CALL and the target. This is
            // something that would easily occur in debug codegen if we
            // produced high-level IR. Instead we will use a GT_PHYSREG node
            // to get the target back from the register that contains the target.
            //
            // Additionally, the validator does not preserve all arg registers,
            // so we have to move all GT_PUTARG_REG nodes that would otherwise
            // be trashed ahead. The JIT also has an internal invariant that
            // once GT_PUTARG nodes start to appear in LIR, the call is coming
            // up. To avoid breaking this invariant we move _all_ GT_PUTARG
            // nodes (in particular, GC info reporting relies on this).
            //
            // To sum up, we end up transforming
            //
            // ta... = <early args>
            // tb... = <late args>
            // tc = callTarget
            // GT_CALL tc, ta..., tb...
            //
            // into
            //
            // ta... = <early args> (without GT_PUTARG_* nodes)
            // tb = callTarget
            // GT_CALL CORINFO_HELP_VALIDATE_INDIRECT_CALL, tb
            // tc = GT_PHYSREG REG_VALIDATE_INDIRECT_CALL_ADDR (preserved by helper)
            // td = <moved GT_PUTARG_* nodes>
            // GT_CALL tb, ta..., td..
            //

            GenTree* regNode = PhysReg(REG_VALIDATE_INDIRECT_CALL_ADDR, TYP_I_IMPL);
            LIR::Use useOfTar;
            bool     gotUse = BlockRange().TryGetUse(callTarget, &useOfTar);
            assert(gotUse);
            useOfTar.ReplaceWith(regNode);

            // Add the call to the validator. Use a placeholder for the target while we
            // morph, sequence and lower, to avoid redoing that for the actual target.
            GenTree*     targetPlaceholder = comp->gtNewZeroConNode(callTarget->TypeGet());
            GenTreeCall* validate          = comp->gtNewHelperCallNode(CORINFO_HELP_VALIDATE_INDIRECT_CALL, TYP_VOID);
            NewCallArg   newArg =
                NewCallArg::Primitive(targetPlaceholder).WellKnown(WellKnownArg::ValidateIndirectCallTarget);
            validate->gtArgs.PushFront(comp, newArg);

            comp->fgMorphTree(validate);

            LIR::Range validateRange = LIR::SeqTree(comp, validate);
            GenTree*   validateFirst = validateRange.FirstNode();
            GenTree*   validateLast  = validateRange.LastNode();
            // Insert the validator with the call target before the late args.
            BlockRange().InsertBefore(call, std::move(validateRange));

            // Swap out the target
            gotUse = BlockRange().TryGetUse(targetPlaceholder, &useOfTar);
            assert(gotUse);
            useOfTar.ReplaceWith(callTarget);
            targetPlaceholder->SetUnusedValue();

            LowerRange(validateFirst, validateLast);

            // Insert the PHYSREG node that we must load right after validation.
            BlockRange().InsertAfter(validate, regNode);
            LowerNode(regNode);

            // Finally move all GT_PUTARG_* nodes
            MoveCFGCallArgs(call);
            break;
        }
        case CFGCallKind::Dispatch:
        {
#ifdef REG_DISPATCH_INDIRECT_CALL_ADDR
            // Now insert the call target as an extra argument.
            //
            NewCallArg callTargetNewArg =
                NewCallArg::Primitive(callTarget).WellKnown(WellKnownArg::DispatchIndirectCallTarget);
            CallArg* targetArg = call->gtArgs.PushBack(comp, callTargetNewArg);
            targetArg->SetEarlyNode(nullptr);
            targetArg->SetLateNode(callTarget);
            call->gtArgs.PushLateBack(targetArg);

            // Set up ABI information for this arg.
            targetArg->AbiInfo =
                ABIPassingInformation::FromSegmentByValue(comp,
                                                          ABIPassingSegment::InRegister(REG_DISPATCH_INDIRECT_CALL_ADDR,
                                                                                        0, TARGET_POINTER_SIZE));

            // Lower the newly added args now that call is updated
            LowerArg(call, targetArg);

            // Finally update the call to be a helper call
            call->gtCallType    = CT_HELPER;
            call->gtCallMethHnd = Compiler::eeFindHelper(CORINFO_HELP_DISPATCH_INDIRECT_CALL);
            call->gtFlags &= ~GTF_CALL_VIRT_KIND_MASK;
#ifdef FEATURE_READYTORUN
            call->gtEntryPoint.addr       = nullptr;
            call->gtEntryPoint.accessType = IAT_VALUE;
#endif

            // Now relower the call target
            call->gtControlExpr = LowerDirectCall(call);

            if (call->gtControlExpr != nullptr)
            {
                LIR::Range dispatchControlExprRange = LIR::SeqTree(comp, call->gtControlExpr);

                ContainCheckRange(dispatchControlExprRange);
                BlockRange().InsertBefore(call, std::move(dispatchControlExprRange));
            }
#else
            assert(!"Unexpected CFGCallKind::Dispatch for platform without dispatcher");
#endif
            break;
        }
        default:
            unreached();
    }
}

//------------------------------------------------------------------------
// IsCFGCallArgInvariantInRange: A cheap version of IsInvariantInRange to check
// if a node is invariant in the specified range. In other words, can 'node' be
// moved to right before 'endExclusive' without its computation changing
// values?
//
// Arguments:
//    node         -  The node.
//    endExclusive -  The exclusive end of the range to check invariance for.
//
bool Lowering::IsCFGCallArgInvariantInRange(GenTree* node, GenTree* endExclusive)
{
    assert(node->Precedes(endExclusive));

    if (node->IsInvariant())
    {
        return true;
    }

    if (!node->IsValue())
    {
        return false;
    }

    if (node->OperIsLocal())
    {
        GenTreeLclVarCommon* lcl  = node->AsLclVarCommon();
        LclVarDsc*           desc = comp->lvaGetDesc(lcl);
        if (desc->IsAddressExposed())
        {
            return false;
        }

        // Currently, non-address exposed locals have the property that their
        // use occurs at the user, so no further interference check is
        // necessary.
        return true;
    }

    return false;
}

//------------------------------------------------------------------------
// MoveCFGCallArg: Given a call that will be CFG transformed using the
// validate+call scheme, and an argument GT_PUTARG_* or GT_FIELD_LIST node,
// move that node right before the call.
//
// Arguments:
//    call - The call that is being CFG transformed
//    node - The argument node
//
// Remarks:
//    We can always move the GT_PUTARG_* node further ahead as the side-effects
//    of these nodes are handled by LSRA. However, the operands of these nodes
//    are not always safe to move further ahead; for invariant operands, we
//    move them ahead as well to shorten the lifetime of these values.
//
void Lowering::MoveCFGCallArg(GenTreeCall* call, GenTree* node)
{
    assert(node->OperIsPutArg() || node->OperIsFieldList());

    if (node->OperIsFieldList())
    {
        JITDUMP("Node is a GT_FIELD_LIST; moving all operands\n");
        for (GenTreeFieldList::Use& operand : node->AsFieldList()->Uses())
        {
            assert(operand.GetNode()->OperIsPutArg());
            MoveCFGCallArg(call, operand.GetNode());
        }
    }
    else
    {
        GenTree* operand = node->AsOp()->gtGetOp1();
        JITDUMP("Checking if we can move operand of GT_PUTARG_* node:\n");
        DISPTREE(operand);
        if (((operand->gtFlags & GTF_ALL_EFFECT) == 0) && IsCFGCallArgInvariantInRange(operand, call))
        {
            JITDUMP("...yes, moving to after validator call\n");
            BlockRange().Remove(operand);
            BlockRange().InsertBefore(call, operand);
        }
        else
        {
            JITDUMP("...no, operand has side effects or is not invariant\n");
        }
    }

    JITDUMP("Moving\n");
    DISPTREE(node);
    JITDUMP("\n");
    BlockRange().Remove(node);
    BlockRange().InsertBefore(call, node);
}

//------------------------------------------------------------------------
// MoveCFGCallArgs: Given a call that will be CFG transformed using the
// validate+call scheme, move all GT_PUTARG_* or GT_FIELD_LIST nodes right before the call.
//
// Arguments:
//    call - The call that is being CFG transformed
//
// Remarks:
//    See comments in MoveCFGCallArg for more details.
//
void Lowering::MoveCFGCallArgs(GenTreeCall* call)
{
    // Finally move all GT_PUTARG_* nodes
    for (CallArg& arg : call->gtArgs.EarlyArgs())
    {
        GenTree* node = arg.GetEarlyNode();
        assert(node->OperIsPutArg() || node->OperIsFieldList());
        MoveCFGCallArg(call, node);
    }

    for (CallArg& arg : call->gtArgs.LateArgs())
    {
        GenTree* node = arg.GetLateNode();
        assert(node->OperIsPutArg() || node->OperIsFieldList());
        MoveCFGCallArg(call, node);
    }
}

#ifndef TARGET_64BIT
//------------------------------------------------------------------------
// Lowering::DecomposeLongCompare: Decomposes a TYP_LONG compare node.
//
// Arguments:
//    cmp - the compare node
//
// Return Value:
//    The next node to lower.
//
// Notes:
//    This is done during lowering because DecomposeLongs handles only nodes
//    that produce TYP_LONG values. Compare nodes may consume TYP_LONG values
//    but produce TYP_INT values.
//
GenTree* Lowering::DecomposeLongCompare(GenTree* cmp)
{
    assert(cmp->gtGetOp1()->TypeIs(TYP_LONG));

    GenTree* src1 = cmp->gtGetOp1();
    GenTree* src2 = cmp->gtGetOp2();
    assert(src1->OperIs(GT_LONG));
    assert(src2->OperIs(GT_LONG));
    GenTree* loSrc1 = src1->gtGetOp1();
    GenTree* hiSrc1 = src1->gtGetOp2();
    GenTree* loSrc2 = src2->gtGetOp1();
    GenTree* hiSrc2 = src2->gtGetOp2();
    BlockRange().Remove(src1);
    BlockRange().Remove(src2);

    genTreeOps condition = cmp->OperGet();
    GenTree*   loCmp;
    GenTree*   hiCmp;

    if (cmp->OperIs(GT_EQ, GT_NE))
    {
        //
        // Transform (x EQ|NE y) into (((x.lo XOR y.lo) OR (x.hi XOR y.hi)) EQ|NE 0). If y is 0 then this can
        // be reduced to just ((x.lo OR x.hi) EQ|NE 0). The OR is expected to set the condition flags so we
        // don't need to generate a redundant compare against 0, we only generate a SETCC|JCC instruction.
        //
        // XOR is used rather than SUB because it is commutative and thus allows swapping the operands when
        // the first happens to be a constant. Usually only the second compare operand is a constant but it's
        // still possible to have a constant on the left side. For example, when src1 is a uint->ulong cast
        // then hiSrc1 would be 0.
        //

        if (loSrc1->OperIs(GT_CNS_INT))
        {
            std::swap(loSrc1, loSrc2);
        }

        if (loSrc2->IsIntegralConst(0))
        {
            BlockRange().Remove(loSrc2);
            loCmp = loSrc1;
        }
        else
        {
            loCmp = comp->gtNewOperNode(GT_XOR, TYP_INT, loSrc1, loSrc2);
            BlockRange().InsertBefore(cmp, loCmp);
            ContainCheckBinary(loCmp->AsOp());
        }

        if (hiSrc1->OperIs(GT_CNS_INT))
        {
            std::swap(hiSrc1, hiSrc2);
        }

        if (hiSrc2->IsIntegralConst(0))
        {
            BlockRange().Remove(hiSrc2);
            hiCmp = hiSrc1;
        }
        else
        {
            hiCmp = comp->gtNewOperNode(GT_XOR, TYP_INT, hiSrc1, hiSrc2);
            BlockRange().InsertBefore(cmp, hiCmp);
            ContainCheckBinary(hiCmp->AsOp());
        }

        hiCmp = comp->gtNewOperNode(GT_OR, TYP_INT, loCmp, hiCmp);
        BlockRange().InsertBefore(cmp, hiCmp);
        ContainCheckBinary(hiCmp->AsOp());
    }
    else
    {
        assert(cmp->OperIs(GT_LT, GT_LE, GT_GE, GT_GT));

        //
        // If the compare is signed then (x LT|GE y) can be transformed into ((x SUB y) LT|GE 0).
        // If the compare is unsigned we can still use SUB but we need to check the Carry flag,
        // not the actual result. In both cases we can simply check the appropriate condition flags
        // and ignore the actual result:
        //     SUB_LO loSrc1, loSrc2
        //     SUB_HI hiSrc1, hiSrc2
        //     SETCC|JCC (signed|unsigned LT|GE)
        // If loSrc2 happens to be 0 then the first SUB can be eliminated and the second one can
        // be turned into a CMP because the first SUB would have set carry to 0. This effectively
        // transforms a long compare against 0 into an int compare of the high part against 0.
        //
        // (x LE|GT y) can to be transformed into ((x SUB y) LE|GT 0) but checking that a long value
        // is greater than 0 is not so easy. We need to turn this into a positive/negative check
        // like the one we get for LT|GE compares, this can be achieved by swapping the compare:
        //     (x LE|GT y) becomes (y GE|LT x)
        //
        // Having to swap operands is problematic when the second operand is a constant. The constant
        // moves to the first operand where it cannot be contained and thus needs a register. This can
        // be avoided by changing the constant such that LE|GT becomes LT|GE:
        //     (x LE|GT 41) becomes (x LT|GE 42)
        //

        if (cmp->OperIs(GT_LE, GT_GT))
        {
            bool mustSwap = true;

            if (loSrc2->OperIs(GT_CNS_INT) && hiSrc2->OperIs(GT_CNS_INT))
            {
                uint32_t loValue  = static_cast<uint32_t>(loSrc2->AsIntCon()->IconValue());
                uint32_t hiValue  = static_cast<uint32_t>(hiSrc2->AsIntCon()->IconValue());
                uint64_t value    = static_cast<uint64_t>(loValue) | (static_cast<uint64_t>(hiValue) << 32);
                uint64_t maxValue = cmp->IsUnsigned() ? UINT64_MAX : INT64_MAX;

                if (value != maxValue)
                {
                    value++;
                    loValue = value & UINT32_MAX;
                    hiValue = (value >> 32) & UINT32_MAX;
                    loSrc2->AsIntCon()->SetIconValue(loValue);
                    hiSrc2->AsIntCon()->SetIconValue(hiValue);

                    condition = cmp->OperIs(GT_LE) ? GT_LT : GT_GE;
                    mustSwap  = false;
                }
            }

            if (mustSwap)
            {
                std::swap(loSrc1, loSrc2);
                std::swap(hiSrc1, hiSrc2);
                condition = GenTree::SwapRelop(condition);
            }
        }

        assert((condition == GT_LT) || (condition == GT_GE));

        if (loSrc2->IsIntegralConst(0))
        {
            BlockRange().Remove(loSrc2);

            // Very conservative dead code removal... but it helps.

            if (loSrc1->OperIs(GT_CNS_INT, GT_LCL_VAR, GT_LCL_FLD))
            {
                BlockRange().Remove(loSrc1);
            }
            else
            {
                loSrc1->SetUnusedValue();
            }

            hiCmp = comp->gtNewOperNode(GT_CMP, TYP_VOID, hiSrc1, hiSrc2);
            BlockRange().InsertBefore(cmp, hiCmp);
            ContainCheckCompare(hiCmp->AsOp());
        }
        else
        {
            loCmp = comp->gtNewOperNode(GT_CMP, TYP_VOID, loSrc1, loSrc2);
            loCmp->gtFlags |= GTF_SET_FLAGS;
            hiCmp = comp->gtNewOperNode(GT_SUB_HI, TYP_INT, hiSrc1, hiSrc2);
            BlockRange().InsertBefore(cmp, loCmp, hiCmp);
            ContainCheckCompare(loCmp->AsOp());
            ContainCheckBinary(hiCmp->AsOp());

            //
            // Try to move the first SUB_HI operands right in front of it, this allows using
            // a single temporary register instead of 2 (one for CMP and one for SUB_HI). Do
            // this only for locals as they won't change condition flags. Note that we could
            // move constants (except 0 which generates XOR reg, reg) but it's extremely rare
            // to have a constant as the first operand.
            //

            if (hiSrc1->OperIs(GT_LCL_VAR, GT_LCL_FLD) && IsInvariantInRange(hiSrc1, hiCmp))
            {
                BlockRange().Remove(hiSrc1);
                BlockRange().InsertBefore(hiCmp, hiSrc1);
            }
        }
    }

    hiCmp->gtFlags |= GTF_SET_FLAGS;
    if (hiCmp->IsValue())
    {
        hiCmp->SetUnusedValue();
    }

    LIR::Use cmpUse;
    if (BlockRange().TryGetUse(cmp, &cmpUse) && cmpUse.User()->OperIs(GT_JTRUE))
    {
        BlockRange().Remove(cmp);

        GenTree* jcc       = cmpUse.User();
        jcc->AsOp()->gtOp1 = nullptr;
        jcc->ChangeOper(GT_JCC);
        jcc->AsCC()->gtCondition = GenCondition::FromIntegralRelop(condition, cmp->IsUnsigned());
    }
    else
    {
        cmp->AsOp()->gtOp1 = nullptr;
        cmp->AsOp()->gtOp2 = nullptr;
        cmp->ChangeOper(GT_SETCC);
        cmp->AsCC()->gtCondition = GenCondition::FromIntegralRelop(condition, cmp->IsUnsigned());
    }

    return cmp->gtNext;
}
#endif // !TARGET_64BIT

//------------------------------------------------------------------------
// Lowering::OptimizeConstCompare: Performs various "compare with const" optimizations.
//
// Arguments:
//    cmp - the compare node
//
// Return Value:
//    The original compare node if lowering should proceed as usual or the next node
//    to lower if the compare node was changed in such a way that lowering is no
//    longer needed.
//
// Notes:
//    - Narrow operands to enable memory operand containment (XARCH specific).
//    - Transform cmp(and(x, y), 0) into test(x, y) (XARCH/Arm64 specific but could
//      be used for ARM as well if support for GT_TEST_EQ/GT_TEST_NE is added).
//    - Transform TEST(x, LSH(1, y)) into BT(x, y) (XARCH specific)
//    - Transform RELOP(OP, 0) into SETCC(OP) or JCC(OP) if OP can set the
//      condition flags appropriately (XARCH/ARM64 specific but could be extended
//      to ARM32 as well if ARM32 codegen supports GTF_SET_FLAGS).
//
GenTree* Lowering::OptimizeConstCompare(GenTree* cmp)
{
    assert(cmp->gtGetOp2()->IsIntegralConst());

    GenTree*       op1 = cmp->gtGetOp1();
    GenTreeIntCon* op2 = cmp->gtGetOp2()->AsIntCon();

#if defined(TARGET_XARCH) || defined(TARGET_ARM64)
    ssize_t op2Value = op2->IconValue();

#ifdef TARGET_XARCH
    var_types op1Type = op1->TypeGet();
    if (IsContainableMemoryOp(op1) && varTypeIsSmall(op1Type) && FitsIn(op1Type, op2Value))
    {
        //
        // If op1's type is small then try to narrow op2 so it has the same type as op1.
        // Small types are usually used by memory loads and if both compare operands have
        // the same type then the memory load can be contained. In certain situations
        // (e.g "cmp ubyte, 200") we also get a smaller instruction encoding.
        //

        op2->gtType = op1Type;
    }
    else
#endif
        if (op1->OperIs(GT_CAST) && !op1->gtOverflow())
    {
        GenTreeCast* cast       = op1->AsCast();
        var_types    castToType = cast->CastToType();
        GenTree*     castOp     = cast->gtGetOp1();

        if ((castToType == TYP_UBYTE) && FitsIn<UINT8>(op2Value))
        {
            //
            // Since we're going to remove the cast we need to be able to narrow the cast operand
            // to the cast type. This can be done safely only for certain opers (e.g AND, OR, XOR).
            // Some opers just can't be narrowed (e.g DIV, MUL) while other could be narrowed but
            // doing so would produce incorrect results (e.g. RSZ, RSH).
            //
            // The below list of handled opers is conservative but enough to handle the most common
            // situations.
            //
            bool removeCast =
#ifdef TARGET_ARM64
                (op2Value == 0) && cmp->OperIs(GT_EQ, GT_NE, GT_GT) && !castOp->isContained() &&
#endif
                (castOp->OperIs(GT_LCL_VAR, GT_CALL, GT_OR, GT_XOR, GT_AND)
#ifdef TARGET_XARCH
                 || IsContainableMemoryOp(castOp)
#endif
                );

            if (removeCast)
            {
                assert(!castOp->gtOverflowEx()); // Must not be an overflow checking operation

#ifdef TARGET_ARM64
                bool cmpEq = cmp->OperIs(GT_EQ);

                cmp->SetOperRaw(cmpEq ? GT_TEST_EQ : GT_TEST_NE);
                op2->SetIconValue(0xff);
                op2->gtType = castOp->gtType;
#else
                castOp->gtType = castToType;
                op2->gtType    = castToType;
#endif
                // If we have any contained memory ops on castOp, they must now not be contained.
                castOp->ClearContained();

                if (castOp->OperIs(GT_OR, GT_XOR, GT_AND))
                {
                    castOp->gtGetOp1()->ClearContained();
                    castOp->gtGetOp2()->ClearContained();
                    ContainCheckBinary(castOp->AsOp());
                }

                cmp->AsOp()->gtOp1 = castOp;

                BlockRange().Remove(cast);
            }
        }
    }
    else if (op1->OperIs(GT_AND) && cmp->OperIs(GT_EQ, GT_NE))
    {
        //
        // Transform ((x AND y) EQ|NE 0) into (x TEST_EQ|TEST_NE y) when possible.
        //

        GenTree* andOp1 = op1->gtGetOp1();
        GenTree* andOp2 = op1->gtGetOp2();

        //
        // If we don't have a 0 compare we can get one by transforming ((x AND mask) EQ|NE mask)
        // into ((x AND mask) NE|EQ 0) when mask is a single bit.
        //
        if ((op2Value != 0) && isPow2(static_cast<target_size_t>(op2Value)) && andOp2->IsIntegralConst(op2Value))
        {
            op2Value = 0;
            op2->SetIconValue(0);
            cmp->SetOperRaw(GenTree::ReverseRelop(cmp->OperGet()));
        }

        // Optimizes (X & 1) != 0 to (X & 1)
        // Optimizes (X & 1) == 0 to ((NOT X) & 1)
        // (== 1 or != 1) cases are transformed to (!= 0 or == 0) above
        // The compiler requires jumps to have relop operands, so we do not fold that case.

        const bool optimizeToAnd    = (op2Value == 0) && cmp->OperIs(GT_NE);
        const bool optimizeToNotAnd = (op2Value == 0) && cmp->OperIs(GT_EQ);

        if ((andOp2->IsIntegralConst(1)) && (genActualType(op1) == cmp->TypeGet()) &&
            (optimizeToAnd || optimizeToNotAnd))
        {
            LIR::Use cmpUse;
            if (BlockRange().TryGetUse(cmp, &cmpUse) && !cmpUse.User()->OperIs(GT_JTRUE) &&
                !cmpUse.User()->OperIsConditional())
            {
                GenTree* next = cmp->gtNext;

                if (optimizeToNotAnd)
                {
                    GenTree* notNode   = comp->gtNewOperNode(GT_NOT, andOp1->TypeGet(), andOp1);
                    op1->AsOp()->gtOp1 = notNode;
                    BlockRange().InsertAfter(andOp1, notNode);
                }

                cmpUse.ReplaceWith(op1);

                BlockRange().Remove(cmp->gtGetOp2());
                BlockRange().Remove(cmp);

                return next;
            }
        }

        if (op2Value == 0)
        {
            BlockRange().Remove(op1);
            BlockRange().Remove(op2);

            cmp->SetOperRaw(cmp->OperIs(GT_EQ) ? GT_TEST_EQ : GT_TEST_NE);
            cmp->AsOp()->gtOp1 = andOp1;
            cmp->AsOp()->gtOp2 = andOp2;
            // We will re-evaluate containment below
            andOp1->ClearContained();
            andOp2->ClearContained();

#ifdef TARGET_XARCH
            if (IsContainableMemoryOp(andOp1) && andOp2->IsIntegralConst())
            {
                //
                // For "test" we only care about the bits that are set in the second operand (mask).
                // If the mask fits in a small type then we can narrow both operands to generate a "test"
                // instruction with a smaller encoding ("test" does not have a r/m32, imm8 form) and avoid
                // a widening load in some cases.
                //
                // For 16 bit operands we narrow only if the memory operand is already 16 bit. This matches
                // the behavior of a previous implementation and avoids adding more cases where we generate
                // 16 bit instructions that require a length changing prefix (0x66). These suffer from
                // significant decoder stalls on Intel CPUs.
                //
                // We could also do this for 64 bit masks that fit into 32 bit but it doesn't help.
                // In such cases morph narrows down the existing GT_AND by inserting a cast between it and
                // the memory operand so we'd need to add more code to recognize and eliminate that cast.
                //

                size_t mask = static_cast<size_t>(andOp2->AsIntCon()->IconValue());

                if (FitsIn<UINT8>(mask))
                {
                    andOp1->gtType = TYP_UBYTE;
                    andOp2->gtType = TYP_UBYTE;
                }
                else if (FitsIn<UINT16>(mask) && genTypeSize(andOp1) == 2)
                {
                    andOp1->gtType = TYP_USHORT;
                    andOp2->gtType = TYP_USHORT;
                }
            }
#endif
        }
        else if (andOp2->IsIntegralConst() && GenTree::Compare(andOp2, op2))
        {
            //
            // Transform EQ|NE(AND(x, y), y) into EQ|NE(AND(NOT(x), y), 0) when y is a constant.
            //

            andOp1->ClearContained();
            GenTree* notNode               = comp->gtNewOperNode(GT_NOT, andOp1->TypeGet(), andOp1);
            cmp->gtGetOp1()->AsOp()->gtOp1 = notNode;
            BlockRange().InsertAfter(andOp1, notNode);
            op2->BashToZeroConst(op2->TypeGet());

            andOp1   = notNode;
            op2Value = 0;
        }
    }

#ifdef TARGET_XARCH
    if (cmp->OperIs(GT_TEST_EQ, GT_TEST_NE))
    {
        //
        // Transform TEST_EQ|NE(x, LSH(1, y)) or TEST_EQ|NE(LSH(1, y), x) into BT(x, y) when possible. Using BT
        // results in smaller and faster code. It also doesn't have special register
        // requirements, unlike LSH that requires the shift count to be in ECX.
        // Note that BT has the same behavior as LSH when the bit index exceeds the
        // operand bit size - it uses (bit_index MOD bit_size).
        //

        GenTree* lsh = cmp->AsOp()->gtOp1;
        GenTree* op  = cmp->AsOp()->gtOp2;

        if (!lsh->OperIs(GT_LSH))
        {
            std::swap(lsh, op);
        }

        if (lsh->OperIs(GT_LSH) && varTypeIsIntOrI(lsh) && lsh->gtGetOp1()->IsIntegralConst(1))
        {
            cmp->SetOper(cmp->OperIs(GT_TEST_EQ) ? GT_BITTEST_EQ : GT_BITTEST_NE);

            BlockRange().Remove(lsh->gtGetOp1());
            BlockRange().Remove(lsh);

            cmp->AsOp()->gtOp1 = op;
            cmp->AsOp()->gtOp2 = lsh->gtGetOp2();
            cmp->gtGetOp2()->ClearContained();

            return cmp->gtNext;
        }
    }
#endif // TARGET_XARCH
#endif // defined(TARGET_XARCH) || defined(TARGET_ARM64)

    // Optimize EQ/NE(relop/SETCC, 0) into (maybe reversed) cond.
    if (cmp->OperIs(GT_EQ, GT_NE) && op2->IsIntegralConst(0) && (op1->OperIsCompare() || op1->OperIs(GT_SETCC)))
    {
        LIR::Use use;
        if (BlockRange().TryGetUse(cmp, &use))
        {
            if (cmp->OperIs(GT_EQ))
            {
                GenTree* reversed = comp->gtReverseCond(op1);
                assert(reversed == op1);
            }

            // Relops and SETCC can be either TYP_INT or TYP_LONG typed, so we
            // may need to retype it.
            op1->gtType = cmp->TypeGet();

            GenTree* next = cmp->gtNext;
            use.ReplaceWith(op1);
            BlockRange().Remove(cmp->gtGetOp2());
            BlockRange().Remove(cmp);
            return next;
        }
    }

    // Optimize EQ/NE(op_that_sets_zf, 0) into op_that_sets_zf with GTF_SET_FLAGS + SETCC.
    LIR::Use use;
    if (cmp->OperIs(GT_EQ, GT_NE) && op2->IsIntegralConst(0) && op1->SupportsSettingZeroFlag() &&
        BlockRange().TryGetUse(cmp, &use) && IsProfitableToSetZeroFlag(op1))
    {
        op1->gtFlags |= GTF_SET_FLAGS;
        op1->SetUnusedValue();

        GenTree* next = cmp->gtNext;
        BlockRange().Remove(cmp);
        BlockRange().Remove(op2);

        GenCondition cmpCondition = GenCondition::FromRelop(cmp);
        GenTreeCC*   setcc        = comp->gtNewCC(GT_SETCC, cmp->TypeGet(), cmpCondition);
        BlockRange().InsertAfter(op1, setcc);

        use.ReplaceWith(setcc);
        return next;
    }

    return cmp;
}

//------------------------------------------------------------------------
// Lowering::LowerCompare: Lowers a compare node.
//
// Arguments:
//    cmp - the compare node
//
// Return Value:
//    The next node to lower.
//
GenTree* Lowering::LowerCompare(GenTree* cmp)
{
#ifndef TARGET_64BIT
    if (cmp->gtGetOp1()->TypeIs(TYP_LONG))
    {
        return DecomposeLongCompare(cmp);
    }
#endif

    if (cmp->gtGetOp2()->IsIntegralConst() && !comp->opts.MinOpts())
    {
        GenTree* next = OptimizeConstCompare(cmp);

        // If OptimizeConstCompare return the compare node as "next" then we need to continue lowering.
        if (next != cmp)
        {
            return next;
        }
    }

#ifdef TARGET_XARCH
    if (cmp->gtGetOp1()->TypeGet() == cmp->gtGetOp2()->TypeGet())
    {
        if (varTypeIsSmall(cmp->gtGetOp1()->TypeGet()) && varTypeIsUnsigned(cmp->gtGetOp1()->TypeGet()))
        {
            //
            // If both operands have the same type then codegen will use the common operand type to
            // determine the instruction type. For small types this would result in performing a
            // signed comparison of two small unsigned values without zero extending them to TYP_INT
            // which is incorrect. Note that making the comparison unsigned doesn't imply that codegen
            // has to generate a small comparison, it can still correctly generate a TYP_INT comparison.
            //

            cmp->gtFlags |= GTF_UNSIGNED;
        }
    }
#endif // TARGET_XARCH
    ContainCheckCompare(cmp->AsOp());
    return cmp->gtNext;
}

#if !defined(TARGET_LOONGARCH64) && !defined(TARGET_RISCV64)
//------------------------------------------------------------------------
// Lowering::LowerJTrue: Lowers a JTRUE node.
//
// Arguments:
//    jtrue - the JTRUE node
//
// Return Value:
//    The next node to lower (usually nullptr).
//
// Notes:
//    On ARM64 this may remove the JTRUE node and transform its associated
//    relop into a JCMP node.
//
GenTree* Lowering::LowerJTrue(GenTreeOp* jtrue)
{
    GenTree* cond = jtrue->gtGetOp1();

    JITDUMP("Lowering JTRUE:\n");
    DISPTREERANGE(BlockRange(), jtrue);
    JITDUMP("\n");

#if defined(TARGET_ARM64)
    if (cond->OperIsCompare() && cond->gtGetOp2()->IsCnsIntOrI())
    {
        GenTree*     relopOp1 = cond->gtGetOp1();
        GenTree*     relopOp2 = cond->gtGetOp2();
        genTreeOps   newOper  = GT_COUNT;
        GenCondition cc;

        if (cond->OperIs(GT_EQ, GT_NE) && relopOp2->IsIntegralConst(0))
        {
            // Codegen will use cbz or cbnz in codegen which do not affect the flag register
            newOper = GT_JCMP;
            cc      = GenCondition::FromRelop(cond);
        }
        else if (cond->OperIs(GT_LT, GT_GE) && !cond->IsUnsigned() && relopOp2->IsIntegralConst(0))
        {
            // Codegen will use tbnz or tbz in codegen which do not affect the flag register
            newOper = GT_JTEST;
            cc      = cond->OperIs(GT_LT) ? GenCondition(GenCondition::NE) : GenCondition(GenCondition::EQ);
            // x < 0 => (x & signBit) != 0. Update the constant to be the sign bit.
            relopOp2->AsIntConCommon()->SetIntegralValue(
                (static_cast<INT64>(1) << (8 * genTypeSize(genActualType(relopOp1)) - 1)));
        }
        else if (cond->OperIs(GT_TEST_EQ, GT_TEST_NE) && isPow2(relopOp2->AsIntCon()->IconValue()))
        {
            // Codegen will use tbz or tbnz in codegen which do not affect the flag register
            newOper = GT_JTEST;
            cc      = GenCondition::FromRelop(cond);
        }

        if (newOper != GT_COUNT)
        {
            jtrue->ChangeOper(newOper);
            jtrue->gtOp1                 = relopOp1;
            jtrue->gtOp2                 = relopOp2;
            jtrue->AsOpCC()->gtCondition = cc;

            relopOp2->SetContained();

            BlockRange().Remove(cond);
            JITDUMP("Lowered to %s\n", GenTree::OpName(newOper));
            return nullptr;
        }
    }
#endif // TARGET_ARM64

    GenCondition condCode;
    if (TryLowerConditionToFlagsNode(jtrue, cond, &condCode))
    {
        jtrue->SetOper(GT_JCC);
        jtrue->AsCC()->gtCondition = condCode;
    }

    JITDUMP("Lowering JTRUE Result:\n");
    DISPTREERANGE(BlockRange(), jtrue);
    JITDUMP("\n");

    return nullptr;
}
#endif // !TARGET_LOONGARCH64 && !TARGET_RISCV64

//----------------------------------------------------------------------------------------------
// LowerSelect: Lower a GT_SELECT node.
//
// Arguments:
//     select - The node
//
// Return Value:
//     The next node to lower.
//
GenTree* Lowering::LowerSelect(GenTreeConditional* select)
{
    GenTree* cond     = select->gtCond;
    GenTree* trueVal  = select->gtOp1;
    GenTree* falseVal = select->gtOp2;

    // Replace SELECT cond 1/0 0/1 with (perhaps reversed) cond
    if (cond->OperIsCompare() && ((trueVal->IsIntegralConst(0) && falseVal->IsIntegralConst(1)) ||
                                  (trueVal->IsIntegralConst(1) && falseVal->IsIntegralConst(0))))
    {
        assert(select->TypeIs(TYP_INT, TYP_LONG));

        LIR::Use use;
        if (BlockRange().TryGetUse(select, &use))
        {
            if (trueVal->IsIntegralConst(0))
            {
                GenTree* reversed = comp->gtReverseCond(cond);
                assert(reversed == cond);
            }

            // Codegen supports also TYP_LONG typed compares so we can just
            // retype the compare instead of inserting a cast.
            cond->gtType = select->TypeGet();

            BlockRange().Remove(trueVal);
            BlockRange().Remove(falseVal);
            BlockRange().Remove(select);
            use.ReplaceWith(cond);

            return cond->gtNext;
        }
    }

    JITDUMP("Lowering select:\n");
    DISPTREERANGE(BlockRange(), select);
    JITDUMP("\n");

    // Do not transform GT_SELECT with GTF_SET_FLAGS into GT_SELECTCC; this
    // node is used by decomposition on x86.
    // TODO-CQ: If we allowed multiple nodes to consume the same CPU flags then
    // we could do this on x86. We currently disable if-conversion for TYP_LONG
    // on 32-bit architectures because of this.
    GenCondition selectCond;
    GenTreeOpCC* newSelect = nullptr;
    if (((select->gtFlags & GTF_SET_FLAGS) == 0) && TryLowerConditionToFlagsNode(select, cond, &selectCond))
    {
        select->SetOper(GT_SELECTCC);
        newSelect              = select->AsOpCC();
        newSelect->gtCondition = selectCond;
        ContainCheckSelect(newSelect);
        JITDUMP("Converted to SELECTCC:\n");
        DISPTREERANGE(BlockRange(), newSelect);
        JITDUMP("\n");
    }
    else
    {
        ContainCheckSelect(select);
    }

#ifdef TARGET_ARM64
    if (trueVal->OperIs(GT_NOT, GT_NEG, GT_ADD) || falseVal->OperIs(GT_NOT, GT_NEG, GT_ADD))
    {
        TryLowerCselToCSOp(select, cond);
    }
    else if (trueVal->IsCnsIntOrI() && falseVal->IsCnsIntOrI())
    {
        TryLowerCnsIntCselToCinc(select, cond);
    }
#endif

    return newSelect != nullptr ? newSelect->gtNext : select->gtNext;
}

//----------------------------------------------------------------------------------------------
// TryLowerConditionToFlagsNode: Given a node 'parent' that is able to consume
// conditions from CPU flags, try to transform 'condition' into a node that
// produces CPU flags, and reorder it to happen right before 'parent'.
//
// Arguments:
//     parent    - The parent node that can consume from CPU flags.
//     condition - The condition that to try to transform into something that produces CPU flags.
//     code      - [out] The condition code that makes the condition true.
//
// Return Value:
//     True if relop was transformed and is now right before 'parent'; otherwise false.
//
bool Lowering::TryLowerConditionToFlagsNode(GenTree*      parent,
                                            GenTree*      condition,
                                            GenCondition* cond,
                                            bool          allowMultipleFlagsChecks)
{
    JITDUMP("Lowering condition:\n");
    DISPTREERANGE(BlockRange(), condition);
    JITDUMP("\n");

    if (condition->OperIsCompare())
    {
        if (!IsInvariantInRange(condition, parent))
        {
            return false;
        }

        GenTreeOp* relop = condition->AsOp();

        *cond           = GenCondition::FromRelop(relop);
        bool optimizing = comp->opts.OptimizationEnabled();

        GenTree* relopOp1 = relop->gtGetOp1();
        GenTree* relopOp2 = relop->gtGetOp2();

#ifdef TARGET_XARCH
        // Optimize FP x != x to only check parity flag. This is a common way of
        // checking NaN and avoids two branches that we would otherwise emit.
        if (optimizing && (cond->GetCode() == GenCondition::FNEU) && relopOp1->OperIsLocal() &&
            GenTree::Compare(relopOp1, relopOp2) && IsInvariantInRange(relopOp1, relop) &&
            IsInvariantInRange(relopOp2, relop))
        {
            *cond = GenCondition(GenCondition::P);
        }
#endif

#if !defined(TARGET_LOONGARCH64) && !defined(TARGET_RISCV64)
        if (!allowMultipleFlagsChecks)
        {
            const GenConditionDesc& desc = GenConditionDesc::Get(*cond);

            if (desc.oper != GT_NONE)
            {
                return false;
            }
        }
#endif

        relop->gtType = TYP_VOID;
        relop->gtFlags |= GTF_SET_FLAGS;

        if (relop->OperIs(GT_EQ, GT_NE, GT_LT, GT_LE, GT_GE, GT_GT))
        {
            relop->SetOper(GT_CMP);

            if (cond->PreferSwap())
            {
                std::swap(relop->gtOp1, relop->gtOp2);
                *cond = GenCondition::Swap(*cond);
            }
        }
#ifdef TARGET_XARCH
        else if (relop->OperIs(GT_BITTEST_EQ, GT_BITTEST_NE))
        {
            relop->SetOper(GT_BT);
        }
#endif
        else
        {
            assert(relop->OperIs(GT_TEST_EQ, GT_TEST_NE));
            relop->SetOper(GT_TEST);
        }

        if (relop->gtNext != parent)
        {
            BlockRange().Remove(relop);
            BlockRange().InsertBefore(parent, relop);
        }

        return true;
    }

    if (condition->OperIs(GT_SETCC))
    {
        assert((condition->gtPrev->gtFlags & GTF_SET_FLAGS) != 0);
        GenTree* flagsDef = condition->gtPrev;
#if defined(TARGET_ARM64) || defined(TARGET_AMD64)
        // CCMP is a flag producing node that also consumes flags, so find the
        // "root" of the flags producers and move the entire range.
        // We limit this to 10 nodes look back to avoid quadratic behavior.
        for (int i = 0; i < 10 && flagsDef->OperIs(GT_CCMP); i++)
        {
            assert((flagsDef->gtPrev != nullptr) && ((flagsDef->gtPrev->gtFlags & GTF_SET_FLAGS) != 0));
            flagsDef = flagsDef->gtPrev;
        }
#endif
        if (!IsRangeInvariantInRange(flagsDef, condition->gtPrev, parent, condition))
        {
            return false;
        }

        *cond = condition->AsCC()->gtCondition;

#if !defined(TARGET_LOONGARCH64) && !defined(TARGET_RISCV64)
        if (!allowMultipleFlagsChecks)
        {
            const GenConditionDesc& desc = GenConditionDesc::Get(*cond);

            if (desc.oper != GT_NONE)
            {
                return false;
            }
        }
#endif

        LIR::Range range = BlockRange().Remove(flagsDef, condition->gtPrev);
        BlockRange().InsertBefore(parent, std::move(range));
        BlockRange().Remove(condition);
        return true;
    }

    return false;
}

//----------------------------------------------------------------------------------------------
// LowerNodeCC: Lowers a node that produces a boolean value by setting the condition flags.
//
// Arguments:
//     node - The node to lower
//     condition - The condition code of the generated SETCC/JCC node
//
// Return Value:
//     A SETCC/JCC node or nullptr if `node` is not used.
//
// Notes:
//     This simply replaces `node`'s use with an appropriate SETCC/JCC node,
//     `node` is not actually changed, except by having its GTF_SET_FLAGS set.
//     It's the caller's responsibility to change `node` such that it only
//     sets the condition flags, without producing a boolean value.
//
GenTreeCC* Lowering::LowerNodeCC(GenTree* node, GenCondition condition)
{
    // Skip over a chain of EQ/NE(x, 0) relops. This may be present either
    // because `node` is not a relop and so it cannot be used directly by a
    // JTRUE, or because the frontend failed to remove a EQ/NE(x, 0) that's
    // used as logical negation.
    //
    // Usually there's only one such relop but there's little difference
    // between removing one or all so we may as well remove them all.
    //
    // We can't allow any other nodes between `node` and its user because we
    // have no way of knowing if those nodes change flags or not. So we're looking
    // to skip over a sequence of appropriately connected zero and EQ/NE nodes.

    // The x in EQ/NE(x, 0)
    GenTree* relop = node;
    // The first node of the relop sequence
    GenTree* first = node->gtNext;
    // The node following the relop sequence
    GenTree* next = first;

    while ((next != nullptr) && next->IsIntegralConst(0) && (next->gtNext != nullptr) &&
           next->gtNext->OperIs(GT_EQ, GT_NE) && (next->gtNext->AsOp()->gtGetOp1() == relop) &&
           (next->gtNext->AsOp()->gtGetOp2() == next))
    {
        relop = next->gtNext;
        next  = relop->gtNext;

        if (relop->OperIs(GT_EQ))
        {
            condition = GenCondition::Reverse(condition);
        }
    }

    GenTreeCC* cc = nullptr;

    // Next may be null if `node` is not used. In that case we don't need to generate a SETCC node.
    if (next != nullptr)
    {
        if (next->OperIs(GT_JTRUE))
        {
            // If the instruction immediately following 'relop', i.e. 'next' is a conditional branch,
            // it should always have 'relop' as its 'op1'. If it doesn't, then we have improperly
            // constructed IL (the setting of a condition code should always immediately precede its
            // use, since the JIT doesn't track dataflow for condition codes). Still, if it happens
            // it's not our problem, it simply means that `node` is not used and can be removed.
            if (next->AsUnOp()->gtGetOp1() == relop)
            {
                assert(relop->OperIsCompare());

                next->ChangeOper(GT_JCC);
                cc              = next->AsCC();
                cc->gtCondition = condition;
            }
        }
        else
        {
            // If the node is used by something other than a JTRUE then we need to insert a
            // SETCC node to materialize the boolean value.
            LIR::Use use;

            if (BlockRange().TryGetUse(relop, &use))
            {
                cc = comp->gtNewCC(GT_SETCC, TYP_INT, condition);
                BlockRange().InsertAfter(node, cc);
                use.ReplaceWith(cc);
            }
        }
    }

    if (cc != nullptr)
    {
        node->gtFlags |= GTF_SET_FLAGS;
    }

    // Remove the chain of EQ/NE(x, 0) relop nodes, if any. Note that if a SETCC was
    // inserted after `node`, `first` still points to the node that was initially
    // after `node`.
    if (relop != node)
    {
        BlockRange().Remove(first, relop);
    }

    return cc;
}

// Lower "jmp <method>" tail call to insert PInvoke method epilog if required.
void Lowering::LowerJmpMethod(GenTree* jmp)
{
    assert(jmp->OperIs(GT_JMP));

    JITDUMP("lowering GT_JMP\n");
    DISPNODE(jmp);
    JITDUMP("============\n");

    // If PInvokes are in-lined, we have to remember to execute PInvoke method epilog anywhere that
    // a method returns.
    if (comp->compMethodRequiresPInvokeFrame())
    {
        InsertPInvokeMethodEpilog(comp->compCurBB DEBUGARG(jmp));
    }
}

// Lower GT_RETURN/GT_SWIFT_ERROR_RET node to insert PInvoke method epilog if required.
void Lowering::LowerRet(GenTreeOp* ret)
{
    assert(ret->OperIs(GT_RETURN, GT_SWIFT_ERROR_RET));

    JITDUMP("lowering return node\n");
    DISPNODE(ret);
    JITDUMP("============\n");

    GenTree* retVal = ret->GetReturnValue();
    // There are two kinds of retyping:
    // - A simple bitcast can be inserted when:
    //   - We're returning a floating type as an integral type or vice-versa, or
    // - If we're returning a struct as a primitive type, we change the type of
    // 'retval' in 'LowerRetStructLclVar()'
    bool needBitcast        = !ret->TypeIs(TYP_VOID) && !varTypeUsesSameRegType(ret, retVal);
    bool doPrimitiveBitcast = false;
    if (needBitcast)
    {
        doPrimitiveBitcast = (!varTypeIsStruct(ret) && !varTypeIsStruct(retVal));
    }

    if (doPrimitiveBitcast)
    {
// Add a simple bitcast when both types are not structs.
// If one type is a struct it will be handled below.
#if defined(DEBUG)
        assert(!varTypeIsStruct(ret) && !varTypeIsStruct(retVal));
#endif

        GenTreeUnOp* bitcast = comp->gtNewBitCastNode(ret->TypeGet(), retVal);
        ret->SetReturnValue(bitcast);
        BlockRange().InsertBefore(ret, bitcast);
        ContainCheckBitCast(bitcast);
    }
    else if (!ret->TypeIs(TYP_VOID))
    {
#if FEATURE_MULTIREG_RET
        if (comp->compMethodReturnsMultiRegRetType() && retVal->OperIs(GT_LCL_VAR))
        {
            CheckMultiRegLclVar(retVal->AsLclVar(), comp->compRetTypeDesc.GetReturnRegCount());
        }
#endif // FEATURE_MULTIREG_RET
#ifdef DEBUG
        if (varTypeIsStruct(ret->TypeGet()) != varTypeIsStruct(retVal->TypeGet()))
        {
            if (varTypeIsStruct(ret->TypeGet()))
            {
                assert(comp->info.compRetNativeType != TYP_STRUCT);

                var_types retActualType    = genActualType(comp->info.compRetNativeType);
                var_types retValActualType = genActualType(retVal->TypeGet());

                bool constStructInit                  = retVal->IsConstInitVal();
                bool implicitCastFromSameOrBiggerSize = (genTypeSize(retActualType) <= genTypeSize(retValActualType));

                // This could happen if we have retyped op1 as a primitive type during struct promotion.
                bool actualTypesMatch = (retActualType == retValActualType);

                assert(actualTypesMatch || constStructInit || implicitCastFromSameOrBiggerSize);
            }
        }
#endif // DEBUG

        if (retVal->OperIsFieldList())
        {
            LowerRetFieldList(ret, retVal->AsFieldList());
        }
        else if (varTypeIsStruct(ret))
        {
            LowerRetStruct(ret);
        }
        else if (!ret->TypeIs(TYP_VOID) && varTypeIsStruct(retVal))
        {
            // Return struct as a primitive using Unsafe cast.
            assert(retVal->OperIs(GT_LCL_VAR));
            LowerRetSingleRegStructLclVar(ret);
        }
    }

    if (comp->compMethodRequiresPInvokeFrame())
    {
        InsertPInvokeMethodEpilog(comp->compCurBB DEBUGARG(ret));
    }
    ContainCheckRet(ret);
}

struct LowerFieldListRegisterInfo
{
    unsigned  Offset;
    var_types RegType;

    LowerFieldListRegisterInfo(unsigned offset, var_types regType)
        : Offset(offset)
        , RegType(regType)
    {
    }
};

//----------------------------------------------------------------------------------------------
// LowerRetFieldList:
//   Lower a returned FIELD_LIST node.
//
// Arguments:
//     ret       - The return node
//     fieldList - The field list
//
void Lowering::LowerRetFieldList(GenTreeOp* ret, GenTreeFieldList* fieldList)
{
    const ReturnTypeDesc& retDesc = comp->compRetTypeDesc;
    unsigned              numRegs = retDesc.GetReturnRegCount();

    auto getRegInfo = [=, &retDesc](unsigned regIndex) {
        unsigned  offset  = retDesc.GetReturnFieldOffset(regIndex);
        var_types regType = genActualType(retDesc.GetReturnRegType(regIndex));
        return LowerFieldListRegisterInfo(offset, regType);
    };

    bool isCompatible = IsFieldListCompatibleWithRegisters(fieldList, numRegs, getRegInfo);
    if (!isCompatible)
    {
        unsigned lclNum =
            StoreFieldListToNewLocal(comp->typGetObjLayout(comp->info.compMethodInfo->args.retTypeClass), fieldList);
        LclVarDsc* varDsc = comp->lvaGetDesc(lclNum);

        GenTree* retValue = comp->gtNewLclvNode(lclNum, varDsc->TypeGet());
        ret->SetReturnValue(retValue);
        BlockRange().InsertBefore(ret, retValue);
        LowerNode(retValue);

        BlockRange().Remove(fieldList);

        if (numRegs == 1)
        {
            var_types nativeReturnType = comp->info.compRetNativeType;
            ret->ChangeType(genActualType(nativeReturnType));
            LowerRetSingleRegStructLclVar(ret);
        }
        else
        {
            varDsc->lvIsMultiRegRet = true;
        }

        return;
    }

    LowerFieldListToFieldListOfRegisters(fieldList, numRegs, getRegInfo);
}

//----------------------------------------------------------------------------------------------
// StoreFieldListToNewLocal:
//   Create a new local with the specified layout and store the specified
//   fields of the specified FIELD_LIST into it.
//
// Arguments:
//   layout    - Layout of the new local
//   fieldList - Fields to store to it
//
// Returns:
//   Var number of new local.
//
unsigned Lowering::StoreFieldListToNewLocal(ClassLayout* layout, GenTreeFieldList* fieldList)
{
    JITDUMP("Spilling field list [%06u] to stack\n", Compiler::dspTreeID(fieldList));
    unsigned   lclNum = comp->lvaGrabTemp(true DEBUGARG("Spilled local for field list"));
    LclVarDsc* varDsc = comp->lvaGetDesc(lclNum);
    comp->lvaSetStruct(lclNum, layout, false);
    comp->lvaSetVarDoNotEnregister(lclNum DEBUGARG(DoNotEnregisterReason::LocalField));

    for (GenTreeFieldList::Use& use : fieldList->Uses())
    {
        GenTree* store = comp->gtNewStoreLclFldNode(lclNum, use.GetType(), use.GetOffset(), use.GetNode());
        BlockRange().InsertAfter(use.GetNode(), store);
        LowerNode(store);
    }

    return lclNum;
}

//----------------------------------------------------------------------------------------------
// LowerArgFieldList:
//   Lower an argument FIELD_LIST node.
//
// Arguments:
//   arg       - The argument
//   fieldList - The FIELD_LIST node
//
void Lowering::LowerArgFieldList(CallArg* arg, GenTreeFieldList* fieldList)
{
    assert(!arg->AbiInfo.HasAnyStackSegment());

    auto getRegInfo = [=](unsigned regIndex) {
        const ABIPassingSegment& seg = arg->AbiInfo.Segment(regIndex);
        return LowerFieldListRegisterInfo(seg.Offset, seg.GetRegisterType());
    };

    bool isCompatible = IsFieldListCompatibleWithRegisters(fieldList, arg->AbiInfo.NumSegments, getRegInfo);
    if (!isCompatible)
    {
        ClassLayout* layout = comp->typGetObjLayout(arg->GetSignatureClassHandle());
        unsigned     lclNum = StoreFieldListToNewLocal(layout, fieldList);
        fieldList->Uses().Clear();
        for (const ABIPassingSegment& seg : arg->AbiInfo.Segments())
        {
            GenTreeLclFld* fld = comp->gtNewLclFldNode(lclNum, seg.GetRegisterType(layout), seg.Offset);
            fieldList->AddFieldLIR(comp, fld, seg.Offset, fld->TypeGet());
            BlockRange().InsertBefore(fieldList, fld);
        }
    }
    else
    {
        LowerFieldListToFieldListOfRegisters(fieldList, arg->AbiInfo.NumSegments, getRegInfo);
    }

    GenTreeFieldList::Use* field = fieldList->Uses().GetHead();
    for (const ABIPassingSegment& seg : arg->AbiInfo.Segments())
    {
        assert((field != nullptr) && "Ran out of fields while inserting PUTARG_REG");
        InsertPutArgReg(&field->NodeRef(), seg);
        field = field->GetNext();
    }

    assert((field == nullptr) && "Missed fields while inserting PUTARG_REG");

    arg->NodeRef() = fieldList->SoleFieldOrThis();
    if (arg->GetNode() != fieldList)
    {
        BlockRange().Remove(fieldList);
    }
}

//----------------------------------------------------------------------------------------------
// IsFieldListCompatibleWithReturn:
//   Check if the fields of a FIELD_LIST are compatible with the registers
//   being returned.
//
// Arguments:
//   fieldList - The FIELD_LIST node
//
// Returns:
//   True if the fields of the FIELD_LIST are all direct insertions into the
//   return registers.
//
template <typename GetRegisterInfoFunc>
bool Lowering::IsFieldListCompatibleWithRegisters(GenTreeFieldList*   fieldList,
                                                  unsigned            numRegs,
                                                  GetRegisterInfoFunc getRegInfo)
{
    JITDUMP("Checking if field list [%06u] is compatible with registers: ", Compiler::dspTreeID(fieldList));

    GenTreeFieldList::Use* use = fieldList->Uses().GetHead();
    for (unsigned i = 0; i < numRegs; i++)
    {
        LowerFieldListRegisterInfo regInfo  = getRegInfo(i);
        unsigned                   regStart = regInfo.Offset;
        var_types                  regType  = regInfo.RegType;
        unsigned                   regEnd   = regStart + genTypeSize(regType);

        if ((i == numRegs - 1) && !varTypeUsesFloatReg(regType))
        {
            // Allow tail end to pass undefined bits into the register
            regEnd = regStart + REGSIZE_BYTES;
        }

        // TODO-CQ: Could just create a 0 for this.
        if ((use == nullptr) || (use->GetOffset() >= regEnd))
        {
            JITDUMP("it is not; register %u has no corresponding field\n", i);
            return false;
        }

        do
        {
            unsigned fieldStart = use->GetOffset();

            if (fieldStart < regStart)
            {
                // Not fully contained in a register.
                // TODO-CQ: Could just remove these fields if they don't partially overlap with the next register.
                JITDUMP("it is not; field [%06u] starts before register %u\n", Compiler::dspTreeID(use->GetNode()), i);
                return false;
            }

            if (fieldStart >= regEnd)
            {
                break;
            }

            unsigned fieldEnd = fieldStart + genTypeSize(use->GetType());
            if (fieldEnd > regEnd)
            {
                JITDUMP("it is not; field [%06u] ends after register %u\n", Compiler::dspTreeID(use->GetNode()), i);
                return false;
            }

            // float -> float insertions are not yet supported
            if (varTypeUsesFloatReg(use->GetNode()) && varTypeUsesFloatReg(regType) && (fieldStart != regStart))
            {
                JITDUMP("it is not; field [%06u] requires an insertion into register %u\n",
                        Compiler::dspTreeID(use->GetNode()), i);
                return false;
            }

            use = use->GetNext();
        } while (use != nullptr);
    }

    if (use != nullptr)
    {
        // TODO-CQ: Could just remove these fields.
        JITDUMP("it is not; field [%06u] corresponds to no register\n", Compiler::dspTreeID(use->GetNode()));
        return false;
    }

    JITDUMP("it is\n");
    return true;
}

//----------------------------------------------------------------------------------------------
// LowerFieldListToFieldListOfRegisters:
//   Lower the specified field list into one that is compatible with the return
//   registers.
//
// Arguments:
//     fieldList - The field list
//
template <typename GetRegisterInfoFunc>
void Lowering::LowerFieldListToFieldListOfRegisters(GenTreeFieldList*   fieldList,
                                                    unsigned            numRegs,
                                                    GetRegisterInfoFunc getRegInfo)
{
    GenTreeFieldList::Use* use = fieldList->Uses().GetHead();
    assert(fieldList->Uses().IsSorted());

    for (unsigned i = 0; i < numRegs; i++)
    {
        LowerFieldListRegisterInfo regInfo  = getRegInfo(i);
        unsigned                   regStart = regInfo.Offset;
        var_types                  regType  = regInfo.RegType;
        unsigned                   regEnd   = regStart + genTypeSize(regType);

        if ((i == numRegs - 1) && !varTypeUsesFloatReg(regType))
        {
            // Allow tail end to pass undefined bits into the register
            regEnd = regStart + REGSIZE_BYTES;
        }

        GenTreeFieldList::Use* regEntry = use;

        assert(use != nullptr);

        GenTree* fieldListPrev = fieldList->gtPrev;

        do
        {
            unsigned fieldStart = use->GetOffset();

            assert(fieldStart >= regStart);

            if (fieldStart >= regEnd)
            {
                break;
            }

            var_types fieldType = use->GetType();
            GenTree*  value     = use->GetNode();

            unsigned               insertOffset = fieldStart - regStart;
            GenTreeFieldList::Use* nextUse      = use->GetNext();

            // First ensure the value does not have upper bits set that
            // interfere with the next field.
            if ((nextUse != nullptr) && (nextUse->GetOffset() < regEnd) &&
                (fieldStart + genTypeSize(genActualType(fieldType)) > nextUse->GetOffset()))
            {
                assert(varTypeIsSmall(fieldType));
                // This value may interfere with the next field. Ensure that doesn't happen.
                if (comp->fgCastNeeded(value, varTypeToUnsigned(fieldType)))
                {
                    value = comp->gtNewCastNode(TYP_INT, value, true, varTypeToUnsigned(fieldType));
                    BlockRange().InsertBefore(fieldList, value);
                }
            }

            // If this is a float -> int insertion, then we need the bitcast now.
            if (varTypeUsesFloatReg(value) && varTypeUsesIntReg(regInfo.RegType))
            {
                assert((genTypeSize(value) == 4) || (genTypeSize(value) == 8));
                var_types castType = genTypeSize(value) == 4 ? TYP_INT : TYP_LONG;
                value              = comp->gtNewBitCastNode(castType, value);
                BlockRange().InsertBefore(fieldList, value);
            }

            if (insertOffset + genTypeSize(fieldType) > genTypeSize(genActualType(value)))
            {
                value = comp->gtNewCastNode(TYP_LONG, value, true, TYP_LONG);
                BlockRange().InsertBefore(fieldList, value);
            }

            if (fieldStart != regStart)
            {
                GenTree* shiftAmount = comp->gtNewIconNode((ssize_t)insertOffset * BITS_PER_BYTE);
                value                = comp->gtNewOperNode(GT_LSH, genActualType(value), value, shiftAmount);
                BlockRange().InsertBefore(fieldList, shiftAmount, value);
            }

            if (regEntry != use)
            {
                GenTree* prevValue = regEntry->GetNode();
                if (genActualType(value) != genActualType(regEntry->GetNode()))
                {
                    prevValue = comp->gtNewCastNode(TYP_LONG, prevValue, true, TYP_LONG);
                    BlockRange().InsertBefore(fieldList, prevValue);
                    regEntry->SetNode(prevValue);
                }

                value = comp->gtNewOperNode(GT_OR, genActualType(value), prevValue, value);
                BlockRange().InsertBefore(fieldList, value);

                // Remove this field from the FIELD_LIST.
                regEntry->SetNext(use->GetNext());
            }

            regEntry->SetNode(value);
            regEntry->SetType(genActualType(value));
            use = regEntry->GetNext();
        } while (use != nullptr);

        assert(regEntry != nullptr);
        if (varTypeUsesIntReg(regEntry->GetNode()) != varTypeUsesIntReg(regType))
        {
            GenTree* bitCast = comp->gtNewBitCastNode(regType, regEntry->GetNode());
            BlockRange().InsertBefore(fieldList, bitCast);
            regEntry->SetNode(bitCast);
        }

        if (fieldListPrev->gtNext != fieldList)
        {
            LowerRange(fieldListPrev->gtNext, fieldList->gtPrev);
        }
    }

    assert(use == nullptr);
}

//----------------------------------------------------------------------------------------------
// LowerStoreLocCommon: platform independent part of local var or field store lowering.
//
// Arguments:
//     lclStore - The store lcl node to lower.
//
// Returns:
//   Next node to lower.
//
GenTree* Lowering::LowerStoreLocCommon(GenTreeLclVarCommon* lclStore)
{
    assert(lclStore->OperIs(GT_STORE_LCL_FLD, GT_STORE_LCL_VAR));
    JITDUMP("lowering store lcl var/field (before):\n");
    DISPTREERANGE(BlockRange(), lclStore);
    JITDUMP("\n");

    TryRetypingFloatingPointStoreToIntegerStore(lclStore);

    GenTree*   src           = lclStore->gtGetOp1();
    LclVarDsc* varDsc        = comp->lvaGetDesc(lclStore);
    const bool srcIsMultiReg = src->IsMultiRegNode();

    if (!srcIsMultiReg && varTypeIsStruct(varDsc))
    {
        // TODO-Cleanup: we want to check `varDsc->lvRegStruct` as the last condition instead of `!varDsc->lvPromoted`,
        // but we do not set it for `CSE` vars so it is currently failing.
        assert(varDsc->CanBeReplacedWithItsField(comp) || varDsc->lvDoNotEnregister || !varDsc->lvPromoted);
        if (varDsc->CanBeReplacedWithItsField(comp))
        {
            assert(varDsc->lvFieldCnt == 1);
            unsigned   fldNum = varDsc->lvFieldLclStart;
            LclVarDsc* fldDsc = comp->lvaGetDesc(fldNum);

            JITDUMP("Replacing an independently promoted local var V%02u with its only field V%02u for the store "
                    "from a call [%06u]\n",
                    lclStore->GetLclNum(), fldNum, comp->dspTreeID(lclStore));
            lclStore->SetLclNum(fldNum);
            lclStore->ChangeType(fldDsc->TypeGet());
            varDsc = fldDsc;
        }
    }

    if (srcIsMultiReg)
    {
        CheckMultiRegLclVar(lclStore->AsLclVar(), src->GetMultiRegCount(comp));
    }

    const var_types lclRegType = varDsc->GetRegisterType(lclStore);

    if (lclStore->TypeIs(TYP_STRUCT) && !srcIsMultiReg)
    {
        bool convertToStoreObj;
        if (lclStore->OperIs(GT_STORE_LCL_FLD))
        {
            convertToStoreObj = true;
        }
        else if (src->OperIs(GT_CALL))
        {
            GenTreeCall* call = src->AsCall();

#ifdef DEBUG
            const ClassLayout* layout    = lclStore->GetLayout(comp);
            const unsigned     slotCount = layout->GetSlotCount();
#if defined(TARGET_XARCH) && !defined(UNIX_AMD64_ABI)
            // Windows x64 doesn't have multireg returns,
            // x86 uses it only for long return type, not for structs.
            assert(slotCount == 1);
            assert(lclRegType != TYP_UNDEF);
#else // !TARGET_XARCH || UNIX_AMD64_ABI
            if (!comp->IsHfa(layout->GetClassHandle()))
            {
                if (slotCount > 1)
                {
#if !defined(TARGET_RISCV64) && !defined(TARGET_LOONGARCH64)
                    assert(call->HasMultiRegRetVal());
#endif
                }
                else
                {
                    unsigned size = layout->GetSize();
                    assert((size <= 8) || (size == 16));
                    bool isPowerOf2    = (((size - 1) & size) == 0);
                    bool isTypeDefined = (lclRegType != TYP_UNDEF);
                    assert(isPowerOf2 == isTypeDefined);
                }
            }
#endif // !TARGET_XARCH || UNIX_AMD64_ABI
#endif // DEBUG

#if !defined(WINDOWS_AMD64_ABI)
            if (!call->HasMultiRegRetVal() && (lclRegType == TYP_UNDEF))
            {
                // If we have a single return register,
                // but we can't retype it as a primitive type, we must spill it.
                GenTreeLclVar* spilledCall = SpillStructCallResult(call);
                lclStore->gtOp1            = spilledCall;
                src                        = lclStore->gtOp1;
                JITDUMP("lowering store lcl var/field has to spill call src.\n");
                return LowerStoreLocCommon(lclStore);
            }
#endif // !WINDOWS_AMD64_ABI
            convertToStoreObj = false;
        }
        else if (!varDsc->IsEnregisterableType())
        {
            convertToStoreObj = true;
        }
        else if (src->OperIs(GT_CNS_INT))
        {
            assert(src->IsIntegralConst(0) && "expected an INIT_VAL for non-zero init.");

#ifdef FEATURE_SIMD
            if (varTypeIsSIMD(lclRegType))
            {
                GenTree* zeroCon = comp->gtNewZeroConNode(lclRegType);

                BlockRange().InsertAfter(src, zeroCon);
                BlockRange().Remove(src);

                src             = zeroCon;
                lclStore->gtOp1 = src;
            }
#endif // FEATURE_SIMD

            convertToStoreObj = false;
        }
        else if (src->OperIs(GT_LCL_VAR))
        {
            convertToStoreObj = false;
        }
        else if (src->OperIs(GT_IND, GT_BLK, GT_LCL_FLD))
        {
#if !defined(TARGET_ARM64)

            if (src->TypeIs(TYP_STRUCT))
            {
                src->ChangeType(lclRegType);
                if (src->OperIs(GT_IND, GT_BLK))
                {
                    if (src->OperIs(GT_BLK))
                    {
                        src->SetOper(GT_IND);
                    }
                    // This logic is skipped for struct indir in
                    // `Lowering::LowerIndir` because we don't know the size.
                    // Do it now.
                    LowerIndir(src->AsIndir());
                }
#if defined(TARGET_XARCH)
                if (varTypeIsSmall(lclRegType))
                {
                    src->SetDontExtend();
                }
#endif // TARGET_XARCH
            }
            convertToStoreObj = false;
#else  // TARGET_ARM64
       // This optimization on arm64 allows more SIMD16 vars to be enregistered but it could cause
       // regressions when there are many calls and before/after each one we have to store/save the upper
       // half of these registers. So enable this for arm64 only when LSRA is taught not to allocate registers when
       // it would have to spilled too many times.
            convertToStoreObj = true;
#endif // TARGET_ARM64
        }
        else
        {
            assert(src->OperIsInitVal());
            convertToStoreObj = true;
        }

        if (convertToStoreObj)
        {
            ClassLayout*   layout = lclStore->GetLayout(comp);
            const unsigned lclNum = lclStore->GetLclNum();
            GenTreeLclFld* addr   = comp->gtNewLclAddrNode(lclNum, lclStore->GetLclOffs(), TYP_BYREF);
            comp->lvaSetVarDoNotEnregister(lclNum DEBUGARG(DoNotEnregisterReason::BlockOp));

            addr->gtFlags |= lclStore->gtFlags & (GTF_VAR_DEF | GTF_VAR_USEASG);

            lclStore->ChangeOper(GT_STORE_BLK);
            GenTreeBlk* objStore = lclStore->AsBlk();
            objStore->gtFlags    = GTF_ASG | GTF_IND_NONFAULTING | GTF_IND_TGT_NOT_HEAP;
            objStore->Initialize(layout);
            objStore->SetAddr(addr);
            objStore->SetData(src);

            BlockRange().InsertBefore(objStore, addr);
            LowerNode(objStore);

            JITDUMP("lowering store lcl var/field (after):\n");
            DISPTREERANGE(BlockRange(), objStore);
            JITDUMP("\n");

            return objStore->gtNext;
        }
    }

    // src and dst can be in registers, check if we need a bitcast.
    if (!src->TypeIs(TYP_STRUCT) && !varTypeUsesSameRegType(lclRegType, src))
    {
        assert(!srcIsMultiReg);
        assert(lclStore->OperIsLocalStore());
        assert(lclRegType != TYP_UNDEF);

        GenTreeUnOp* bitcast = comp->gtNewBitCastNode(lclRegType, src);
        lclStore->gtOp1      = bitcast;
        src                  = lclStore->gtGetOp1();
        BlockRange().InsertBefore(lclStore, bitcast);
        ContainCheckBitCast(bitcast);
    }

    GenTree* next = LowerStoreLoc(lclStore);

    JITDUMP("lowering store lcl var/field (after):\n");
    DISPTREERANGE(BlockRange(), lclStore);
    JITDUMP("\n");

    return next;
}

//----------------------------------------------------------------------------------------------
// LowerRetStructLclVar: Lowers a struct return node.
//
// Arguments:
//     node - The return node to lower.
//
void Lowering::LowerRetStruct(GenTreeUnOp* ret)
{
#ifdef TARGET_ARM64
    if (GlobalJitOptions::compFeatureHfa)
    {
        if (varTypeIsSIMD(ret))
        {
            if (comp->info.compRetNativeType == TYP_STRUCT)
            {
                assert(varTypeIsSIMD(ret->gtGetOp1()));
                assert(comp->compMethodReturnsMultiRegRetType());
                ret->ChangeType(comp->info.compRetNativeType);
            }
            else
            {
                assert(comp->info.compRetNativeType == ret->TypeGet());
            }
        }
    }
#endif // TARGET_ARM64

    if (comp->compMethodReturnsMultiRegRetType())
    {
        return;
    }

    assert(ret->OperIs(GT_RETURN, GT_SWIFT_ERROR_RET));
    assert(varTypeIsStruct(ret));

    GenTree*  retVal           = ret->gtGetOp1();
    var_types nativeReturnType = comp->info.compRetNativeType;
    // Note: small types are returned as INT.
    ret->ChangeType(genActualType(nativeReturnType));

    switch (retVal->OperGet())
    {
        case GT_CNS_INT:
        {
            // When we promote LCL_VAR single fields into return, we could have all types of constants here.
            if (varTypeUsesFloatReg(nativeReturnType))
            {
                // ZeroObj assertion propagation can create INT zeros for DOUBLE returns.
                assert((genTypeSize(retVal) == genTypeSize(nativeReturnType)) || retVal->IsIntegralConst(0));
                int64_t value = retVal->AsIntCon()->IconValue();

                if (nativeReturnType == TYP_FLOAT)
                {
                    retVal->BashToConst(*reinterpret_cast<float*>(&value));
                }
                else
                {
                    retVal->BashToConst(*reinterpret_cast<double*>(&value));
                }
            }
            else
            {
                assert(varTypeUsesIntReg(nativeReturnType));
            }
            break;
        }

        case GT_BLK:
        case GT_IND:
        {
            // Spill to a local if sizes don't match so we can avoid the "load more than requested"
            // problem, e.g. struct size is 5 and we emit "ldr x0, [x1]"
            if (genTypeSize(nativeReturnType) > retVal->AsIndir()->Size())
            {
                LIR::Use retValUse(BlockRange(), &ret->gtOp1, ret);
                unsigned tmpNum = comp->lvaGrabTemp(true DEBUGARG("mis-sized struct return"));
                comp->lvaSetStruct(tmpNum, comp->info.compMethodInfo->args.retTypeClass, false);

                ReplaceWithLclVar(retValUse, tmpNum);
                LowerRetSingleRegStructLclVar(ret);
                break;
            }

            retVal->ChangeOper(GT_IND);
            retVal->ChangeType(nativeReturnType);
            LowerIndir(retVal->AsIndir());
            break;
        }

        case GT_LCL_VAR:
            LowerRetSingleRegStructLclVar(ret);
            break;

        case GT_LCL_FLD:
            retVal->ChangeType(nativeReturnType);
            break;

        default:
            assert(varTypeIsEnregisterable(retVal));
            if (!varTypeUsesSameRegType(ret, retVal))
            {
                GenTreeUnOp* bitcast = comp->gtNewBitCastNode(ret->TypeGet(), retVal);
                ret->gtOp1           = bitcast;
                BlockRange().InsertBefore(ret, bitcast);
                ContainCheckBitCast(bitcast);
            }
            break;
    }
}

//----------------------------------------------------------------------------------------------
// LowerRetSingleRegStructLclVar: Lowers a return node with a struct lclVar as a source.
//
// Arguments:
//    node - The return node to lower.
//
// Notes:
//    - the function is only for LclVars that are returned in one register;
//    - if LclVar is allocated in memory then read it as return type;
//    - if LclVar can be enregistered read it as register type and add a bitcast if necessary;
//
void Lowering::LowerRetSingleRegStructLclVar(GenTreeUnOp* ret)
{
    assert(!comp->compMethodReturnsMultiRegRetType());
    assert(ret->OperIs(GT_RETURN, GT_SWIFT_ERROR_RET));
    GenTreeLclVarCommon* lclVar = ret->AsOp()->GetReturnValue()->AsLclVar();
    assert(lclVar->OperIs(GT_LCL_VAR));
    unsigned   lclNum = lclVar->GetLclNum();
    LclVarDsc* varDsc = comp->lvaGetDesc(lclNum);

    if (varDsc->lvPromoted)
    {
        // TODO-1stClassStructs: We can no longer independently promote
        // or enregister this struct, since it is referenced as a whole.
        comp->lvaSetVarDoNotEnregister(lclNum DEBUGARG(DoNotEnregisterReason::BlockOpRet));
    }

    if (varDsc->lvDoNotEnregister)
    {
        lclVar->ChangeOper(GT_LCL_FLD);

        // We are returning as a primitive type and the lcl is of struct type.
        assert(comp->info.compRetNativeType != TYP_STRUCT);
        assert((genTypeSize(comp->info.compRetNativeType) == genTypeSize(ret)) ||
               (varTypeIsIntegral(ret) && varTypeIsIntegral(comp->info.compRetNativeType) &&
                (genTypeSize(comp->info.compRetNativeType) <= genTypeSize(ret))));
        // If the actual return type requires normalization, then make sure we
        // do so by using the correct small type for the GT_LCL_FLD. It would
        // be conservative to check just compRetNativeType for this since small
        // structs are normalized to primitive types when they are returned in
        // registers, so we would normalize for them as well.
        if (varTypeIsSmall(comp->info.compRetType))
        {
            assert(genTypeSize(comp->info.compRetNativeType) == genTypeSize(comp->info.compRetType));
            lclVar->ChangeType(comp->info.compRetType);
        }
        else
        {
            // Otherwise we don't mind that we leave the upper bits undefined.
            lclVar->ChangeType(ret->TypeGet());
        }
        lclVar->AsLclFld()->SetLclOffs(comp->compRetTypeDesc.GetSingleReturnFieldOffset());
    }
    else
    {
        const var_types lclVarType = varDsc->GetRegisterType(lclVar);
        assert(lclVarType != TYP_UNDEF);

        const var_types actualType = genActualType(lclVarType);
        lclVar->ChangeType(actualType);

        if (!varTypeUsesSameRegType(ret, lclVarType))
        {
            GenTreeUnOp* bitcast = comp->gtNewBitCastNode(ret->TypeGet(), ret->gtOp1);
            ret->AsOp()->SetReturnValue(bitcast);
            BlockRange().InsertBefore(ret, bitcast);
            ContainCheckBitCast(bitcast);
        }
    }
}

//----------------------------------------------------------------------------------------------
// LowerAsyncContinuation: Lower a GT_ASYNC_CONTINUATION node
//
// Arguments:
//   asyncCont - Async continuation node
//
// Returns:
//   Next node to lower.
//
GenTree* Lowering::LowerAsyncContinuation(GenTree* asyncCont)
{
    assert(asyncCont->OperIs(GT_ASYNC_CONTINUATION));

    GenTree* next = asyncCont->gtNext;

    //
    // ASYNC_CONTINUATION is created from two sources:
    //
    // 1. The async resumption stubs are IL stubs created by the VM. These call
    // runtime async functions via "calli", passing the continuation manually.
    // They use the AsyncHelpers.AsyncCallContinuation intrinsic after the
    // calli, which turns into the ASYNC_CONTINUATION node during import.
    //
    // 2. In the async transformation, ASYNC_CONTINUATION nodes are inserted
    // after calls to async calls.
    //
    // In the former case nothing has marked the previous call as an "async"
    // method. We need to do that here to ensure that the backend knows that
    // the call has a non-standard calling convention that returns an
    // additional GC ref. This requires additional GC tracking that we would
    // otherwise not get.
    //
    GenTree* node = asyncCont;
    while (true)
    {
        node = node->gtPrev;
        noway_assert((node != nullptr) && "Ran out of nodes while looking for call before async continuation");

        if (node->IsCall())
        {
            if (!node->AsCall()->IsAsync())
            {
                JITDUMP("Marking the call [%06u] before async continuation [%06u] as an async call\n",
                        Compiler::dspTreeID(node), Compiler::dspTreeID(asyncCont));
                node->AsCall()->SetIsAsync(new (comp, CMK_Async) AsyncCallInfo);
            }

            BlockRange().Remove(asyncCont);
            BlockRange().InsertAfter(node, asyncCont);
            break;
        }
    }

    return next;
}

//----------------------------------------------------------------------------------------------
// LowerReturnSuspend:
//   Lower a GT_RETURN_SUSPEND by making it a terminator node.
//
// Arguments:
//   node - The node
//
void Lowering::LowerReturnSuspend(GenTree* node)
{
    assert(node->OperIs(GT_RETURN_SUSPEND));
    while (BlockRange().LastNode() != node)
    {
        BlockRange().Remove(BlockRange().LastNode(), true);
    }

    if (comp->compMethodRequiresPInvokeFrame())
    {
        InsertPInvokeMethodEpilog(comp->compCurBB DEBUGARG(node));
    }
}

//----------------------------------------------------------------------------------------------
// LowerCallStruct: Lowers a call node that returns a struct.
//
// Arguments:
//     call - The call node to lower.
//
// Notes:
//    - this handles only single-register returns;
//    - it transforms the call's user for `GT_STOREIND`.
//
void Lowering::LowerCallStruct(GenTreeCall* call)
{
    assert(varTypeIsStruct(call));
    if (call->HasMultiRegRetVal())
    {
        return;
    }

    if (GlobalJitOptions::compFeatureHfa)
    {
        if (comp->IsHfa(call->gtRetClsHnd))
        {
#if defined(TARGET_ARM64)
            assert(comp->GetHfaCount(call->gtRetClsHnd) == 1);
#elif defined(TARGET_ARM)
            // ARM returns double in 2 float registers, but
            // `call->HasMultiRegRetVal()` count double registers.
            assert(comp->GetHfaCount(call->gtRetClsHnd) <= 2);
#else  // !TARGET_ARM64 && !TARGET_ARM
            NYI("Unknown architecture");
#endif // !TARGET_ARM64 && !TARGET_ARM
            var_types hfaType = comp->GetHfaType(call->gtRetClsHnd);
            if (call->TypeIs(hfaType))
            {
                return;
            }
        }
    }

    CORINFO_CLASS_HANDLE        retClsHnd = call->gtRetClsHnd;
    Compiler::structPassingKind howToReturnStruct;
    var_types returnType = comp->getReturnTypeForStruct(retClsHnd, call->GetUnmanagedCallConv(), &howToReturnStruct);
    assert(returnType != TYP_STRUCT && returnType != TYP_UNKNOWN);
    var_types origType = call->TypeGet();
    call->gtType       = genActualType(returnType);

    LIR::Use callUse;
    if (BlockRange().TryGetUse(call, &callUse))
    {
        GenTree* user = callUse.User();
        switch (user->OperGet())
        {
            case GT_RETURN:
            case GT_STORE_LCL_VAR:
            case GT_STORE_BLK:
                // Leave as is, the user will handle it.
                assert(user->TypeIs(origType) || varTypeIsSIMD(user->TypeGet()));
                break;

            case GT_STORE_LCL_FLD:
                // The call's type should match the user's type or struct's returnType.
                // We leave handling the former case to user's lowering.
                assert(user->TypeIs(origType) || (returnType == user->TypeGet()));
                break;

            case GT_CALL:
            case GT_FIELD_LIST:
                // Argument/return lowering will deal with register file mismatches if needed.
                assert(varTypeIsSIMD(origType));
                break;

            case GT_STOREIND:
#ifdef FEATURE_SIMD
                if (varTypeIsSIMD(user))
                {
                    user->ChangeType(returnType);
                    break;
                }
#endif // FEATURE_SIMD
       // importer has a separate mechanism to retype calls to helpers,
       // keep it for now.
                assert(user->TypeIs(TYP_REF) || (user->TypeIs(TYP_I_IMPL) && comp->IsTargetAbi(CORINFO_NATIVEAOT_ABI)));
                assert(call->IsHelperCall());
                assert(returnType == user->TypeGet());
                break;

#ifdef FEATURE_HW_INTRINSICS
            case GT_HWINTRINSIC:
            {
                if (!varTypeUsesSameRegType(returnType, origType))
                {
                    GenTreeUnOp* bitCast = comp->gtNewBitCastNode(origType, call);
                    BlockRange().InsertAfter(call, bitCast);
                    callUse.ReplaceWith(bitCast);
                    ContainCheckBitCast(bitCast);
                }
                break;
            }
#endif // FEATURE_HW_INTRINSICS

            default:
                unreached();
        }
    }
}

//----------------------------------------------------------------------------------------------
// LowerStoreSingleRegCallStruct: Lowers a store block where the source is a struct typed call.
//
// Arguments:
//     store - The store node to lower.
//
// Notes:
//    - the function is only for calls that return one register;
//    - it spills the call's result if it can be retyped as a primitive type;
//
void Lowering::LowerStoreSingleRegCallStruct(GenTreeBlk* store)
{
    assert(store->Data()->IsCall());
    GenTreeCall* call = store->Data()->AsCall();
    assert(!call->HasMultiRegRetVal());

    const ClassLayout* layout  = store->GetLayout();
    var_types          regType = layout->GetRegisterType();

    if (regType != TYP_UNDEF)
    {
#if defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
        if (varTypeIsFloating(call->TypeGet()))
        {
            regType = call->TypeGet();
        }
#endif
        store->ChangeType(regType);
        store->SetOper(GT_STOREIND);
        LowerStoreIndirCommon(store->AsStoreInd());
        return;
    }
    else
    {
#if defined(WINDOWS_AMD64_ABI)
        // All ABI except Windows x64 supports passing 3 byte structs in registers.
        // Other 64 bites ABI-s support passing 5, 6, 7 byte structs.
        unreached();
#else  // !WINDOWS_AMD64_ABI
        store->gtBlkOpKind         = GenTreeBlk::BlkOpKindUnroll;
        GenTreeLclVar* spilledCall = SpillStructCallResult(call);
        store->SetData(spilledCall);
        LowerBlockStoreCommon(store);
#endif // WINDOWS_AMD64_ABI
    }
}

#if !defined(WINDOWS_AMD64_ABI)
//----------------------------------------------------------------------------------------------
// SpillStructCallResult: Spill call result to memory.
//
// Arguments:
//     call - call with 3, 5, 6 or 7 return size that has to be spilled to memory.
//
// Return Value:
//    load of the spilled variable.
//
GenTreeLclVar* Lowering::SpillStructCallResult(GenTreeCall* call) const
{
    // TODO-1stClassStructs: we can support this in codegen for `GT_STORE_BLK` without new temps.
    const unsigned spillNum = comp->lvaGrabTemp(true DEBUGARG("Return value temp for an odd struct return size"));
    comp->lvaSetVarDoNotEnregister(spillNum DEBUGARG(DoNotEnregisterReason::LocalField));
    CORINFO_CLASS_HANDLE retClsHnd = call->gtRetClsHnd;
    comp->lvaSetStruct(spillNum, retClsHnd, false);
    unsigned       offset = call->GetReturnTypeDesc()->GetSingleReturnFieldOffset();
    GenTreeLclFld* spill  = comp->gtNewStoreLclFldNode(spillNum, call->TypeGet(), offset, call);

    BlockRange().InsertAfter(call, spill);
    ContainCheckStoreLoc(spill);
    GenTreeLclVar* loadCallResult = comp->gtNewLclvNode(spillNum, TYP_STRUCT)->AsLclVar();
    BlockRange().InsertAfter(spill, loadCallResult);
    return loadCallResult;
}
#endif // !WINDOWS_AMD64_ABI

GenTree* Lowering::LowerDirectCall(GenTreeCall* call)
{
    noway_assert(call->gtCallType == CT_USER_FUNC || call->IsHelperCall());

    // Non-virtual direct/indirect calls: Work out if the address of the
    // call is known at JIT time.  If not it is either an indirect call
    // or the address must be accessed via an single/double indirection.

    void*           addr;
    InfoAccessType  accessType;
    CorInfoHelpFunc helperNum = comp->eeGetHelperNum(call->gtCallMethHnd);

#ifdef FEATURE_READYTORUN
    if (call->gtEntryPoint.addr != nullptr)
    {
        accessType = call->gtEntryPoint.accessType;
        addr       = call->gtEntryPoint.addr;
    }
    else
#endif
        if (call->IsHelperCall())
    {
        noway_assert(helperNum != CORINFO_HELP_UNDEF);

        CORINFO_CONST_LOOKUP addrInfo = comp->compGetHelperFtn(helperNum);
        addr                          = addrInfo.addr;
        accessType                    = addrInfo.accessType;
    }
    else
    {
        noway_assert(helperNum == CORINFO_HELP_UNDEF);

        CORINFO_ACCESS_FLAGS aflags = CORINFO_ACCESS_ANY;

        if (call->IsSameThis())
        {
            aflags = (CORINFO_ACCESS_FLAGS)(aflags | CORINFO_ACCESS_THIS);
        }

        if (!call->NeedsNullCheck())
        {
            aflags = (CORINFO_ACCESS_FLAGS)(aflags | CORINFO_ACCESS_NONNULL);
        }

        CORINFO_CONST_LOOKUP addrInfo;
        comp->info.compCompHnd->getFunctionEntryPoint(call->gtCallMethHnd, &addrInfo, aflags);

        accessType = addrInfo.accessType;
        addr       = addrInfo.addr;
    }

    GenTree* result = nullptr;
    switch (accessType)
    {
        case IAT_VALUE:
            // Non-virtual direct call to known address.
            // For JIT helper based tailcall (only used on x86) the target
            // address is passed as an arg to the helper so we want a node for
            // it.
            if (!IsCallTargetInRange(addr) || call->IsTailCallViaJitHelper())
            {
                result = AddrGen(addr);
            }
            else
            {
                // a direct call within range of hardware relative call instruction
                // stash the address for codegen
                call->gtDirectCallAddress = addr;
            }
            break;

        case IAT_PVALUE:
        {
            // If we are using an indirection cell for a direct call then apply
            // an optimization that loads the call target directly from the
            // indirection cell, instead of duplicating the tree.
            bool hasIndirectionCell = call->GetIndirectionCellArgKind() != WellKnownArg::None;

            if (!hasIndirectionCell)
            {
                // Non-virtual direct calls to addresses accessed by
                // a single indirection.
                GenTree* cellAddr = AddrGen(addr);
#ifdef DEBUG
                cellAddr->AsIntCon()->gtTargetHandle = (size_t)call->gtCallMethHnd;
#endif
                GenTree* indir = Ind(cellAddr);
                result         = indir;
            }
            break;
        }

        case IAT_PPVALUE:
            // Non-virtual direct calls to addresses accessed by
            // a double indirection.
            //

            // Expanding an IAT_PPVALUE here, will lose the opportunity
            // to Hoist/CSE the first indirection as it is an invariant load
            //
            assert(!"IAT_PPVALUE case in LowerDirectCall");

            noway_assert(helperNum == CORINFO_HELP_UNDEF);
            result = AddrGen(addr);
            // Double-indirection. Load the address into a register
            // and call indirectly through the register
            //
            result = Ind(Ind(result));
            break;

        case IAT_RELPVALUE:
        {
            // Non-virtual direct calls to addresses accessed by
            // a single relative indirection.
            GenTree* cellAddr = AddrGen(addr);
            GenTree* indir    = Ind(cellAddr);
            result            = comp->gtNewOperNode(GT_ADD, TYP_I_IMPL, indir, AddrGen(addr));
            break;
        }

        default:
            noway_assert(!"Bad accessType");
            break;
    }

    return result;
}

GenTree* Lowering::LowerDelegateInvoke(GenTreeCall* call)
{
    noway_assert(call->gtCallType == CT_USER_FUNC);

    assert((comp->info.compCompHnd->getMethodAttribs(call->gtCallMethHnd) &
            (CORINFO_FLG_DELEGATE_INVOKE | CORINFO_FLG_FINAL)) == (CORINFO_FLG_DELEGATE_INVOKE | CORINFO_FLG_FINAL));

    GenTree* thisArgNode;
    if (call->IsTailCallViaJitHelper())
    {
        thisArgNode = call->gtArgs.GetArgByIndex(0)->GetNode();
    }
    else
    {
        thisArgNode = call->gtArgs.GetThisArg()->GetNode();
    }

    assert(thisArgNode != nullptr);
    assert(thisArgNode->OperIs(GT_PUTARG_REG));
    GenTree* thisExpr = thisArgNode->AsOp()->gtOp1;

    // We're going to use the 'this' expression multiple times, so make a local to copy it.

    GenTree* base;
    if (thisExpr->OperIs(GT_LCL_VAR))
    {
        base = comp->gtNewLclvNode(thisExpr->AsLclVar()->GetLclNum(), thisExpr->TypeGet());
    }
    else if (thisExpr->OperIs(GT_LCL_FLD))
    {
        base = comp->gtNewLclFldNode(thisExpr->AsLclFld()->GetLclNum(), thisExpr->TypeGet(),
                                     thisExpr->AsLclFld()->GetLclOffs());
    }
    else
    {
        unsigned delegateInvokeTmp = comp->lvaGrabTemp(true DEBUGARG("delegate invoke call"));
        base                       = comp->gtNewLclvNode(delegateInvokeTmp, thisExpr->TypeGet());

        LIR::Use thisExprUse(BlockRange(), &thisArgNode->AsOp()->gtOp1, thisArgNode);
        ReplaceWithLclVar(thisExprUse, delegateInvokeTmp);

        thisExpr = thisExprUse.Def(); // it's changed; reload it.
    }

    // replace original expression feeding into thisPtr with
    // [originalThis + offsetOfDelegateInstance]

    GenTree* newThisAddr = new (comp, GT_LEA)
        GenTreeAddrMode(TYP_BYREF, thisExpr, nullptr, 0, comp->eeGetEEInfo()->offsetOfDelegateInstance);

    GenTree* newThis = comp->gtNewIndir(TYP_REF, newThisAddr);

    // Insert the new 'this' arg right before the call to get the correct null
    // behavior (the NRE that would logically happen inside Delegate.Invoke
    // should happen after all args are evaluated). We must also move the
    // PUTARG_REG node ahead.
    thisArgNode->AsOp()->gtOp1 = newThis;
    BlockRange().Remove(thisArgNode);
    BlockRange().InsertBefore(call, newThisAddr, newThis, thisArgNode);

    ContainCheckIndir(newThis->AsIndir());

    // the control target is
    // [originalThis + firstTgtOffs]

    unsigned targetOffs = comp->eeGetEEInfo()->offsetOfDelegateFirstTarget;
    GenTree* result     = new (comp, GT_LEA) GenTreeAddrMode(TYP_REF, base, nullptr, 0, targetOffs);
    GenTree* callTarget = Ind(result);

    // don't need to sequence and insert this tree, caller will do it

    return callTarget;
}

GenTree* Lowering::LowerIndirectNonvirtCall(GenTreeCall* call)
{
#ifdef TARGET_X86
    if (call->gtCallCookie != nullptr)
    {
        NYI_X86("Morphing indirect non-virtual call with non-standard args");
    }
#endif

    // Indirect cookie calls gets transformed by fgMorphArgs as indirect call with non-standard args.
    // Hence we should never see this type of call in lower.

    noway_assert(call->gtCallCookie == nullptr);

    return nullptr;
}

//------------------------------------------------------------------------
// CreateReturnTrapSeq: Create a tree to perform a "return trap", used in PInvoke
// epilogs to invoke a GC under a condition. The return trap checks some global
// location (the runtime tells us where that is and how many indirections to make),
// then, based on the result, conditionally calls a GC helper. We use a special node
// for this because at this time (late in the compilation phases), introducing flow
// is tedious/difficult.
//
// This is used for PInvoke inlining.
//
// Return Value:
//    Code tree to perform the action.
//
GenTree* Lowering::CreateReturnTrapSeq()
{
    // The GT_RETURNTRAP node expands to this:
    //    if (g_TrapReturningThreads)
    //    {
    //       RareDisablePreemptiveGC();
    //    }

    // The only thing to do here is build up the expression that evaluates 'g_TrapReturningThreads'.

    void*    pAddrOfCaptureThreadGlobal = nullptr;
    int32_t* addrOfCaptureThreadGlobal =
        comp->info.compCompHnd->getAddrOfCaptureThreadGlobal(&pAddrOfCaptureThreadGlobal);

    GenTree* testTree;
    if (addrOfCaptureThreadGlobal != nullptr)
    {
        testTree = AddrGen(addrOfCaptureThreadGlobal);
    }
    else
    {
        testTree = Ind(AddrGen(pAddrOfCaptureThreadGlobal));
    }
    return comp->gtNewOperNode(GT_RETURNTRAP, TYP_INT, Ind(testTree, TYP_INT));
}

//------------------------------------------------------------------------
// SetGCState: Create a tree that stores the given constant (0 or 1) into the
// thread's GC state field.
//
// This is used for PInvoke inlining.
//
// Arguments:
//    state - constant (0 or 1) to store into the thread's GC state field.
//
// Return Value:
//    Code tree to perform the action.
//
GenTree* Lowering::SetGCState(int state)
{
    // Thread.offsetOfGcState = 0/1

    assert(state == 0 || state == 1);

    const CORINFO_EE_INFO* pInfo = comp->eeGetEEInfo();

    GenTree* base = new (comp, GT_LCL_VAR) GenTreeLclVar(GT_LCL_VAR, TYP_I_IMPL, comp->info.compLvFrameListRoot);

    GenTree* stateNode    = new (comp, GT_CNS_INT) GenTreeIntCon(TYP_BYTE, state);
    GenTree* addr         = new (comp, GT_LEA) GenTreeAddrMode(TYP_I_IMPL, base, nullptr, 1, pInfo->offsetOfGCState);
    GenTree* storeGcState = new (comp, GT_STOREIND) GenTreeStoreInd(TYP_BYTE, addr, stateNode);
    return storeGcState;
}

//------------------------------------------------------------------------
// CreateFrameLinkUpdate: Create a tree that either links or unlinks the
// locally-allocated InlinedCallFrame from the Frame list.
//
// This is used for PInvoke inlining.
//
// Arguments:
//    action - whether to link (push) or unlink (pop) the Frame
//
// Return Value:
//    Code tree to perform the action.
//
GenTree* Lowering::CreateFrameLinkUpdate(FrameLinkAction action)
{
    const CORINFO_EE_INFO*                       pInfo         = comp->eeGetEEInfo();
    const CORINFO_EE_INFO::InlinedCallFrameInfo& callFrameInfo = pInfo->inlinedCallFrameInfo;

    GenTree* TCB = comp->gtNewLclVarNode(comp->info.compLvFrameListRoot, TYP_I_IMPL);

    // Thread->m_pFrame
    GenTree* addr = new (comp, GT_LEA) GenTreeAddrMode(TYP_I_IMPL, TCB, nullptr, 1, pInfo->offsetOfThreadFrame);

    GenTree* data = nullptr;

    if (action == PushFrame)
    {
        // Thread->m_pFrame = &inlinedCallFrame;
        data = comp->gtNewLclVarAddrNode(comp->lvaInlinedPInvokeFrameVar);
    }
    else
    {
        assert(action == PopFrame);
        // Thread->m_pFrame = inlinedCallFrame.m_pNext;

        data = comp->gtNewLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                     pInfo->inlinedCallFrameInfo.offsetOfFrameLink);
    }
    GenTree* storeInd = comp->gtNewStoreIndNode(TYP_I_IMPL, addr, data);
    return storeInd;
}

//------------------------------------------------------------------------
// InsertPInvokeMethodProlog: Create the code that runs at the start of
// every method that has PInvoke calls.
//
// Initialize the TCB local and the InlinedCallFrame object. Then link ("push")
// the InlinedCallFrame object on the Frame chain. The layout of InlinedCallFrame
// is defined in vm/frames.h. See also vm/jitinterface.cpp for more information.
// The offsets of these fields is returned by the VM in a call to ICorStaticInfo::getEEInfo().
//
// The (current) layout is as follows:
//
//  64-bit  32-bit                                    CORINFO_EE_INFO
//  offset  offset  field name                        offset                  when set
//  -----------------------------------------------------------------------------------------
//  +00h    +00h    _frameIdentifier                  0                       method prolog
//  +08h    +04h    m_Next                            offsetOfFrameLink       method prolog
//  +10h    +08h    m_Datum                           offsetOfCallTarget      call site
//  +18h    +0Ch    m_pCallSiteSP                     offsetOfCallSiteSP      x86: call site, and zeroed in method
//                                                                              prolog;
//                                                                            non-x86: method prolog (SP remains
//                                                                              constant in function, after prolog: no
//                                                                              localloc and PInvoke in same function)
//  +20h    +10h    m_pCallerReturnAddress            offsetOfReturnAddress   call site
//  +28h    +14h    m_pCalleeSavedFP                  offsetOfCalleeSavedFP   not set by JIT
//  +30h    +18h    m_pThread
//          +1Ch    m_pSPAfterProlog                  offsetOfSPAfterProlog   arm only
//  +38h    +1C/20h m_StubSecretArg                   offsetOfSecretStubArg   method prolog of IL stubs with secret arg
//
// Note that in the VM, InlinedCallFrame is a C++ class whose objects have a 'this' pointer that points
// to the InlinedCallFrame vptr (the 2nd field listed above), and the GS cookie is stored *before*
// the object. When we link the InlinedCallFrame onto the Frame chain, we must point at this location,
// and not at the beginning of the InlinedCallFrame local, which is actually the GS cookie.
//
// See the usages for USE_PER_FRAME_PINVOKE_INIT for more information.
void Lowering::InsertPInvokeMethodProlog()
{
    noway_assert(comp->info.compUnmanagedCallCountWithGCTransition);
    noway_assert(comp->lvaInlinedPInvokeFrameVar != BAD_VAR_NUM);

    if (!comp->info.compPublishStubParam && comp->opts.ShouldUsePInvokeHelpers())
    {
        return;
    }

    JITDUMP("======= Inserting PInvoke method prolog\n");

    LIR::Range& firstBlockRange = LIR::AsRange(comp->fgFirstBB);

    const CORINFO_EE_INFO*                       pInfo         = comp->eeGetEEInfo();
    const CORINFO_EE_INFO::InlinedCallFrameInfo& callFrameInfo = pInfo->inlinedCallFrameInfo;

    assert(comp->lvaGetDesc(comp->lvaInlinedPInvokeFrameVar)->IsAddressExposed());

    GenTree* const insertionPoint = firstBlockRange.FirstNonCatchArgNode();

    // Store the stub secret arg if necessary. This has to be done before the
    // call to the init helper below, which links the frame into the thread
    // list on 32-bit platforms.
    // InlinedCallFrame.m_StubSecretArg = stubSecretArg;
    if (comp->info.compPublishStubParam)
    {
        GenTree* value = comp->gtNewLclvNode(comp->lvaStubArgumentVar, TYP_I_IMPL);
        GenTree* store = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                    callFrameInfo.offsetOfSecretStubArg, value);
        firstBlockRange.InsertBefore(insertionPoint, LIR::SeqTree(comp, store));
        DISPTREERANGE(firstBlockRange, store);
    }

    // If we use P/Invoke helper calls then the hidden stub initialization
    // is all we need to do. Rest will get initialized by the helper.
    if (comp->opts.ShouldUsePInvokeHelpers())
    {
        return;
    }

    // Call runtime helper to fill in our InlinedCallFrame and push it on the Frame list:
    //     TCB = CORINFO_HELP_INIT_PINVOKE_FRAME(&symFrameStart);
    GenTree*   frameAddr    = comp->gtNewLclVarAddrNode(comp->lvaInlinedPInvokeFrameVar);
    NewCallArg frameAddrArg = NewCallArg::Primitive(frameAddr).WellKnown(WellKnownArg::PInvokeFrame);

    GenTreeCall* call = comp->gtNewHelperCallNode(CORINFO_HELP_INIT_PINVOKE_FRAME, TYP_I_IMPL);
    call->gtArgs.PushBack(comp, frameAddrArg);

    // some sanity checks on the frame list root vardsc
    const unsigned   lclNum = comp->info.compLvFrameListRoot;
    const LclVarDsc* varDsc = comp->lvaGetDesc(lclNum);
    noway_assert(!varDsc->lvIsParam);
    noway_assert(varDsc->lvType == TYP_I_IMPL);

    GenTree* store = comp->gtNewStoreLclVarNode(lclNum, call);
    comp->fgMorphTree(store);
    firstBlockRange.InsertBefore(insertionPoint, LIR::SeqTree(comp, store));
    DISPTREERANGE(firstBlockRange, store);

#if !defined(TARGET_X86) && !defined(TARGET_ARM)
    // For x86, this step is done at the call site (due to stack pointer not being static in the function).
    // For arm32, CallSiteSP is set up by the call to CORINFO_HELP_INIT_PINVOKE_FRAME.

    // --------------------------------------------------------
    // InlinedCallFrame.m_pCallSiteSP = @RSP;

    GenTree*       spValue = PhysReg(REG_SPBASE);
    GenTreeLclFld* storeSP = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                        callFrameInfo.offsetOfCallSiteSP, spValue);

    firstBlockRange.InsertBefore(insertionPoint, LIR::SeqTree(comp, storeSP));
    DISPTREERANGE(firstBlockRange, storeSP);

#endif // !defined(TARGET_X86) && !defined(TARGET_ARM)

#if !defined(TARGET_ARM)
    // For arm32, CalleeSavedFP is set up by the call to CORINFO_HELP_INIT_PINVOKE_FRAME.

    // --------------------------------------------------------
    // InlinedCallFrame.m_pCalleeSavedEBP = @RBP;

    GenTree*       fpValue = PhysReg(REG_FPBASE);
    GenTreeLclFld* storeFP = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                        callFrameInfo.offsetOfCalleeSavedFP, fpValue);

    firstBlockRange.InsertBefore(insertionPoint, LIR::SeqTree(comp, storeFP));
    DISPTREERANGE(firstBlockRange, storeFP);
#endif // !defined(TARGET_ARM)

    // --------------------------------------------------------
    // On 32-bit targets, CORINFO_HELP_INIT_PINVOKE_FRAME initializes the PInvoke frame and then pushes it onto
    // the current thread's Frame stack. On 64-bit targets, it only initializes the PInvoke frame.
    // As a result, don't push the frame onto the frame stack here for any 64-bit targets

#ifdef TARGET_64BIT
#ifdef USE_PER_FRAME_PINVOKE_INIT
    // For IL stubs, we push the frame once even when we're doing per-pinvoke init.
    if (comp->opts.jitFlags->IsSet(JitFlags::JIT_FLAG_IL_STUB))
#endif // USE_PER_FRAME_PINVOKE_INIT
    {
        // Push a frame. The init routine sets InlinedCallFrame's m_pNext, so we just set the thread's top-of-stack
        GenTree* frameUpd = CreateFrameLinkUpdate(PushFrame);
        firstBlockRange.InsertBefore(insertionPoint, LIR::SeqTree(comp, frameUpd));
        ContainCheckStoreIndir(frameUpd->AsStoreInd());
        DISPTREERANGE(firstBlockRange, frameUpd);
    }
#endif // TARGET_64BIT
}

//------------------------------------------------------------------------
// InsertPInvokeMethodEpilog: Code that needs to be run when exiting any method
// that has PInvoke inlines. This needs to be inserted any place you can exit the
// function: returns, tailcalls and jmps.
//
// Arguments:
//    returnBB   -  basic block from which a method can return
//    lastExpr   -  GenTree of the last top level stmnt of returnBB (debug only arg)
//
// Return Value:
//    Code tree to perform the action.
//
void Lowering::InsertPInvokeMethodEpilog(BasicBlock* returnBB DEBUGARG(GenTree* lastExpr))
{
    assert(returnBB != nullptr);
    assert(comp->info.compUnmanagedCallCountWithGCTransition);

    if (comp->opts.ShouldUsePInvokeHelpers())
    {
        return;
    }

    JITDUMP("======= Inserting PInvoke method epilog\n");

    // Method doing PInvoke calls has exactly one return block unless it has "jmp" or tail calls.
    assert(returnBB->KindIs(BBJ_RETURN) || returnBB->endsWithTailCallOrJmp(comp));

    LIR::Range& returnBlockRange = LIR::AsRange(returnBB);

    GenTree* insertionPoint = returnBlockRange.LastNode();
    assert(insertionPoint == lastExpr);

    // Note: PInvoke Method Epilog (PME) needs to be inserted just before GT_RETURN, GT_JMP or GT_CALL node in execution
    // order so that it is guaranteed that there will be no further PInvokes after that point in the method.
    //
    // Example1: GT_RETURN(op1) - say execution order is: Op1, GT_RETURN.  After inserting PME, execution order would be
    //           Op1, PME, GT_RETURN
    //
    // Example2: GT_CALL(arg side effect computing nodes, Stk Args Setup, Reg Args setup). The execution order would be
    //           arg side effect computing nodes, Stk Args setup, Reg Args setup, GT_CALL
    //           After inserting PME execution order would be:
    //           arg side effect computing nodes, Stk Args setup, Reg Args setup, PME, GT_CALL
    //
    // Example3: GT_JMP.  After inserting PME execution order would be: PME, GT_JMP
    //           That is after PME, args for GT_JMP call will be setup.

    // Pop the frame if necessary. This always happens in the epilog on 32-bit targets. For 64-bit targets, we only do
    // this in the epilog for IL stubs; for non-IL stubs the frame is popped after every PInvoke call.

#ifdef USE_PER_FRAME_PINVOKE_INIT
    // For IL stubs, we push the frame once even when we're doing per-pinvoke init
    if (comp->opts.jitFlags->IsSet(JitFlags::JIT_FLAG_IL_STUB))
#endif // USE_PER_FRAME_PINVOKE_INIT
    {
        GenTree* frameUpd = CreateFrameLinkUpdate(PopFrame);
        returnBlockRange.InsertBefore(insertionPoint, LIR::SeqTree(comp, frameUpd));
        ContainCheckStoreIndir(frameUpd->AsStoreInd());
    }
}

//------------------------------------------------------------------------
// InsertPInvokeCallProlog: Emit the call-site prolog for direct calls to unmanaged code.
// It does all the necessary call-site setup of the InlinedCallFrame.
//
// Arguments:
//    call - the call for which we are inserting the PInvoke prolog.
//
// Return Value:
//    None.
//
void Lowering::InsertPInvokeCallProlog(GenTreeCall* call)
{
    JITDUMP("======= Inserting PInvoke call prolog\n");

    GenTree* insertBefore = call;
    if (call->gtCallType == CT_INDIRECT)
    {
        bool isClosed;
        insertBefore = BlockRange().GetTreeRange(call->gtCallAddr, &isClosed).FirstNode();
        assert(isClosed);
    }

    const CORINFO_EE_INFO::InlinedCallFrameInfo& callFrameInfo = comp->eeGetEEInfo()->inlinedCallFrameInfo;

    gtCallTypes callType = (gtCallTypes)call->gtCallType;

    noway_assert(comp->lvaInlinedPInvokeFrameVar != BAD_VAR_NUM);

    if (comp->opts.ShouldUsePInvokeHelpers())
    {
        // First argument is the address of the frame variable.
        GenTree* frameAddr = comp->gtNewLclVarAddrNode(comp->lvaInlinedPInvokeFrameVar, TYP_BYREF);

#if defined(TARGET_X86) && !defined(UNIX_X86_ABI)
        // On x86 targets, PInvoke calls need the size of the stack args in InlinedCallFrame.m_Datum.
        // This is because the callee pops stack arguments, and we need to keep track of this during stack
        // walking
        const unsigned numStkArgBytes = call->gtArgs.OutgoingArgsStackSize();
        GenTree*       stackBytes     = comp->gtNewIconNode(numStkArgBytes, TYP_INT);
        // Insert call to CORINFO_HELP_JIT_PINVOKE_BEGIN
        GenTree* helperCall =
            comp->gtNewHelperCallNode(CORINFO_HELP_JIT_PINVOKE_BEGIN, TYP_VOID, frameAddr, stackBytes);
#else
        GenTree* helperCall = comp->gtNewHelperCallNode(CORINFO_HELP_JIT_PINVOKE_BEGIN, TYP_VOID, frameAddr);
#endif

        comp->fgMorphTree(helperCall);
        BlockRange().InsertBefore(insertBefore, LIR::SeqTree(comp, helperCall));
        LowerNode(helperCall); // helper call is inserted before current node and should be lowered here.
        return;
    }

    // Emit the following sequence:
    //
    // InlinedCallFrame.callTarget = methodHandle   // stored in m_Datum
    // InlinedCallFrame.m_pCallSiteSP = SP          // x86 only
    // InlinedCallFrame.m_pCallerReturnAddress = return address
    // GT_START_PREEEMPTC
    // Thread.gcState = 0
    // (non-stub) - update top Frame on TCB         // 64-bit targets only

    // ----------------------------------------------------------------------------------
    // Setup InlinedCallFrame.callSiteTarget (which is how the JIT refers to it).
    // The actual field is InlinedCallFrame.m_Datum which has many different uses and meanings.

    GenTree* src = nullptr;

    if (callType == CT_INDIRECT)
    {
#if !defined(TARGET_64BIT)
        // On 32-bit targets, indirect calls need the size of the stack args in InlinedCallFrame.m_Datum.
        const unsigned stackByteOffset = call->gtArgs.OutgoingArgsStackSize();
        src                            = comp->gtNewIconNode(stackByteOffset, TYP_INT);
#else
        // On 64-bit targets, indirect calls may need the stub parameter value in InlinedCallFrame.m_Datum.
        // If the stub parameter value is not needed, m_Datum will be initialized by the VM.
        if (comp->info.compPublishStubParam)
        {
            src = comp->gtNewLclvNode(comp->lvaStubArgumentVar, TYP_I_IMPL);
        }
#endif // !defined(TARGET_64BIT)
    }
    else
    {
        assert(callType == CT_USER_FUNC);

        void*                 pEmbedMethodHandle = nullptr;
        CORINFO_METHOD_HANDLE embedMethodHandle =
            comp->info.compCompHnd->embedMethodHandle(call->gtCallMethHnd, &pEmbedMethodHandle);

        noway_assert((!embedMethodHandle) != (!pEmbedMethodHandle));

        if (embedMethodHandle != nullptr)
        {
            // InlinedCallFrame.callSiteTarget = methodHandle
            src = AddrGen(embedMethodHandle);
        }
        else
        {
            // InlinedCallFrame.callSiteTarget = *pEmbedMethodHandle
            src = Ind(AddrGen(pEmbedMethodHandle));
        }
    }

    if (src != nullptr)
    {
        // Store into InlinedCallFrame.m_Datum, the offset of which is given by offsetOfCallTarget.
        GenTreeLclFld* store = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                          callFrameInfo.offsetOfCallTarget, src);

        InsertTreeBeforeAndContainCheck(insertBefore, store);
    }

#ifdef TARGET_X86

    // ----------------------------------------------------------------------------------
    // InlinedCallFrame.m_pCallSiteSP = SP

    GenTree*       callSiteSP      = PhysReg(REG_SPBASE);
    GenTreeLclFld* storeCallSiteSP = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                                callFrameInfo.offsetOfCallSiteSP, callSiteSP);

    InsertTreeBeforeAndContainCheck(insertBefore, storeCallSiteSP);

#endif

    // ----------------------------------------------------------------------------------
    // InlinedCallFrame.m_pCallerReturnAddress = &label (the address of the instruction immediately following the call)

    GenTree*       label    = new (comp, GT_LABEL) GenTree(GT_LABEL, TYP_I_IMPL);
    GenTreeLclFld* storeLab = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                         callFrameInfo.offsetOfReturnAddress, label);

    InsertTreeBeforeAndContainCheck(insertBefore, storeLab);

    // Push the PInvoke frame if necessary. On 32-bit targets this only happens in the method prolog if a method
    // contains PInvokes; on 64-bit targets this is necessary in non-stubs.

#ifdef USE_PER_FRAME_PINVOKE_INIT
    if (!comp->opts.jitFlags->IsSet(JitFlags::JIT_FLAG_IL_STUB))
    {
        // Set the TCB's frame to be the one we just created.
        // Note the init routine for the InlinedCallFrame (CORINFO_HELP_INIT_PINVOKE_FRAME)
        // has prepended it to the linked list to maintain the stack of Frames.
        //
        // Stubs do this once per stub, not once per call.
        GenTree* frameUpd = CreateFrameLinkUpdate(PushFrame);
        BlockRange().InsertBefore(insertBefore, LIR::SeqTree(comp, frameUpd));
        ContainCheckStoreIndir(frameUpd->AsStoreInd());
    }
#endif // USE_PER_FRAME_PINVOKE_INIT

    // IMPORTANT **** This instruction must be the last real instruction ****
    // It changes the thread's state to Preemptive mode
    // ----------------------------------------------------------------------------------
    //  [tcb + offsetOfGcState] = 0
    GenTree* storeGCState = SetGCState(0);
    BlockRange().InsertBefore(insertBefore, LIR::SeqTree(comp, storeGCState));
    ContainCheckStoreIndir(storeGCState->AsStoreInd());

    // Indicate that codegen has switched this thread to preemptive GC.
    // This tree node doesn't generate any code, but impacts LSRA and gc reporting.
    // This tree node is simple so doesn't require sequencing.
    GenTree* preemptiveGCNode = new (comp, GT_START_PREEMPTGC) GenTree(GT_START_PREEMPTGC, TYP_VOID);
    BlockRange().InsertBefore(insertBefore, preemptiveGCNode);
}

//------------------------------------------------------------------------
// InsertPInvokeCallEpilog: Insert the code that goes after every inlined pinvoke call.
//
// Arguments:
//    call - the call for which we are inserting the PInvoke epilog.
//
// Return Value:
//    None.
//
void Lowering::InsertPInvokeCallEpilog(GenTreeCall* call)
{
    JITDUMP("======= Inserting PInvoke call epilog\n");

    if (comp->opts.ShouldUsePInvokeHelpers())
    {
        noway_assert(comp->lvaInlinedPInvokeFrameVar != BAD_VAR_NUM);

        // First argument is the address of the frame variable.
        GenTree* frameAddr = comp->gtNewLclVarAddrNode(comp->lvaInlinedPInvokeFrameVar, TYP_BYREF);

#if defined(DEBUG)
        const LclVarDsc* inlinedPInvokeDsc = comp->lvaGetDesc(comp->lvaInlinedPInvokeFrameVar);
        assert(inlinedPInvokeDsc->IsAddressExposed());
#endif // DEBUG

        // Insert call to CORINFO_HELP_JIT_PINVOKE_END
        GenTreeCall* helperCall = comp->gtNewHelperCallNode(CORINFO_HELP_JIT_PINVOKE_END, TYP_VOID, frameAddr);

        comp->fgMorphTree(helperCall);
        BlockRange().InsertAfter(call, LIR::SeqTree(comp, helperCall));
        ContainCheckCallOperands(helperCall);
        return;
    }

    // gcstate = 1
    GenTree* insertionPoint = call->gtNext;

    GenTree* tree = SetGCState(1);
    BlockRange().InsertBefore(insertionPoint, LIR::SeqTree(comp, tree));
    ContainCheckStoreIndir(tree->AsStoreInd());

    tree = CreateReturnTrapSeq();
    BlockRange().InsertBefore(insertionPoint, LIR::SeqTree(comp, tree));
    ContainCheckReturnTrap(tree->AsOp());

    // Pop the frame if necessary. On 32-bit targets this only happens in the method epilog; on 64-bit targets
    // this happens after every PInvoke call in non-stubs. 32-bit targets instead mark the frame as inactive.

#ifdef USE_PER_FRAME_PINVOKE_INIT
    if (!comp->opts.jitFlags->IsSet(JitFlags::JIT_FLAG_IL_STUB))
    {
        tree = CreateFrameLinkUpdate(PopFrame);
        BlockRange().InsertBefore(insertionPoint, LIR::SeqTree(comp, tree));
        ContainCheckStoreIndir(tree->AsStoreInd());
    }
#else
    const CORINFO_EE_INFO::InlinedCallFrameInfo& callFrameInfo = comp->eeGetEEInfo()->inlinedCallFrameInfo;

    // ----------------------------------------------------------------------------------
    // InlinedCallFrame.m_pCallerReturnAddress = nullptr

    GenTreeIntCon* const zero                 = comp->gtNewIconNode(0, TYP_I_IMPL);
    GenTreeLclFld* const storeCallSiteTracker = comp->gtNewStoreLclFldNode(comp->lvaInlinedPInvokeFrameVar, TYP_I_IMPL,
                                                                           callFrameInfo.offsetOfReturnAddress, zero);

    BlockRange().InsertBefore(insertionPoint, zero, storeCallSiteTracker);
    ContainCheckStoreLoc(storeCallSiteTracker);
#endif // USE_PER_FRAME_PINVOKE_INIT
}

//------------------------------------------------------------------------
// LowerNonvirtPinvokeCall: Lower a non-virtual / indirect PInvoke call
//
// Arguments:
//    call - The call to lower.
//
// Return Value:
//    The lowered call tree.
//
GenTree* Lowering::LowerNonvirtPinvokeCall(GenTreeCall* call)
{
    // PInvoke lowering varies depending on the flags passed in by the EE. By default,
    // GC transitions are generated inline; if CORJIT_FLAG_USE_PINVOKE_HELPERS is specified,
    // GC transitions are instead performed using helper calls. Examples of each case are given
    // below. Note that the data structure that is used to store information about a call frame
    // containing any P/Invoke calls is initialized in the method prolog (see
    // InsertPInvokeMethod{Prolog,Epilog} for details).
    //
    // Inline transitions:
    //     InlinedCallFrame inlinedCallFrame;
    //
    //     ...
    //
    //     // Set up frame information
    //     inlinedCallFrame.callTarget = methodHandle;      // stored in m_Datum
    //     inlinedCallFrame.m_pCallSiteSP = SP;             // x86 only
    //     inlinedCallFrame.m_pCallerReturnAddress = &label; (the address of the instruction immediately following the
    //     call)
    //     Thread.m_pFrame = &inlinedCallFrame; (non-IL-stub only)
    //
    //     // Switch the thread's GC mode to preemptive mode
    //     thread->m_fPreemptiveGCDisabled = 0;
    //
    //     // Call the unmanaged method
    //     target();
    //
    //     // Switch the thread's GC mode back to cooperative mode
    //     thread->m_fPreemptiveGCDisabled = 1;
    //
    //     // Rendezvous with a running collection if necessary
    //     if (g_TrapReturningThreads)
    //         RareDisablePreemptiveGC();
    //
    // Transitions using helpers:
    //
    //     OpaqueFrame opaqueFrame;
    //
    //     ...
    //
    //     // Call the JIT_PINVOKE_BEGIN helper
    //     JIT_PINVOKE_BEGIN(&opaqueFrame);
    //
    //     // Call the unmanaged method
    //     target();
    //
    //     // Call the JIT_PINVOKE_END helper
    //     JIT_PINVOKE_END(&opaqueFrame);
    //
    // Note that the JIT_PINVOKE_{BEGIN.END} helpers currently use the default calling convention for the target
    // platform. They may be changed in the future such that they preserve all register values.

    GenTree* result = nullptr;

    // All code generated by this function must not contain the randomly-inserted NOPs
    // that we insert to inhibit JIT spraying in partial trust scenarios.
    // The PINVOKE_PROLOG op signals this to the code generator/emitter.

    GenTree* prolog = new (comp, GT_NOP) GenTree(GT_PINVOKE_PROLOG, TYP_VOID);
    BlockRange().InsertBefore(call, prolog);

    bool addPInvokePrologEpilog = !call->IsSuppressGCTransition();
    if (addPInvokePrologEpilog)
    {
        InsertPInvokeCallProlog(call);
    }

    if (call->gtCallType != CT_INDIRECT)
    {
        noway_assert(call->gtCallType == CT_USER_FUNC);
        CORINFO_METHOD_HANDLE methHnd = call->gtCallMethHnd;

        CORINFO_CONST_LOOKUP lookup;
        comp->info.compCompHnd->getAddressOfPInvokeTarget(methHnd, &lookup);

        void*    addr = lookup.addr;
        GenTree* addrTree;
        switch (lookup.accessType)
        {
            case IAT_VALUE:
                // IsCallTargetInRange always return true on x64. It wants to use rip-based addressing
                // for this call. Unfortunately, in case of already resolved pinvokes to external libs,
                // which are identified via accessType: IAT_VALUE, the relative offset is unlikely to
                // fit into int32 and we will have to turn fAllowRel32 off globally. To prevent that
                // we'll create a wrapper node and force LSRA to allocate a register so RIP relative
                // isn't used and we don't need to pessimize other callsites.
                if (!comp->IsAot() || !IsCallTargetInRange(addr))
                {
                    result = AddrGen(addr);
                }
                else
                {
                    // a direct call within range of hardware relative call instruction
                    // stash the address for codegen
                    call->gtDirectCallAddress = addr;
#ifdef FEATURE_READYTORUN
                    call->gtEntryPoint.addr       = nullptr;
                    call->gtEntryPoint.accessType = IAT_VALUE;
#endif
                }
                break;

            case IAT_PVALUE:
                addrTree = AddrGen(addr);
#ifdef DEBUG
                addrTree->AsIntCon()->gtTargetHandle = (size_t)methHnd;
#endif
                result = Ind(addrTree);
                break;

            case IAT_PPVALUE:
                // ToDo:  Expanding an IAT_PPVALUE here, loses the opportunity
                // to Hoist/CSE the first indirection as it is an invariant load
                //
                // This case currently occurs today when we make PInvoke calls in crossgen
                //
                // assert(!"IAT_PPVALUE in Lowering::LowerNonvirtPinvokeCall");

                addrTree = AddrGen(addr);
#ifdef DEBUG
                addrTree->AsIntCon()->gtTargetHandle = (size_t)methHnd;
#endif
                // Double-indirection. Load the address into a register
                // and call indirectly through the register
                //
                result = Ind(Ind(addrTree));
                break;

            case IAT_RELPVALUE:
                unreached();
        }
    }

    if (addPInvokePrologEpilog)
    {
        InsertPInvokeCallEpilog(call);
    }

#ifdef SWIFT_SUPPORT
    // For Swift calls that require error handling, ensure the GT_SWIFT_ERROR node
    // that consumes the error register is the call node's successor.
    // This is to simplify logic for marking the error register as busy in LSRA.
    if (call->HasSwiftErrorHandling())
    {
        GenTree* swiftErrorNode = call->gtNext;
        assert(swiftErrorNode != nullptr);

        while (!swiftErrorNode->OperIs(GT_SWIFT_ERROR))
        {
            swiftErrorNode = swiftErrorNode->gtNext;
            assert(swiftErrorNode != nullptr);
        }

        BlockRange().Remove(swiftErrorNode);
        BlockRange().InsertAfter(call, swiftErrorNode);
    }
#endif // SWIFT_SUPPORT

    return result;
}

// Expand the code necessary to calculate the control target.
// Returns: the expression needed to calculate the control target
// May insert embedded statements
GenTree* Lowering::LowerVirtualVtableCall(GenTreeCall* call)
{
    noway_assert(call->gtCallType == CT_USER_FUNC);

    GenTree* thisArgNode;
    if (call->IsTailCallViaJitHelper())
    {
        assert(call->gtArgs.CountArgs() > 0);
        thisArgNode = call->gtArgs.GetArgByIndex(0)->GetNode();
    }
    else
    {
        assert(call->gtArgs.HasThisPointer());
        thisArgNode = call->gtArgs.GetThisArg()->GetNode();
    }

    // get a reference to the thisPtr being passed
    assert(thisArgNode->OperIs(GT_PUTARG_REG));
    GenTree* thisPtr = thisArgNode->AsUnOp()->gtGetOp1();

    // If what we are passing as the thisptr is not already a local, make a new local to place it in
    // because we will be creating expressions based on it.
    unsigned lclNum;
    if (thisPtr->OperIsLocal())
    {
        lclNum = thisPtr->AsLclVarCommon()->GetLclNum();
    }
    else
    {
        // Split off the thisPtr and store to a temporary variable.
        if (vtableCallTemp == BAD_VAR_NUM)
        {
            vtableCallTemp = comp->lvaGrabTemp(true DEBUGARG("virtual vtable call"));
        }

        LIR::Use thisPtrUse(BlockRange(), &thisArgNode->AsUnOp()->gtOp1, thisArgNode);
        ReplaceWithLclVar(thisPtrUse, vtableCallTemp);

        lclNum = vtableCallTemp;
    }

    // Get hold of the vtable offset (note: this might be expensive)
    unsigned vtabOffsOfIndirection;
    unsigned vtabOffsAfterIndirection;
    bool     isRelative;
    comp->info.compCompHnd->getMethodVTableOffset(call->gtCallMethHnd, &vtabOffsOfIndirection,
                                                  &vtabOffsAfterIndirection, &isRelative);

    // If the thisPtr is a local field, then construct a local field type node
    GenTree* local;
    if (thisPtr->isLclField())
    {
        local = new (comp, GT_LCL_FLD)
            GenTreeLclFld(GT_LCL_FLD, thisPtr->TypeGet(), lclNum, thisPtr->AsLclFld()->GetLclOffs());
    }
    else
    {
        local = new (comp, GT_LCL_VAR) GenTreeLclVar(GT_LCL_VAR, thisPtr->TypeGet(), lclNum);
    }

    // pointer to virtual table = [REG_CALL_THIS + offs]
    GenTree* result = Ind(Offset(local, VPTR_OFFS));

    // Get the appropriate vtable chunk
    if (vtabOffsOfIndirection != CORINFO_VIRTUALCALL_NO_CHUNK)
    {
        if (isRelative)
        {
            // MethodTable offset is a relative pointer.
            //
            // Additional temporary variable is used to store virtual table pointer.
            // Address of method is obtained by the next computations:
            //
            // Save relative offset to tmp (vtab is virtual table pointer, vtabOffsOfIndirection is offset of
            // vtable-1st-level-indirection):
            // tmp = vtab
            //
            // Save address of method to result (vtabOffsAfterIndirection is offset of vtable-2nd-level-indirection):
            // result = [tmp + vtabOffsOfIndirection + vtabOffsAfterIndirection + [tmp + vtabOffsOfIndirection]]
            //
            //
            // If relative pointers are also in second level indirection, additional temporary is used:
            // tmp1 = vtab
            // tmp2 = tmp1 + vtabOffsOfIndirection + vtabOffsAfterIndirection + [tmp1 + vtabOffsOfIndirection]
            // result = tmp2 + [tmp2]
            //
            unsigned lclNumTmp  = comp->lvaGrabTemp(true DEBUGARG("lclNumTmp"));
            unsigned lclNumTmp2 = comp->lvaGrabTemp(true DEBUGARG("lclNumTmp2"));

            GenTree* lclvNodeStore = comp->gtNewTempStore(lclNumTmp, result);

            GenTree* tmpTree = comp->gtNewLclvNode(lclNumTmp, result->TypeGet());
            tmpTree          = Offset(tmpTree, vtabOffsOfIndirection);

            tmpTree       = Ind(tmpTree);
            GenTree* offs = comp->gtNewIconNode(vtabOffsOfIndirection + vtabOffsAfterIndirection, TYP_INT);
            result = comp->gtNewOperNode(GT_ADD, TYP_I_IMPL, comp->gtNewLclvNode(lclNumTmp, result->TypeGet()), offs);

            GenTree* base           = OffsetByIndexWithScale(result, tmpTree, 1);
            GenTree* lclvNodeStore2 = comp->gtNewTempStore(lclNumTmp2, base);

            LIR::Range range = LIR::SeqTree(comp, lclvNodeStore);
            JITDUMP("result of obtaining pointer to virtual table:\n");
            DISPRANGE(range);
            BlockRange().InsertBefore(call, std::move(range));

            LIR::Range range2 = LIR::SeqTree(comp, lclvNodeStore2);
            ContainCheckIndir(tmpTree->AsIndir());
            JITDUMP("result of obtaining pointer to virtual table 2nd level indirection:\n");
            DISPRANGE(range2);
            BlockRange().InsertAfter(lclvNodeStore, std::move(range2));

            result = Ind(comp->gtNewLclvNode(lclNumTmp2, result->TypeGet()));
            result =
                comp->gtNewOperNode(GT_ADD, TYP_I_IMPL, result, comp->gtNewLclvNode(lclNumTmp2, result->TypeGet()));
        }
        else
        {
            // result = [REG_CALL_IND_SCRATCH + vtabOffsOfIndirection]
            result = Ind(Offset(result, vtabOffsOfIndirection));
        }
    }
    else
    {
        assert(!isRelative);
    }

    // Load the function address
    // result = [reg+vtabOffs]
    if (!isRelative)
    {
        result = Ind(Offset(result, vtabOffsAfterIndirection));
    }

    return result;
}

// Lower stub dispatched virtual calls.
GenTree* Lowering::LowerVirtualStubCall(GenTreeCall* call)
{
    assert(call->IsVirtualStub());

    // An x86 JIT which uses full stub dispatch must generate only
    // the following stub dispatch calls:
    //
    // (1) isCallRelativeIndirect:
    //        call dword ptr [rel32]  ;  FF 15 ---rel32----
    // (2) isCallRelative:
    //        call abc                ;     E8 ---rel32----
    // (3) isCallRegisterIndirect:
    //     3-byte nop                 ;
    //     call dword ptr [eax]       ;     FF 10
    //
    // THIS IS VERY TIGHTLY TIED TO THE PREDICATES IN
    // vm\i386\cGenCpu.h, esp. isCallRegisterIndirect.

    GenTree* result = nullptr;

    // This is code to set up an indirect call to a stub address computed
    // via dictionary lookup.
    if (call->gtCallType == CT_INDIRECT)
    {
        // The importer decided we needed a stub call via a computed
        // stub dispatch address, i.e. an address which came from a dictionary lookup.
        //   - The dictionary lookup produces an indirected address, suitable for call
        //     via "call [VirtualStubParam.reg]"
        //
        // This combination will only be generated for shared generic code and when
        // stub dispatch is active.

        // fgMorphArgs will have created trees to pass the address in VirtualStubParam.reg.
        // All we have to do here is add an indirection to generate the actual call target.

        GenTree* ind = Ind(call->gtCallAddr);
        BlockRange().InsertAfter(call->gtCallAddr, ind);
        call->gtCallAddr = ind;

        ind->gtFlags |= GTF_IND_REQ_ADDR_IN_REG;

        ContainCheckIndir(ind->AsIndir());
    }
    else
    {
        // Direct stub call.
        // Get stub addr. This will return NULL if virtual call stubs are not active
        void* stubAddr = call->gtStubCallStubAddr;
        noway_assert(stubAddr != nullptr);

        // If not CT_INDIRECT,  then it should always be relative indir call.
        // This is ensured by VM.
        noway_assert(call->IsVirtualStubRelativeIndir());

        // Direct stub calls, though the stubAddr itself may still need to be
        // accessed via an indirection.
        GenTree* addr = AddrGen(stubAddr);

        // On x86, for tailcall via helper, the JIT_TailCall helper takes the stubAddr as
        // the target address, and we set a flag that it's a VSD call. The helper then
        // handles any necessary indirection.
        if (call->IsTailCallViaJitHelper())
        {
            result = addr;
        }
        else
        {
            bool shouldOptimizeVirtualStubCall = false;
#if defined(TARGET_ARMARCH) || defined(TARGET_AMD64) || defined(TARGET_LOONGARCH64)
            // Skip inserting the indirection node to load the address that is already
            // computed in the VSD stub arg register as a hidden parameter. Instead during the
            // codegen, just load the call target from there.
            shouldOptimizeVirtualStubCall = true;
#endif

            if (!shouldOptimizeVirtualStubCall)
            {
                result = Ind(addr);
            }
        }
    }

    // TODO-Cleanup: start emitting random NOPS
    return result;
}

//------------------------------------------------------------------------
// Lowering::AreSourcesPossibleModifiedLocals:
//    Given two nodes which will be used in an addressing mode (base,
//    index), check to see if they are lclVar reads, and if so, walk
//    backwards from the use until both reads have been visited to
//    determine if they are potentially modified in that range.
//
// Arguments:
//    addr - the node that uses the base and index nodes
//    base - the base node
//    index - the index node
//
// Returns: true if either the base or index may be modified between the
//          node and addr.
//
bool Lowering::AreSourcesPossiblyModifiedLocals(GenTree* addr, GenTree* base, GenTree* index)
{
    assert(addr != nullptr);

    SideEffectSet baseSideEffects;
    if (base != nullptr)
    {
        if (base->OperIsLocalRead())
        {
            baseSideEffects.AddNode(comp, base);
        }
        else
        {
            base = nullptr;
        }
    }

    SideEffectSet indexSideEffects;
    if (index != nullptr)
    {
        if (index->OperIsLocalRead())
        {
            indexSideEffects.AddNode(comp, index);
        }
        else
        {
            index = nullptr;
        }
    }

    for (GenTree* cursor = addr;; cursor = cursor->gtPrev)
    {
        assert(cursor != nullptr);

        if (cursor == base)
        {
            base = nullptr;
        }

        if (cursor == index)
        {
            index = nullptr;
        }

        if ((base == nullptr) && (index == nullptr))
        {
            return false;
        }

        m_scratchSideEffects.Clear();
        m_scratchSideEffects.AddNode(comp, cursor);
        if ((base != nullptr) && m_scratchSideEffects.InterferesWith(baseSideEffects, false))
        {
            return true;
        }

        if ((index != nullptr) && m_scratchSideEffects.InterferesWith(indexSideEffects, false))
        {
            return true;
        }
    }
}

//------------------------------------------------------------------------
// TryCreateAddrMode: recognize trees which can be implemented using an
//    addressing mode and transform them to a GT_LEA
//
// Arguments:
//    addr - the use of the address we want to transform
//    isContainable - true if this addressing mode can be contained
//    parent - the node that consumes the given addr (most likely it's an IND)
//
// Returns:
//    true if the address node was changed to a LEA, false otherwise.
//
bool Lowering::TryCreateAddrMode(GenTree* addr, bool isContainable, GenTree* parent)
{
    if (!addr->OperIs(GT_ADD) || addr->gtOverflow())
    {
        return false;
    }

#ifdef TARGET_ARM64
    const bool hasRcpc2 = comp->compOpportunisticallyDependsOn(InstructionSet_Rcpc2);
    if (parent->OperIsIndir() && parent->AsIndir()->IsVolatile() && !hasRcpc2)
    {
        // For Arm64 we avoid using LEA for volatile INDs
        // because we won't be able to use ldar/star
        return false;
    }
#endif

    GenTree* base   = nullptr;
    GenTree* index  = nullptr;
    unsigned scale  = 0;
    ssize_t  offset = 0;
    bool     rev    = false;

    var_types targetType = parent->OperIsIndir() ? parent->TypeGet() : TYP_UNDEF;

    unsigned naturalMul = 0;
#ifdef TARGET_ARM64
    // Multiplier should be a "natural-scale" power of two number which is equal to target's width.
    //
    //   *(ulong*)(data + index * 8); - can be optimized
    //   *(ulong*)(data + index * 7); - can not be optimized
    //     *(int*)(data + index * 2); - can not be optimized
    //
    naturalMul = genTypeSize(targetType);
#endif

    // Find out if an addressing mode can be constructed
    bool doAddrMode = comp->codeGen->genCreateAddrMode(addr,       // address
                                                       true,       // fold
                                                       naturalMul, // natural multiplier
                                                       &rev,       // reverse ops
                                                       &base,      // base addr
                                                       &index,     // index val
                                                       &scale,     // scaling
                                                       &offset);   // displacement

#ifdef TARGET_ARM64
    if (parent->OperIsIndir() && parent->AsIndir()->IsVolatile())
    {
        // Generally, we try to avoid creating addressing modes for volatile INDs so we can then use
        // ldar/stlr instead of ldr/str + dmb. Although, with Arm 8.4+'s RCPC2 we can handle unscaled
        // addressing modes (if the offset fits into 9 bits)
        assert(hasRcpc2);
        if ((scale > 1) || (!emitter::emitIns_valid_imm_for_unscaled_ldst_offset(offset)) || (index != nullptr))
        {
            return false;
        }
    }
#endif

    if (scale == 0)
    {
        scale = 1;
    }

    if (!isContainable)
    {
        // this is just a reg-const add
        if (index == nullptr)
        {
            return false;
        }

        // this is just a reg-reg add
        if ((scale == 1) && (offset == 0))
        {
            return false;
        }
    }

    // make sure there are not any side effects between def of leaves and use
    if (!doAddrMode || AreSourcesPossiblyModifiedLocals(addr, base, index))
    {
        JITDUMP("No addressing mode:\n  ");
        DISPNODE(addr);
        return false;
    }

    JITDUMP("Addressing mode:\n");
    JITDUMP("  Base\n    ");
    DISPNODE(base);
    if (index != nullptr)
    {
        JITDUMP("  + Index * %u + %d\n    ", scale, offset);
        DISPNODE(index);
    }
    else
    {
        JITDUMP("  + %d\n", offset);
    }

    // Save the (potentially) unused operands before changing the address to LEA.
    ArrayStack<GenTree*> unusedStack(comp->getAllocator(CMK_ArrayStack));
    unusedStack.Push(addr->AsOp()->gtGetOp1());
    unusedStack.Push(addr->AsOp()->gtGetOp2());

    addr->ChangeOper(GT_LEA);
    // Make sure there are no leftover side effects (though the existing ADD we're
    // changing shouldn't have any at this point, but sometimes it does).
    addr->gtFlags &= ~GTF_ALL_EFFECT;

    GenTreeAddrMode* addrMode = addr->AsAddrMode();
    addrMode->SetBase(base);
    addrMode->SetIndex(index);
    addrMode->SetScale(scale);
    addrMode->SetOffset(static_cast<int>(offset));

    // Neither the base nor the index should now be contained.
    if (base != nullptr)
    {
        base->ClearContained();
    }
    if (index != nullptr)
    {
        index->ClearContained();
    }

    // Remove all the nodes that are no longer used.
    while (!unusedStack.Empty())
    {
        GenTree* unused = unusedStack.Pop();

        if ((unused != base) && (unused != index))
        {
            JITDUMP("Removing unused node:\n  ");
            DISPNODE(unused);

            BlockRange().Remove(unused);

            for (GenTree* operand : unused->Operands())
            {
                unusedStack.Push(operand);
            }

            DEBUG_DESTROY_NODE(unused);
        }
    }

#ifdef TARGET_ARM64

    if (index != nullptr)
    {
        if (index->OperIs(GT_CAST) && (scale == 1) && (offset == 0) && varTypeIsByte(targetType))
        {
            if (IsInvariantInRange(index, parent))
            {
                // Check containment safety against the parent node - this will ensure that LEA with the contained
                // index will itself always be contained. We do not support uncontained LEAs with contained indices.
                index->AsCast()->CastOp()->ClearContained(); // Uncontain any memory operands.
                MakeSrcContained(addrMode, index);
            }
        }
        else if (index->OperIs(GT_BFIZ) && index->gtGetOp1()->OperIs(GT_CAST) && index->gtGetOp2()->IsCnsIntOrI() &&
                 !varTypeIsStruct(targetType))
        {
            // Check if we can "contain" LEA(BFIZ) in order to extend 32bit index to 64bit as part of load/store.
            // BFIZ node is a binary op where op1 is GT_CAST and op2 is GT_CNS_INT
            GenTreeCast* cast = index->gtGetOp1()->AsCast();
            assert(cast->isContained());

            const unsigned shiftBy = (unsigned)index->gtGetOp2()->AsIntCon()->IconValue();

            // 'scale' and 'offset' have to be unset since we're going to use [base + index * SXTW/UXTW scale] form
            // where there is no room for additional offsets/scales on ARM64. 'shiftBy' has to match target's width.
            if (cast->CastOp()->TypeIs(TYP_INT) && cast->TypeIs(TYP_LONG) &&
                (genTypeSize(targetType) == (1U << shiftBy)) && (scale == 1) && (offset == 0))
            {
                if (IsInvariantInRange(index, parent))
                {
                    // Check containment safety against the parent node - this will ensure that LEA with the contained
                    // index will itself always be contained. We do not support uncontained LEAs with contained indices.

                    // TODO: Make sure that genCreateAddrMode marks such BFIZ candidates as GTF_DONT_CSE for better CQ.
                    MakeSrcContained(addrMode, index);
                }
            }
        }
    }
#endif

    JITDUMP("New addressing mode node:\n  ");
    DISPNODE(addrMode);
    JITDUMP("\n");

    return true;
}

//------------------------------------------------------------------------
// LowerAdd: turn this add into a GT_LEA if that would be profitable
//
// Arguments:
//    node - the node we care about
//
// Returns:
//    nullptr if no transformation was done, or the next node in the transformed node sequence that
//    needs to be lowered.
//
GenTree* Lowering::LowerAdd(GenTreeOp* node)
{
    if (varTypeIsIntegralOrI(node->TypeGet()))
    {
        GenTree* op1 = node->gtGetOp1();
        GenTree* op2 = node->gtGetOp2();
        LIR::Use use;

        // It is not the best place to do such simple arithmetic optimizations,
        // but it allows us to avoid `LEA(addr, 0)` nodes and doing that in morph
        // requires more changes. Delete that part if we get an expression optimizer.
        if (op2->IsIntegralConst(0))
        {
            JITDUMP("Lower: optimize val + 0: ");
            DISPNODE(node);
            JITDUMP("Replaced with: ");
            DISPNODE(op1);
            if (BlockRange().TryGetUse(node, &use))
            {
                use.ReplaceWith(op1);
            }
            else
            {
                op1->SetUnusedValue();
            }
            GenTree* next = node->gtNext;
            BlockRange().Remove(op2);
            BlockRange().Remove(node);
            JITDUMP("Remove [%06u], [%06u]\n", op2->gtTreeID, node->gtTreeID);
            return next;
        }

        if (comp->opts.OptimizationEnabled())
        {
            // Fold (x + c1) + c2
            while (op1->OperIs(GT_ADD) && op2->IsIntegralConst() && op1->gtGetOp2()->IsIntegralConst() &&
                   !node->gtOverflow() && !op1->gtOverflow())
            {
                GenTreeIntConCommon* cns1 = op1->gtGetOp2()->AsIntConCommon();
                GenTreeIntConCommon* cns2 = op2->AsIntConCommon();

                if (cns1->ImmedValNeedsReloc(comp) || cns2->ImmedValNeedsReloc(comp))
                {
                    break;
                }

                if (varTypeIsGC(cns1) || (cns1->TypeGet() != cns2->TypeGet()))
                {
                    break;
                }

                JITDUMP("Folding (x + c1) + c2. Before:\n");
                DISPTREERANGE(BlockRange(), node);

                int64_t c1 = cns1->IntegralValue();
                int64_t c2 = cns2->IntegralValue();

                int64_t result;
                if (genTypeSize(node) == sizeof(int64_t))
                {
                    result = c1 + c2;
                }
                else
                {
                    result = static_cast<int32_t>(c1) + static_cast<int32_t>(c2);
                }

                cns2->SetIntegralValue(result);
                node->gtOp1 = op1->gtGetOp1();

                BlockRange().Remove(cns1);
                BlockRange().Remove(op1);

                op1 = node->gtGetOp1();

                // We will recompute containment/reg optionality below.
                op1->ClearRegOptional();
                op1->ClearContained();

                JITDUMP("\nAfter:\n");
                DISPTREERANGE(BlockRange(), node);
            }

            // Fold ADD(CNS1, CNS2). We mainly target a very specific pattern - byref ADD(frozen_handle, cns_offset)
            // We could do this folding earlier, but that is not trivial as we'll have to introduce a way to restore
            // the original object from a byref constant for optimizations.
            if (op1->IsCnsIntOrI() && op2->IsCnsIntOrI() && !node->gtOverflow() &&
                (op1->IsIconHandle(GTF_ICON_OBJ_HDL) || op2->IsIconHandle(GTF_ICON_OBJ_HDL)) &&
                !op1->AsIntCon()->ImmedValNeedsReloc(comp) && !op2->AsIntCon()->ImmedValNeedsReloc(comp))
            {
                assert(node->TypeIs(TYP_I_IMPL, TYP_BYREF));

                // TODO-CQ: we should allow this for AOT too. For that we need to guarantee that the new constant
                // will be lowered as the original handle with offset in a reloc.
                BlockRange().Remove(op1);
                BlockRange().Remove(op2);
                node->BashToConst(op1->AsIntCon()->IconValue() + op2->AsIntCon()->IconValue(), node->TypeGet());
            }
        }

#ifdef TARGET_XARCH
        if (BlockRange().TryGetUse(node, &use))
        {
            // If this is a child of an ordinary indir, let the parent handle it.
            // If there is a chain of adds, only look at the topmost one.
            GenTree* parent = use.User();
            if ((!parent->OperIsIndir() || parent->OperIsAtomicOp()) && !parent->OperIs(GT_ADD))
            {
                TryCreateAddrMode(node, false, parent);
            }
        }
#endif // TARGET_XARCH
    }

#ifdef TARGET_ARM64
    if (node->OperIs(GT_ADD))
    {
        GenTree* next;
        if (TryLowerAddForPossibleContainment(node, &next))
        {
            return next;
        }

        if (TryLowerAddSubToMulLongOp(node, &next))
        {
            return next;
        }
    }
#endif // TARGET_ARM64

#ifdef TARGET_RISCV64
    if (comp->compOpportunisticallyDependsOn(InstructionSet_Zba))
    {
        GenTree* next;
        if (TryLowerShiftAddToShxadd(node, &next))
        {
            return next;
        }
        else if (TryLowerZextAddToAddUw(node, &next))
        {
            return next;
        }
    }
#endif

    if (node->OperIs(GT_ADD))
    {
        ContainCheckBinary(node);
    }

    return nullptr;
}

//------------------------------------------------------------------------
// LowerUnsignedDivOrMod: Lowers a GT_UDIV/GT_UMOD node.
//
// Arguments:
//    divMod - pointer to the GT_UDIV/GT_UMOD node to be lowered
//
// Return Value:
//    Returns a boolean indicating whether the node was transformed.
//
// Notes:
//    - Transform UDIV/UMOD by power of 2 into RSZ/AND
//    - Transform UDIV by constant >= 2^(N-1) into GE
//    - Transform UDIV/UMOD by constant >= 3 into "magic division"
//

bool Lowering::LowerUnsignedDivOrMod(GenTreeOp* divMod)
{
    assert(divMod->OperIs(GT_UDIV, GT_UMOD));

#if defined(USE_HELPERS_FOR_INT_DIV)
    if (!varTypeIsIntegral(divMod->TypeGet()))
    {
        assert(!"unreachable: integral GT_UDIV/GT_UMOD should get morphed into helper calls");
    }
    assert(varTypeIsFloating(divMod->TypeGet()));
#endif // USE_HELPERS_FOR_INT_DIV
#if defined(TARGET_ARM64)
    assert(!divMod->OperIs(GT_UMOD));
#endif // TARGET_ARM64

    GenTree* dividend = divMod->gtGetOp1();
    GenTree* divisor  = divMod->gtGetOp2();

#if !defined(TARGET_64BIT)
    if (dividend->OperIs(GT_LONG))
    {
        return false;
    }
#endif

    if (!divisor->IsCnsIntOrI())
    {
        return false;
    }

    if (dividend->IsCnsIntOrI())
    {
        // We shouldn't see a divmod with constant operands here but if we do then it's likely
        // because optimizations are disabled or it's a case that's supposed to throw an exception.
        // Don't optimize this.
        return false;
    }

    const var_types type = divMod->TypeGet();
    assert((type == TYP_INT) || (type == TYP_I_IMPL));

    size_t divisorValue = static_cast<size_t>(divisor->AsIntCon()->IconValue());

    if (type == TYP_INT)
    {
        // Clear up the upper 32 bits of the value, they may be set to 1 because constants
        // are treated as signed and stored in ssize_t which is 64 bit in size on 64 bit targets.
        divisorValue &= UINT32_MAX;
    }

    if (divisorValue == 0)
    {
        return false;
    }

    const bool isDiv = divMod->OperIs(GT_UDIV);

    if (isPow2(divisorValue))
    {
        genTreeOps newOper;

        if (isDiv)
        {
            newOper      = GT_RSZ;
            divisorValue = genLog2(divisorValue);
        }
        else
        {
            newOper = GT_AND;
            divisorValue -= 1;
        }

        divMod->ChangeOper(newOper);
        divisor->AsIntCon()->SetIconValue(divisorValue);
        ContainCheckNode(divMod);
        return true;
    }
    if (isDiv)
    {
        // If the divisor is greater or equal than 2^(N - 1) then the result is 1
        // iff the dividend is greater or equal than the divisor.
        if (((type == TYP_INT) && (divisorValue > (UINT32_MAX / 2))) ||
            ((type == TYP_LONG) && (divisorValue > (UINT64_MAX / 2))))
        {
            divMod->ChangeOper(GT_GE);
            divMod->gtFlags |= GTF_UNSIGNED;
            ContainCheckNode(divMod);
            return true;
        }
    }

// TODO-ARM-CQ: Currently there's no GT_MULHI for ARM32
#if defined(TARGET_XARCH) || defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    if (!comp->opts.MinOpts() && (divisorValue >= 3))
    {
        size_t magic;
        bool   increment;
        int    preShift;
        int    postShift;
        bool   simpleMul = false;

        unsigned bits = type == TYP_INT ? 32 : 64;
        // if the dividend operand is AND or RSZ with a constant then the number of input bits can be reduced
        if (dividend->OperIs(GT_AND) && dividend->gtGetOp2()->IsCnsIntOrI())
        {
            size_t maskCns = static_cast<size_t>(dividend->gtGetOp2()->AsIntCon()->IconValue());
            if (maskCns != 0)
            {
                unsigned maskBits = 1;
                while (maskCns >>= 1)
                    maskBits++;
                if (maskBits < bits)
                    bits = maskBits;
            }
        }
        else if (dividend->OperIs(GT_RSZ) && dividend->gtGetOp2()->IsCnsIntOrI())
        {
            size_t shiftCns = static_cast<size_t>(dividend->gtGetOp2()->AsIntCon()->IconValue());
            if (shiftCns < bits)
            {
                bits -= static_cast<unsigned>(shiftCns);
            }
        }

        if (type == TYP_INT)
        {
            magic = MagicDivide::GetUnsigned32Magic(static_cast<uint32_t>(divisorValue), &increment, &preShift,
                                                    &postShift, bits);

#ifdef TARGET_64BIT
            // avoid inc_saturate/multiple shifts by widening to 32x64 MULHI
            if (increment || (preShift
#ifdef TARGET_XARCH
                              // IMUL reg,reg,imm32 can't be used if magic<0 because of sign-extension
                              && static_cast<int32_t>(magic) < 0
#endif
                              ))
            {
                magic = MagicDivide::GetUnsigned64Magic(static_cast<uint64_t>(divisorValue), &increment, &preShift,
                                                        &postShift, bits);
            }
            // otherwise just widen to regular multiplication
            else
            {
                postShift += 32;
                simpleMul = true;
            }
#endif
        }
        else
        {
#ifdef TARGET_64BIT
            magic = MagicDivide::GetUnsigned64Magic(static_cast<uint64_t>(divisorValue), &increment, &preShift,
                                                    &postShift, bits);
#else
            unreached();
#endif
        }

        const bool     requiresDividendMultiuse = !isDiv;
        const weight_t curBBWeight              = m_block->getBBWeight(comp);

        if (requiresDividendMultiuse)
        {
            LIR::Use dividendUse(BlockRange(), &divMod->gtOp1, divMod);
            dividend = ReplaceWithLclVar(dividendUse);
        }

        GenTree* firstNode        = nullptr;
        GenTree* adjustedDividend = dividend;

#if defined(TARGET_ARM64)
        // On ARM64 we will use a 32x32->64 bit multiply instead of a 64x64->64 one.
        bool widenToNativeIntForMul = (type != TYP_I_IMPL) && !simpleMul;
#else
        bool widenToNativeIntForMul = (type != TYP_I_IMPL);
#endif

        // If "increment" flag is returned by GetUnsignedMagic we need to do Saturating Increment first
        if (increment)
        {
            adjustedDividend = comp->gtNewOperNode(GT_INC_SATURATE, type, adjustedDividend);
            BlockRange().InsertBefore(divMod, adjustedDividend);
            firstNode = adjustedDividend;
            assert(!preShift);
        }
        // if "preShift" is required, then do a right shift before
        else if (preShift)
        {
            GenTree* preShiftBy = comp->gtNewIconNode(preShift, TYP_INT);
            adjustedDividend    = comp->gtNewOperNode(GT_RSZ, type, adjustedDividend, preShiftBy);
            BlockRange().InsertBefore(divMod, preShiftBy, adjustedDividend);
            firstNode = preShiftBy;
        }
        else if (widenToNativeIntForMul)
        {
            adjustedDividend = comp->gtNewCastNode(TYP_I_IMPL, adjustedDividend, true, TYP_I_IMPL);
            BlockRange().InsertBefore(divMod, adjustedDividend);
            firstNode = adjustedDividend;
        }

#ifdef TARGET_XARCH
        // force input transformation to RAX/RDX because the following MULHI will kill RDX:RAX (RDX if mulx is
        // available) anyway and LSRA often causes redundant copies otherwise
        if (firstNode && !simpleMul)
        {
            regNumber implicitReg = comp->compOpportunisticallyDependsOn(InstructionSet_AVX2) ? REG_RDX : REG_RAX;
            adjustedDividend->SetRegNum(implicitReg);
        }
#endif

        if (widenToNativeIntForMul)
        {
            divisor->gtType = TYP_I_IMPL;
        }
        divisor->AsIntCon()->SetIconValue(magic);

        if (isDiv && !postShift && (type == TYP_I_IMPL))
        {
            divMod->ChangeOper(GT_MULHI);
            divMod->gtOp1 = adjustedDividend;
            divMod->SetUnsigned();
        }
        else
        {
#ifdef TARGET_ARM64
            // 64-bit MUL is more expensive than UMULL on ARM64.
            genTreeOps mulOper = simpleMul ? GT_MUL_LONG : GT_MULHI;
#else
            // 64-bit IMUL is less expensive than MUL eax:edx on x64.
            genTreeOps mulOper = simpleMul ? GT_MUL : GT_MULHI;
#endif
            // Insert a new multiplication node before the existing GT_UDIV/GT_UMOD node.
            // The existing node will later be transformed into a GT_RSZ/GT_SUB that
            // computes the final result. This way don't need to find and change the use
            // of the existing node.
            GenTree* mulhi = comp->gtNewOperNode(mulOper, TYP_I_IMPL, adjustedDividend, divisor);
            mulhi->SetUnsigned();
            BlockRange().InsertBefore(divMod, mulhi);
            if (firstNode == nullptr)
            {
                firstNode = mulhi;
            }

            if (postShift)
            {
                GenTree* shiftBy = comp->gtNewIconNode(postShift, TYP_INT);
                BlockRange().InsertBefore(divMod, shiftBy);

                if (isDiv && (type == TYP_I_IMPL))
                {
                    divMod->ChangeOper(GT_RSZ);
                    divMod->gtOp1 = mulhi;
                    divMod->gtOp2 = shiftBy;
                }
                else
                {
                    mulhi = comp->gtNewOperNode(GT_RSZ, TYP_I_IMPL, mulhi, shiftBy);
                    BlockRange().InsertBefore(divMod, mulhi);
                }
            }

            if (!isDiv)
            {
                // divisor UMOD dividend = dividend SUB (div MUL divisor)
                GenTree* divisor = comp->gtNewIconNode(divisorValue, type);
                GenTree* mul     = comp->gtNewOperNode(GT_MUL, type, mulhi, divisor);
                dividend         = comp->gtNewLclvNode(dividend->AsLclVar()->GetLclNum(), dividend->TypeGet());

                divMod->ChangeOper(GT_SUB);
                divMod->gtOp1 = dividend;
                divMod->gtOp2 = mul;

                BlockRange().InsertBefore(divMod, divisor, mul, dividend);
            }
            else if (type != TYP_I_IMPL)
            {
                divMod->ChangeOper(GT_CAST);
                divMod->AsCast()->gtCastType = TYP_INT;
                divMod->gtOp1                = mulhi;
                divMod->gtOp2                = nullptr;
            }
        }

        if (firstNode != nullptr)
        {
            ContainCheckRange(firstNode, divMod);
        }

        return true;
    }
#endif
    return false;
}

// LowerConstIntDivOrMod: Transform integer GT_DIV/GT_MOD nodes with a power of 2
//     const divisor into equivalent but faster sequences.
//
// Arguments:
//    node - pointer to the DIV or MOD node
//    nextNode - out parameter for the next node in the transformed node sequence that needs to be lowered
//
// Returns:
//    false if no transformation is done, true if a transformation is done
//
bool Lowering::TryLowerConstIntDivOrMod(GenTree* node, GenTree** nextNode)
{
    assert(node->OperIs(GT_DIV) || node->OperIs(GT_MOD));
    assert(nextNode != nullptr);

    GenTree* divMod   = node;
    GenTree* dividend = divMod->gtGetOp1();
    GenTree* divisor  = divMod->gtGetOp2();

    const var_types type = divMod->TypeGet();
    assert((type == TYP_INT) || (type == TYP_LONG));

#if defined(USE_HELPERS_FOR_INT_DIV)
    assert(!"unreachable: integral GT_DIV/GT_MOD should get morphed into helper calls");
#endif // USE_HELPERS_FOR_INT_DIV
#if defined(TARGET_ARM64)
    if (divMod->OperIs(GT_MOD) && divisor->IsIntegralConstPow2())
    {
        LowerModPow2(node);
        *nextNode = node->gtNext;
        return true;
    }
    assert(!node->OperIs(GT_MOD));
#endif // TARGET_ARM64

    if (!divisor->IsCnsIntOrI())
    {
        return false; // no transformations to make
    }

    if (dividend->IsCnsIntOrI())
    {
        // We shouldn't see a divmod with constant operands here but if we do then it's likely
        // because optimizations are disabled or it's a case that's supposed to throw an exception.
        // Don't optimize this.
        return false;
    }

    ssize_t divisorValue = divisor->AsIntCon()->IconValue();

    if (divisorValue == -1 || divisorValue == 0)
    {
        // x / 0 and x % 0 can't be optimized because they are required to throw an exception.

        // x / -1 can't be optimized because INT_MIN / -1 is required to throw an exception.

        // x % -1 is always 0 and the IL spec says that the rem instruction "can" throw an exception if x is
        // the minimum representable integer. However, the C# spec says that an exception "is" thrown in this
        // case so optimizing this case would break C# code.

        // A runtime check could be used to handle this case but it's probably too rare to matter.
        return false;
    }

    bool isDiv = divMod->OperIs(GT_DIV);

    if (isDiv)
    {
        if ((type == TYP_INT && divisorValue == INT_MIN) || (type == TYP_LONG && divisorValue == INT64_MIN))
        {
            // If the divisor is the minimum representable integer value then we can use a compare,
            // the result is 1 iff the dividend equals divisor.
            divMod->ChangeOper(GT_EQ);
            *nextNode = node;
            return true;
        }
    }

    size_t absDivisorValue =
        (divisorValue == SSIZE_T_MIN) ? static_cast<size_t>(divisorValue) : static_cast<size_t>(std::abs(divisorValue));

    if (!isPow2(absDivisorValue))
    {
        if (comp->opts.MinOpts())
        {
            return false;
        }

#if defined(TARGET_XARCH) || defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
        ssize_t magic;
        int     shift;

        if (type == TYP_INT)
        {
            magic = MagicDivide::GetSigned32Magic(static_cast<int32_t>(divisorValue), &shift);
        }
        else
        {
#ifdef TARGET_64BIT
            magic = MagicDivide::GetSigned64Magic(static_cast<int64_t>(divisorValue), &shift);
#else  // !TARGET_64BIT
            unreached();
#endif // !TARGET_64BIT
        }

        divisor->AsIntConCommon()->SetIconValue(magic);

        // Insert a new GT_MULHI node in front of the existing GT_DIV/GT_MOD node.
        // The existing node will later be transformed into a GT_ADD/GT_SUB that
        // computes the final result. This way don't need to find and change the
        // use of the existing node.
        GenTree* mulhi = comp->gtNewOperNode(GT_MULHI, type, divisor, dividend);
        BlockRange().InsertBefore(divMod, mulhi);

        // mulhi was the easy part. Now we need to generate different code depending
        // on the divisor value:
        // For 3 we need:
        //     div = signbit(mulhi) + mulhi
        // For 5 we need:
        //     div = signbit(mulhi) + sar(mulhi, 1) ; requires shift adjust
        // For 7 we need:
        //     mulhi += dividend                    ; requires add adjust
        //     div = signbit(mulhi) + sar(mulhi, 2) ; requires shift adjust
        // For -3 we need:
        //     mulhi -= dividend                    ; requires sub adjust
        //     div = signbit(mulhi) + sar(mulhi, 1) ; requires shift adjust
        bool requiresAddSubAdjust     = signum(divisorValue) != signum(magic);
        bool requiresShiftAdjust      = shift != 0;
        bool requiresDividendMultiuse = requiresAddSubAdjust || !isDiv;

        if (requiresDividendMultiuse)
        {
            LIR::Use dividendUse(BlockRange(), &mulhi->AsOp()->gtOp2, mulhi);
            dividend = ReplaceWithLclVar(dividendUse);
        }

        GenTree* adjusted;

        if (requiresAddSubAdjust)
        {
            dividend = comp->gtNewLclvNode(dividend->AsLclVar()->GetLclNum(), dividend->TypeGet());
            adjusted = comp->gtNewOperNode(divisorValue > 0 ? GT_ADD : GT_SUB, type, mulhi, dividend);
            BlockRange().InsertBefore(divMod, dividend, adjusted);
        }
        else
        {
            adjusted = mulhi;
        }

        GenTree* shiftBy = comp->gtNewIconNode(genTypeSize(type) * 8 - 1, type);
        GenTree* signBit = comp->gtNewOperNode(GT_RSZ, type, adjusted, shiftBy);
        BlockRange().InsertBefore(divMod, shiftBy, signBit);

        LIR::Use adjustedUse(BlockRange(), &signBit->AsOp()->gtOp1, signBit);
        adjusted = ReplaceWithLclVar(adjustedUse);
        adjusted = comp->gtNewLclvNode(adjusted->AsLclVar()->GetLclNum(), adjusted->TypeGet());
        BlockRange().InsertBefore(divMod, adjusted);

        if (requiresShiftAdjust)
        {
            shiftBy  = comp->gtNewIconNode(shift, TYP_INT);
            adjusted = comp->gtNewOperNode(GT_RSH, type, adjusted, shiftBy);
            BlockRange().InsertBefore(divMod, shiftBy, adjusted);
        }

        if (isDiv)
        {
            divMod->ChangeOper(GT_ADD);
            divMod->AsOp()->gtOp1 = adjusted;
            divMod->AsOp()->gtOp2 = signBit;
        }
        else
        {
            GenTree* div = comp->gtNewOperNode(GT_ADD, type, adjusted, signBit);

            dividend = comp->gtNewLclvNode(dividend->AsLclVar()->GetLclNum(), dividend->TypeGet());

            // divisor % dividend = dividend - divisor x div
            GenTree* divisor = comp->gtNewIconNode(divisorValue, type);
            GenTree* mul     = comp->gtNewOperNode(GT_MUL, type, div, divisor);
            BlockRange().InsertBefore(divMod, dividend, div, divisor, mul);

            divMod->ChangeOper(GT_SUB);
            divMod->AsOp()->gtOp1 = dividend;
            divMod->AsOp()->gtOp2 = mul;
        }

        *nextNode = mulhi;
        return true;
#elif defined(TARGET_ARM)
        // Currently there's no GT_MULHI for ARM32
        return false;
#else
#error Unsupported or unset target architecture
#endif
    }

    // We're committed to the conversion now. Go find the use if any.
    LIR::Use use;
    if (!BlockRange().TryGetUse(node, &use))
    {
        return false;
    }

    // We need to use the dividend node multiple times so its value needs to be
    // computed once and stored in a temp variable.
    LIR::Use opDividend(BlockRange(), &divMod->AsOp()->gtOp1, divMod);
    dividend = ReplaceWithLclVar(opDividend);

    GenTree* adjustment = comp->gtNewOperNode(GT_RSH, type, dividend, comp->gtNewIconNode(type == TYP_INT ? 31 : 63));

    if (absDivisorValue == 2)
    {
        // If the divisor is +/-2 then we'd end up with a bitwise and between 0/-1 and 1.
        // We can get the same result by using GT_RSZ instead of GT_RSH.
        adjustment->SetOper(GT_RSZ);
    }
    else
    {
        adjustment = comp->gtNewOperNode(GT_AND, type, adjustment, comp->gtNewIconNode(absDivisorValue - 1, type));
    }

    GenTree* adjustedDividend =
        comp->gtNewOperNode(GT_ADD, type, adjustment,
                            comp->gtNewLclvNode(dividend->AsLclVar()->GetLclNum(), dividend->TypeGet()));

    GenTree* newDivMod;

    if (isDiv)
    {
        // perform the division by right shifting the adjusted dividend
        divisor->AsIntCon()->SetIconValue(genLog2(absDivisorValue));

        newDivMod = comp->gtNewOperNode(GT_RSH, type, adjustedDividend, divisor);
        ContainCheckShiftRotate(newDivMod->AsOp());

        if (divisorValue < 0)
        {
            // negate the result if the divisor is negative
            newDivMod = comp->gtNewOperNode(GT_NEG, type, newDivMod);
            ContainCheckNode(newDivMod);
        }
    }
    else
    {
        // divisor % dividend = dividend - divisor x (dividend / divisor)
        // divisor x (dividend / divisor) translates to (dividend >> log2(divisor)) << log2(divisor)
        // which simply discards the low log2(divisor) bits, that's just dividend & ~(divisor - 1)
        divisor->AsIntCon()->SetIconValue(~(absDivisorValue - 1));

        newDivMod = comp->gtNewOperNode(GT_SUB, type,
                                        comp->gtNewLclvNode(dividend->AsLclVar()->GetLclNum(), dividend->TypeGet()),
                                        comp->gtNewOperNode(GT_AND, type, adjustedDividend, divisor));
    }

    // Remove the divisor and dividend nodes from the linear order,
    // since we have reused them and will resequence the tree
    BlockRange().Remove(divisor);
    BlockRange().Remove(dividend);

    // linearize and insert the new tree before the original divMod node
    InsertTreeBeforeAndContainCheck(divMod, newDivMod);
    BlockRange().Remove(divMod);

    // replace the original divmod node with the new divmod tree
    use.ReplaceWith(newDivMod);

    *nextNode = newDivMod->gtNext;
    return true;
}
//------------------------------------------------------------------------
// LowerSignedDivOrMod: transform integer GT_DIV/GT_MOD nodes with a power of 2
// const divisor into equivalent but faster sequences.
//
// Arguments:
//    node - the DIV or MOD node
//
// Returns:
//    The next node to lower.
//
GenTree* Lowering::LowerSignedDivOrMod(GenTree* node)
{
    assert(node->OperIs(GT_DIV) || node->OperIs(GT_MOD));

    if (varTypeIsIntegral(node->TypeGet()))
    {
        GenTree* nextNode = nullptr;
        if (TryLowerConstIntDivOrMod(node, &nextNode))
        {
            return nextNode;
        }
        assert(nextNode == nullptr);
    }
    ContainCheckDivOrMod(node->AsOp());

    return node->gtNext;
}

//------------------------------------------------------------------------
// TryFoldBinop: Try removing a binop node by constant folding.
//
// Parameters:
//   node - the node
//
// Returns:
//   True if the node was removed
//
bool Lowering::TryFoldBinop(GenTreeOp* node)
{
    if (node->gtSetFlags())
    {
        return false;
    }

    GenTree* op1 = node->gtGetOp1();
    GenTree* op2 = node->gtGetOp2();

    if (op1->IsIntegralConst() && op2->IsIntegralConst())
    {
        GenTree* folded = comp->gtFoldExprConst(node);
        assert(folded == node);
        if (!folded->OperIsConst())
        {
            return false;
        }

        BlockRange().Remove(op1);
        BlockRange().Remove(op2);
        return true;
    }

    if ((node->OperIs(GT_LSH, GT_RSH, GT_RSZ, GT_ROL, GT_ROR) && op2->IsIntegralConst(0)) ||
        (node->OperIs(GT_OR, GT_XOR) && (op1->IsIntegralConst(0) || op2->IsIntegralConst(0))))
    {
        GenTree* zeroOp  = op2->IsIntegralConst(0) ? op2 : op1;
        GenTree* otherOp = zeroOp == op1 ? op2 : op1;

        LIR::Use use;
        if (BlockRange().TryGetUse(node, &use))
        {
            use.ReplaceWith(otherOp);
        }
        else
        {
            otherOp->SetUnusedValue();
        }

        BlockRange().Remove(node);
        BlockRange().Remove(zeroOp);

        return true;
    }

    return false;
}

//------------------------------------------------------------------------
// LowerShift: Lower shift nodes
//
// Arguments:
//    shift - the shift node (GT_LSH, GT_RSH or GT_RSZ)
//
// Notes:
//    Remove unnecessary shift count masking, xarch shift instructions
//    mask the shift count to 5 bits (or 6 bits for 64 bit operations).
//
void Lowering::LowerShift(GenTreeOp* shift)
{
    assert(shift->OperIs(GT_LSH, GT_RSH, GT_RSZ));

    size_t mask = 0x1f;
#ifdef TARGET_64BIT
    if (varTypeIsLong(shift->TypeGet()))
    {
        mask = 0x3f;
    }
#else
    assert(!varTypeIsLong(shift->TypeGet()));
#endif

    for (GenTree* andOp = shift->gtGetOp2(); andOp->OperIs(GT_AND); andOp = andOp->gtGetOp1())
    {
        GenTree* maskOp = andOp->gtGetOp2();

        if (!maskOp->IsCnsIntOrI())
        {
            break;
        }

        if ((static_cast<size_t>(maskOp->AsIntCon()->IconValue()) & mask) != mask)
        {
            break;
        }

        shift->gtOp2 = andOp->gtGetOp1();
        BlockRange().Remove(andOp);
        BlockRange().Remove(maskOp);
        // The parent was replaced, clear contain and regOpt flag.
        shift->gtOp2->ClearContained();
    }

    ContainCheckShiftRotate(shift);

#ifdef TARGET_ARM64
    // Try to recognize ubfiz/sbfiz idiom in LSH(CAST(X), CNS) tree
    if (comp->opts.OptimizationEnabled() && shift->OperIs(GT_LSH) && shift->gtGetOp1()->OperIs(GT_CAST) &&
        shift->gtGetOp2()->IsCnsIntOrI() && !shift->isContained())
    {
        GenTreeIntCon* cns  = shift->gtGetOp2()->AsIntCon();
        GenTreeCast*   cast = shift->gtGetOp1()->AsCast();

        if (!cast->isContained() && !cast->IsRegOptional() && !cast->gtOverflow() &&
            // Smaller CastOp is most likely an IND(X) node which is lowered to a zero-extend load
            cast->CastOp()->TypeIs(TYP_LONG, TYP_INT))
        {
            // Cast is either "TYP_LONG <- TYP_INT" or "TYP_INT <- %SMALL_INT% <- TYP_INT" (signed or unsigned)
            unsigned dstBits = genTypeSize(cast) * BITS_PER_BYTE;
            unsigned srcBits = varTypeIsSmall(cast->CastToType()) ? genTypeSize(cast->CastToType()) * BITS_PER_BYTE
                                                                  : genTypeSize(cast->CastOp()) * BITS_PER_BYTE;

            // It has to be an upcast and CNS must be in [1..srcBits) range
            if ((srcBits < dstBits) && (cns->IconValue() > 0) && (cns->IconValue() < srcBits))
            {
                JITDUMP("Recognized ubfix/sbfix pattern in LSH(CAST, CNS). Changing op to GT_BFIZ");
                shift->ChangeOper(GT_BFIZ);
                cast->CastOp()->ClearContained(); // Uncontain any memory operands.
                MakeSrcContained(shift, cast);
            }
        }
    }
#endif

#ifdef TARGET_RISCV64
    if (comp->compOpportunisticallyDependsOn(InstructionSet_Zba))
    {
        GenTree* next;
        TryLowerZextLeftShiftToSlliUw(shift, &next);
    }
#endif
}

void Lowering::WidenSIMD12IfNecessary(GenTreeLclVarCommon* node)
{
#ifdef FEATURE_SIMD
    if (node->TypeIs(TYP_SIMD12))
    {
        // Assumption 1:
        // RyuJit backend depends on the assumption that on 64-Bit targets Vector3 size is rounded off
        // to TARGET_POINTER_SIZE and hence Vector3 locals on stack can be treated as TYP_SIMD16 for
        // reading and writing purposes.
        //
        // Assumption 2:
        // RyuJit backend is making another implicit assumption that Vector3 type args when passed in
        // registers or on stack, the upper most 4-bytes will be zero.
        //
        // For P/Invoke return and Reverse P/Invoke argument passing, native compiler doesn't guarantee
        // that upper 4-bytes of a Vector3 type struct is zero initialized and hence assumption 2 is
        // invalid.
        //
        // RyuJIT x64 Windows: arguments are treated as passed by ref and hence read/written just 12
        // bytes. In case of Vector3 returns, Caller allocates a zero initialized Vector3 local and
        // passes it retBuf arg and Callee method writes only 12 bytes to retBuf. For this reason,
        // there is no need to clear upper 4-bytes of Vector3 type args.
        //
        // RyuJIT x64 Unix: arguments are treated as passed by value and read/writen as if TYP_SIMD16.
        // Vector3 return values are returned two return registers and Caller assembles them into a
        // single xmm reg. Hence RyuJIT explicitly generates code to clears upper 4-bytes of Vector3
        // type args in prolog and Vector3 type return value of a call
        //
        // RyuJIT x86 Windows: all non-param Vector3 local vars are allocated as 16 bytes. Vector3 arguments
        // are pushed as 12 bytes. For return values, a 16-byte local is allocated and the address passed
        // as a return buffer pointer. The callee doesn't write the high 4 bytes, and we don't need to clear
        // it either.

        if (comp->lvaMapSimd12ToSimd16(node->AsLclVarCommon()->GetLclNum()))
        {
            JITDUMP("Mapping TYP_SIMD12 lclvar node to TYP_SIMD16:\n");
            DISPNODE(node);
            JITDUMP("============\n");

            node->gtType = TYP_SIMD16;
        }
    }
#endif // FEATURE_SIMD
}

PhaseStatus Lowering::DoPhase()
{
    // If we have any PInvoke calls, insert the one-time prolog code. We'll insert the epilog code in the
    // appropriate spots later. NOTE: there is a minor optimization opportunity here, as we still create p/invoke
    // data structures and setup/teardown even if we've eliminated all p/invoke calls due to dead code elimination.
    if (comp->compMethodRequiresPInvokeFrame())
    {
        InsertPInvokeMethodProlog();
    }

#if !defined(TARGET_64BIT)
    DecomposeLongs decomp(comp, this); // Initialize the long decomposition class.
    if (comp->compLongUsed)
    {
        decomp.PrepareForDecomposition();
    }
#endif // !defined(TARGET_64BIT)

    if (!comp->compEnregLocals())
    {
        // Lowering is checking if lvDoNotEnregister is already set for contained optimizations.
        // If we are running without `CLFLG_REGVAR` flag set (`compEnregLocals() == false`)
        // then we already know that we won't enregister any locals and it is better to set
        // `lvDoNotEnregister` flag before we start reading it.
        // The main reason why this flag is not set is that we are running in minOpts.
        comp->lvSetMinOptsDoNotEnreg();
    }

    if (comp->opts.OptimizationEnabled() && !comp->opts.IsOSR())
    {
        MapParameterRegisterLocals();
    }

    for (BasicBlock* const block : comp->Blocks())
    {
        /* Make the block publicly available */
        comp->compCurBB = block;

#if !defined(TARGET_64BIT)
        if (comp->compLongUsed)
        {
            decomp.DecomposeBlock(block);
        }
#endif //! TARGET_64BIT

        LowerBlock(block);
    }

#ifdef DEBUG
    JITDUMP("Lower has completed modifying nodes.\n");
    if (VERBOSE)
    {
        comp->fgDispBasicBlocks(true);
    }
#endif

    // Recompute local var ref counts before potentially sorting for liveness.
    // Note this does minimal work in cases where we are not going to sort.
    const bool isRecompute    = true;
    const bool setSlotNumbers = false;
    comp->lvaComputeRefCounts(isRecompute, setSlotNumbers);

    if (comp->m_dfsTree == nullptr)
    {
        // Compute DFS tree. We want to remove dead blocks even in MinOpts, so we
        // do this everywhere. The dead blocks are removed below, however, some of
        // lowering may use the DFS tree, so we compute that here.
        comp->m_dfsTree = comp->fgComputeDfs();
    }

    // Remove dead blocks. We want to remove unreachable blocks even in
    // MinOpts.
    comp->fgRemoveBlocksOutsideDfsTree();

    if (comp->backendRequiresLocalVarLifetimes())
    {
        assert(comp->opts.OptimizationEnabled());

        comp->fgLocalVarLiveness();
        // local var liveness can delete code, which may create empty blocks
        bool modified = comp->fgUpdateFlowGraph(/* doTailDuplication */ false, /* isPhase */ false);

        if (modified)
        {
            comp->fgDfsBlocksAndRemove();
            JITDUMP("had to run another liveness pass:\n");
            comp->fgLocalVarLiveness();
        }

        // Recompute local var ref counts again after liveness to reflect
        // impact of any dead code removal. Note this may leave us with
        // tracked vars that have zero refs.
        comp->lvaComputeRefCounts(isRecompute, setSlotNumbers);
    }

    comp->fgInvalidateDfsTree();

    return PhaseStatus::MODIFIED_EVERYTHING;
}

//------------------------------------------------------------------------
// Lowering::MapParameterRegisterLocals:
//   Create mappings between parameter registers and locals corresponding to
//   them.
//
void Lowering::MapParameterRegisterLocals()
{
    comp->m_paramRegLocalMappings =
        new (comp, CMK_ABI) ArrayStack<ParameterRegisterLocalMapping>(comp->getAllocator(CMK_ABI));

    // Create initial mappings for promotions.
    for (unsigned lclNum = 0; lclNum < comp->info.compArgsCount; lclNum++)
    {
        LclVarDsc*                   lclDsc  = comp->lvaGetDesc(lclNum);
        const ABIPassingInformation& abiInfo = comp->lvaGetParameterABIInfo(lclNum);

        if (comp->lvaGetPromotionType(lclDsc) != Compiler::PROMOTION_TYPE_INDEPENDENT)
        {
            // If not promoted, then we do not need to create any mappings.
            // If dependently promoted then the fields are never enregistered
            // by LSRA, so no reason to try to create any mappings.
            continue;
        }

        if (!abiInfo.HasAnyRegisterSegment())
        {
            // If the parameter is not passed in any registers, then there are
            // no mappings to create.
            continue;
        }

        // Currently we do not support promotion of split parameters, so we
        // should not see any split parameters here.
        assert(!abiInfo.IsSplitAcrossRegistersAndStack());

        for (int i = 0; i < lclDsc->lvFieldCnt; i++)
        {
            unsigned   fieldLclNum = lclDsc->lvFieldLclStart + i;
            LclVarDsc* fieldDsc    = comp->lvaGetDesc(fieldLclNum);

            for (const ABIPassingSegment& segment : abiInfo.Segments())
            {
                if (segment.Offset + segment.Size <= fieldDsc->lvFldOffset)
                {
                    // This register does not map to this field (ends before the field starts)
                    continue;
                }

                if (fieldDsc->lvFldOffset + fieldDsc->lvExactSize() <= segment.Offset)
                {
                    // This register does not map to this field (starts after the field ends)
                    continue;
                }

                comp->m_paramRegLocalMappings->Emplace(&segment, fieldLclNum, segment.Offset - fieldDsc->lvFldOffset);
            }

            assert(!fieldDsc->lvIsParamRegTarget);
            fieldDsc->lvIsParamRegTarget = true;
        }
    }

    FindInducedParameterRegisterLocals();

#ifdef DEBUG
    if (comp->verbose)
    {
        printf("%d parameter register to local mappings\n", comp->m_paramRegLocalMappings->Height());
        for (int i = 0; i < comp->m_paramRegLocalMappings->Height(); i++)
        {
            const ParameterRegisterLocalMapping& mapping = comp->m_paramRegLocalMappings->BottomRef(i);
            printf("  %s -> V%02u+%u\n", getRegName(mapping.RegisterSegment->GetRegister()), mapping.LclNum,
                   mapping.Offset);
        }
    }
#endif
}

//------------------------------------------------------------------------
// Lowering::FindInducedParameterRegisterLocals:
//   Find locals that would be profitable to map from parameter registers,
//   based on IR in the initialization block.
//
void Lowering::FindInducedParameterRegisterLocals()
{
#ifdef TARGET_ARM
    // On arm32 the profiler hook does not preserve arg registers, so
    // parameters are prespilled and cannot stay enregistered.
    if (comp->compIsProfilerHookNeeded())
    {
        JITDUMP("Skipping FindInducedParameterRegisterLocals on arm32 with profiler hook\n");
        return;
    }
#endif

    // Check if we possibly have any parameters we can induce new register
    // locals from.
    bool anyCandidates = false;
    for (unsigned lclNum = 0; lclNum < comp->info.compArgsCount; lclNum++)
    {
        LclVarDsc* lcl = comp->lvaGetDesc(lclNum);
        if (lcl->lvPromoted || !lcl->lvDoNotEnregister)
        {
            continue;
        }

        const ABIPassingInformation& abiInfo = comp->lvaGetParameterABIInfo(lclNum);
        if (!abiInfo.HasAnyRegisterSegment())
        {
            continue;
        }

        anyCandidates = true;
        break;
    }

    if (!anyCandidates)
    {
        return;
    }

    bool     hasRegisterKill = false;
    LocalSet storedToLocals(comp->getAllocator(CMK_ABI));
    // Now look for optimization opportunities in the first block: places where
    // we read fields out of struct parameters that can be mapped cleanly. This
    // is frequently created by physical promotion.
    for (GenTree* node : LIR::AsRange(comp->fgFirstBB))
    {
        hasRegisterKill |= node->IsCall();

        auto visitDefs = [&](GenTreeLclVarCommon* lcl) {
            storedToLocals.Emplace(lcl->GetLclNum(), true);
            return GenTree::VisitResult::Continue;
        };

        node->VisitLocalDefNodes(comp, visitDefs);

        if (node->OperIs(GT_LCL_ADDR))
        {
            // Model these as stored to, since we cannot reason about them in the same way.
            storedToLocals.Emplace(node->AsLclVarCommon()->GetLclNum(), true);
            continue;
        }

        if (!node->OperIs(GT_LCL_FLD))
        {
            continue;
        }

        GenTreeLclFld* fld = node->AsLclFld();
        if (fld->GetLclNum() >= comp->info.compArgsCount)
        {
            continue;
        }

        LclVarDsc* paramDsc = comp->lvaGetDesc(fld);
        if (paramDsc->lvPromoted)
        {
            // These are complicated to reason about since they may be
            // defined/used through their fields, so just skip them.
            continue;
        }

        if (fld->TypeIs(TYP_STRUCT))
        {
            continue;
        }

        if (storedToLocals.Lookup(fld->GetLclNum()))
        {
            // LCL_FLD does not necessarily take the value of the parameter
            // anymore.
            continue;
        }

        const ABIPassingInformation& dataAbiInfo = comp->lvaGetParameterABIInfo(fld->GetLclNum());
        const ABIPassingSegment*     regSegment  = nullptr;
        for (const ABIPassingSegment& segment : dataAbiInfo.Segments())
        {
            if (!segment.IsPassedInRegister())
            {
                continue;
            }

            assert(fld->GetLclOffs() <= comp->lvaLclExactSize(fld->GetLclNum()));
            unsigned structAccessedSize =
                min(genTypeSize(fld), comp->lvaLclExactSize(fld->GetLclNum()) - fld->GetLclOffs());
            if ((segment.Offset != fld->GetLclOffs()) || (structAccessedSize > segment.Size) ||
                (varTypeUsesIntReg(fld) != genIsValidIntReg(segment.GetRegister())))
            {
                continue;
            }

            // This is a match, but check if it is already remapped.
            // TODO-CQ: If it is already remapped, we can reuse the value from
            // the remapping.
            if (comp->FindParameterRegisterLocalMappingByRegister(segment.GetRegister()) == nullptr)
            {
                regSegment = &segment;
            }

            break;
        }

        if (regSegment == nullptr)
        {
            continue;
        }

        JITDUMP("LCL_FLD use [%06u] of unenregisterable parameter corresponds to ", Compiler::dspTreeID(fld));
        DBEXEC(VERBOSE, regSegment->Dump());
        JITDUMP("\n");

        // Now see if we want to introduce a new local for this value, or if we
        // can reuse one because this is the source of a store (frequently
        // created by physical promotion).
        LIR::Use use;
        if (!LIR::AsRange(comp->fgFirstBB).TryGetUse(fld, &use))
        {
            JITDUMP("  ..but no use was found\n");
            continue;
        }

        unsigned remappedLclNum = TryReuseLocalForParameterAccess(use, storedToLocals);

        if (remappedLclNum == BAD_VAR_NUM)
        {
            // If we have seen register kills then avoid creating a new local.
            // The local is going to have to move from the parameter register
            // into a callee saved register, and the callee saved register will
            // need to be saved/restored to/from stack anyway.
            if (hasRegisterKill)
            {
                JITDUMP("  ..but use happens after a call and is deemed unprofitable to create a local for\n");
                continue;
            }

            remappedLclNum = comp->lvaGrabTemp(
                false DEBUGARG(comp->printfAlloc("V%02u.%s", fld->GetLclNum(), getRegName(regSegment->GetRegister()))));
            comp->lvaGetDesc(remappedLclNum)->lvType = fld->TypeGet();
            JITDUMP("Created new local V%02u for the mapping\n", remappedLclNum);
        }
        else
        {
            JITDUMP("Reusing local V%02u for store from struct parameter register %s. Store:\n", remappedLclNum,
                    getRegName(regSegment->GetRegister()));
            DISPTREERANGE(LIR::AsRange(comp->fgFirstBB), use.User());

            // The store will be a no-op, so get rid of it
            LIR::AsRange(comp->fgFirstBB).Remove(use.User(), true);
            use = LIR::Use();

#ifdef DEBUG
            LclVarDsc* remappedLclDsc = comp->lvaGetDesc(remappedLclNum);
            remappedLclDsc->lvReason =
                comp->printfAlloc("%s%sV%02u.%s", remappedLclDsc->lvReason == nullptr ? "" : remappedLclDsc->lvReason,
                                  remappedLclDsc->lvReason == nullptr ? "" : " ", fld->GetLclNum(),
                                  getRegName(regSegment->GetRegister()));
#endif
        }

        comp->m_paramRegLocalMappings->Emplace(regSegment, remappedLclNum, 0);
        comp->lvaGetDesc(remappedLclNum)->lvIsParamRegTarget = true;

        JITDUMP("New mapping: ");
        DBEXEC(VERBOSE, regSegment->Dump());
        JITDUMP(" -> V%02u\n", remappedLclNum);

        // Insert explicit normalization for small types (the LCL_FLD we
        // are replacing comes with this normalization).
        if (varTypeIsSmall(fld))
        {
            GenTree* lcl                = comp->gtNewLclvNode(remappedLclNum, genActualType(fld));
            GenTree* normalizeLcl       = comp->gtNewCastNode(TYP_INT, lcl, false, fld->TypeGet());
            GenTree* storeNormalizedLcl = comp->gtNewStoreLclVarNode(remappedLclNum, normalizeLcl);
            LIR::AsRange(comp->fgFirstBB).InsertAtBeginning(LIR::SeqTree(comp, storeNormalizedLcl));

            JITDUMP("Parameter normalization:\n");
            DISPTREERANGE(LIR::AsRange(comp->fgFirstBB), storeNormalizedLcl);
        }

        // If we still have a valid use, then replace the LCL_FLD with a
        // LCL_VAR of the remapped parameter register local.
        if (use.IsInitialized())
        {
            GenTree* lcl = comp->gtNewLclvNode(remappedLclNum, genActualType(fld));
            LIR::AsRange(comp->fgFirstBB).InsertAfter(fld, lcl);
            use.ReplaceWith(lcl);
            LowerNode(lcl);
            JITDUMP("New user tree range:\n");
            DISPTREERANGE(LIR::AsRange(comp->fgFirstBB), use.User());
        }

        fld->gtBashToNOP();
    }
}

//------------------------------------------------------------------------
// Lowering::TryReuseLocalForParameterAccess:
//   Try to figure out if a LCL_FLD that corresponds to a parameter register is
//   being stored directly to a LCL_VAR, and in that case whether it would be
//   profitable to reuse that local as the parameter register.
//
// Parameters:
//   use - The use of the LCL_FLD
//   storedToLocals - Map of locals that have had potential definitions to them
//                    up until the use
//
// Returns:
//   The local number to reuse, or BAD_VAR_NUM to create a new local instead.
//
unsigned Lowering::TryReuseLocalForParameterAccess(const LIR::Use& use, const LocalSet& storedToLocals)
{
    GenTree* useNode = use.User();

    if (!useNode->OperIs(GT_STORE_LCL_VAR))
    {
        return BAD_VAR_NUM;
    }

    LclVarDsc* destLclDsc = comp->lvaGetDesc(useNode->AsLclVarCommon());

    if (destLclDsc->lvIsParam || destLclDsc->lvIsParamRegTarget)
    {
        return BAD_VAR_NUM;
    }

    if (destLclDsc->lvIsStructField)
    {
        return BAD_VAR_NUM;
    }

    if (destLclDsc->TypeIs(TYP_STRUCT))
    {
        return BAD_VAR_NUM;
    }

    if (destLclDsc->lvDoNotEnregister)
    {
        return BAD_VAR_NUM;
    }

    if (storedToLocals.Lookup(useNode->AsLclVarCommon()->GetLclNum()))
    {
        // Destination may change value before this access
        return BAD_VAR_NUM;
    }

    return useNode->AsLclVarCommon()->GetLclNum();
}

#ifdef DEBUG

//------------------------------------------------------------------------
// Lowering::CheckCallArg: check that a call argument is in an expected
//                         form after lowering.
//
// Arguments:
//   arg - the argument to check.
//
void Lowering::CheckCallArg(GenTree* arg)
{
    if (!arg->IsValue() && !arg->OperIsPutArgStk())
    {
        assert(arg->OperIsStore());
        return;
    }

    switch (arg->OperGet())
    {
        case GT_FIELD_LIST:
        {
            GenTreeFieldList* list = arg->AsFieldList();
            assert(list->isContained());

            for (GenTreeFieldList::Use& use : list->Uses())
            {
                assert(use.GetNode()->OperIsPutArg());
            }
        }
        break;

        default:
            assert(arg->OperIsPutArg());
            break;
    }
}

//------------------------------------------------------------------------
// Lowering::CheckCall: check that a call is in an expected form after
//                      lowering. Currently this amounts to checking its
//                      arguments, but could be expanded to verify more
//                      properties in the future.
//
// Arguments:
//   call - the call to check.
//
void Lowering::CheckCall(GenTreeCall* call)
{
    for (CallArg& arg : call->gtArgs.EarlyArgs())
    {
        CheckCallArg(arg.GetEarlyNode());
    }

    for (CallArg& arg : call->gtArgs.LateArgs())
    {
        CheckCallArg(arg.GetLateNode());
    }
}

//------------------------------------------------------------------------
// Lowering::CheckNode: check that an LIR node is in an expected form
//                      after lowering.
//
// Arguments:
//   compiler - the compiler context.
//   node - the node to check.
//
void Lowering::CheckNode(Compiler* compiler, GenTree* node)
{
    switch (node->OperGet())
    {
        case GT_CALL:
            CheckCall(node->AsCall());
            break;

#ifdef FEATURE_SIMD
        case GT_HWINTRINSIC:
            assert(!node->TypeIs(TYP_SIMD12));
            break;
#endif // FEATURE_SIMD

        case GT_LCL_VAR:
        case GT_STORE_LCL_VAR:
        {
            const LclVarDsc* varDsc = compiler->lvaGetDesc(node->AsLclVar());
#if defined(FEATURE_SIMD) && defined(TARGET_64BIT)
            if (node->TypeIs(TYP_SIMD12))
            {
                assert(compiler->lvaIsFieldOfDependentlyPromotedStruct(varDsc) ||
                       (compiler->lvaLclStackHomeSize(node->AsLclVar()->GetLclNum()) == 12));
            }
#endif // FEATURE_SIMD && TARGET_64BIT
            if (varDsc->lvPromoted)
            {
                assert(varDsc->lvDoNotEnregister || (node->OperIs(GT_STORE_LCL_VAR) && varDsc->lvIsMultiRegDest));
            }
        }
        break;

        case GT_LCL_ADDR:
        {
            const GenTreeLclVarCommon* lclVarAddr = node->AsLclVarCommon();
            const LclVarDsc*           varDsc     = compiler->lvaGetDesc(lclVarAddr);
            if (((lclVarAddr->gtFlags & GTF_VAR_DEF) != 0) && varDsc->HasGCPtr())
            {
                // Emitter does not correctly handle live updates for LCL_ADDR
                // when they are not contained, for example, `STOREIND byref(GT_LCL_ADDR not-contained)`
                // would generate:
                // add     r1, sp, 48   // r1 contains address of a lclVar V01.
                // str     r0, [r1]     // a gc ref becomes live in V01, but emitter would not report it.
                // Make sure that we use uncontained address nodes only for variables
                // that will be marked as mustInit and will be alive throughout the whole block even when tracked.
                assert(lclVarAddr->isContained() || !varDsc->lvTracked || varTypeIsStruct(varDsc));
                // TODO: support this assert for uses, see https://github.com/dotnet/runtime/issues/51900.
            }

            assert(varDsc->lvDoNotEnregister);
            break;
        }

        case GT_PHI:
        case GT_PHI_ARG:
            assert(!"Should not see phi nodes after rationalize");
            break;

        case GT_LCL_FLD:
        case GT_STORE_LCL_FLD:
        {
            const LclVarDsc* varDsc = compiler->lvaGetDesc(node->AsLclFld());
            assert(varDsc->lvDoNotEnregister);
        }
        break;

        default:
            break;
    }
}

//------------------------------------------------------------------------
// Lowering::CheckBlock: check that the contents of an LIR block are in an
//                       expected form after lowering.
//
// Arguments:
//   compiler - the compiler context.
//   block    - the block to check.
//
bool Lowering::CheckBlock(Compiler* compiler, BasicBlock* block)
{
    assert(block->isEmpty() || block->IsLIR());

    LIR::Range& blockRange = LIR::AsRange(block);
    for (GenTree* node : blockRange)
    {
        CheckNode(compiler, node);
    }

    assert(blockRange.CheckLIR(compiler, true));
    return true;
}
#endif

//------------------------------------------------------------------------
// Lowering::LowerBlock: Lower all the nodes in a BasicBlock
//
// Arguments:
//   block    - the block to lower.
//
void Lowering::LowerBlock(BasicBlock* block)
{
    assert(block == comp->compCurBB); // compCurBB must already be set.
    assert(block->isEmpty() || block->IsLIR());

    m_block = block;
#ifdef TARGET_ARM64
    m_blockIndirs.Reset();
    m_ffrTrashed = true;
#endif

    // NOTE: some of the lowering methods insert calls before the node being
    // lowered (See e.g. InsertPInvoke{Method,Call}{Prolog,Epilog}). In
    // general, any code that is inserted before the current node should be
    // "pre-lowered" as they won't be subject to further processing.
    // Lowering::CheckBlock() runs some extra checks on call arguments in
    // order to help catch unlowered nodes.

    GenTree* node = BlockRange().FirstNode();
    while (node != nullptr)
    {
        node = LowerNode(node);
    }

    assert(CheckBlock(comp, block));
}

/** Verifies if both of these trees represent the same indirection.
 * Used by Lower to annotate if CodeGen generate an instruction of the
 * form *addrMode BinOp= expr
 *
 * Preconditions: both trees are children of GT_INDs and their underlying children
 * have the same gtOper.
 *
 * This is a first iteration to actually recognize trees that can be code-generated
 * as a single read-modify-write instruction on AMD64/x86.  For now
 * this method only supports the recognition of simple addressing modes (through GT_LEA)
 * or local var indirections.  Local fields, array access and other more complex nodes are
 * not yet supported.
 *
 * TODO-CQ:  Perform tree recognition by using the Value Numbering Package, that way we can recognize
 * arbitrary complex trees and support much more addressing patterns.
 */
bool Lowering::IndirsAreEquivalent(GenTree* candidate, GenTree* storeInd)
{
    assert(candidate->OperIs(GT_IND));
    assert(storeInd->OperIs(GT_STOREIND));

    // We should check the size of the indirections.  If they are
    // different, say because of a cast, then we can't call them equivalent.  Doing so could cause us
    // to drop a cast.
    // Signed-ness difference is okay and expected since a store indirection must always
    // be signed based on the CIL spec, but a load could be unsigned.
    if (genTypeSize(candidate->gtType) != genTypeSize(storeInd->gtType))
    {
        return false;
    }

    GenTree* pTreeA = candidate->gtGetOp1();
    GenTree* pTreeB = storeInd->gtGetOp1();

    // This method will be called by codegen (as well as during lowering).
    // After register allocation, the sources may have been spilled and reloaded
    // to a different register, indicated by an inserted GT_RELOAD node.
    pTreeA = pTreeA->gtSkipReloadOrCopy();
    pTreeB = pTreeB->gtSkipReloadOrCopy();

    genTreeOps oper;

    if (pTreeA->OperGet() != pTreeB->OperGet())
    {
        return false;
    }

    oper = pTreeA->OperGet();
    switch (oper)
    {
        case GT_LCL_ADDR:
            if (pTreeA->AsLclFld()->GetLclOffs() != 0)
            {
                // TODO-CQ: support arbitrary local addresses here.
                return false;
            }
            FALLTHROUGH;

        case GT_LCL_VAR:
        case GT_CNS_INT:
            return NodesAreEquivalentLeaves(pTreeA, pTreeB);

        case GT_LEA:
        {
            GenTreeAddrMode* gtAddr1 = pTreeA->AsAddrMode();
            GenTreeAddrMode* gtAddr2 = pTreeB->AsAddrMode();
            return NodesAreEquivalentLeaves(gtAddr1->Base(), gtAddr2->Base()) &&
                   NodesAreEquivalentLeaves(gtAddr1->Index(), gtAddr2->Index()) &&
                   (gtAddr1->gtScale == gtAddr2->gtScale) && (gtAddr1->Offset() == gtAddr2->Offset());
        }
        default:
            // We don't handle anything that is not either a constant,
            // a local var or LEA.
            return false;
    }
}

//------------------------------------------------------------------------
// NodesAreEquivalentLeaves: Check whether the two given nodes are the same leaves.
//
// Arguments:
//      tree1 and tree2 are nodes to be checked.
// Return Value:
//    Returns true if they are same leaves, false otherwise.
//
// static
bool Lowering::NodesAreEquivalentLeaves(GenTree* tree1, GenTree* tree2)
{
    if (tree1 == tree2)
    {
        return true;
    }

    if (tree1 == nullptr || tree2 == nullptr)
    {
        return false;
    }

    tree1 = tree1->gtSkipReloadOrCopy();
    tree2 = tree2->gtSkipReloadOrCopy();

    if (tree1->TypeGet() != tree2->TypeGet())
    {
        return false;
    }

    if (tree1->OperGet() != tree2->OperGet())
    {
        return false;
    }

    if (!tree1->OperIsLeaf() || !tree2->OperIsLeaf())
    {
        return false;
    }

    switch (tree1->OperGet())
    {
        case GT_CNS_INT:
            return tree1->AsIntCon()->IconValue() == tree2->AsIntCon()->IconValue() &&
                   tree1->IsIconHandle() == tree2->IsIconHandle();
        case GT_LCL_ADDR:
            if (tree1->AsLclFld()->GetLclOffs() != tree2->AsLclFld()->GetLclOffs())
            {
                return false;
            }
            FALLTHROUGH;
        case GT_LCL_VAR:
            return tree1->AsLclVarCommon()->GetLclNum() == tree2->AsLclVarCommon()->GetLclNum();
        default:
            return false;
    }
}

//------------------------------------------------------------------------
// Lowering::CheckMultiRegLclVar: Check whether a MultiReg GT_LCL_VAR node can
//                                remain a multi-reg.
//
// Arguments:
//   lclNode       - the GT_LCL_VAR or GT_STORE_LCL_VAR node.
//   registerCount - use register count for uses; source register count for stores.
//
bool Lowering::CheckMultiRegLclVar(GenTreeLclVar* lclNode, int registerCount)
{
    bool canEnregisterAsMultiReg  = false;
    bool canEnregisterAsSingleReg = false;

#if FEATURE_MULTIREG_RET || defined(FEATURE_HW_INTRINSICS)
    LclVarDsc* varDsc = comp->lvaGetDesc(lclNode->GetLclNum());
    if (varDsc->lvDoNotEnregister)
    {
        assert(!lclNode->IsMultiReg());
        return false;
    }

    if ((comp->lvaEnregMultiRegVars) && varDsc->lvPromoted)
    {
        // We can enregister if we have a promoted struct and all the fields' types match the ABI requirements.
        // Note that we don't promote structs with explicit layout, so we don't need to check field offsets, and
        // if we have multiple types packed into a single register, we won't have matching reg and field counts,
        // so we can tolerate mismatches of integer size.
        if (comp->lvaGetPromotionType(varDsc) == Compiler::PROMOTION_TYPE_INDEPENDENT)
        {
            if (registerCount == varDsc->lvFieldCnt)
            {
                canEnregisterAsMultiReg = true;

#ifdef FEATURE_SIMD
                // TYP_SIMD12 breaks the above invariant that "we won't have
                // matching reg and field counts"; for example, consider
                //
                // * STORE_LCL_VAR<struct{Vector3, int}>(CALL)
                // * RETURN(LCL_VAR<struct{Vector3, int}>)
                //
                // These return in two GPR registers, while the fields of the
                // local are stored in SIMD and GPR register, so registerCount
                // == varDsc->lvFieldCnt == 2. But the backend cannot handle
                // this.

                for (int i = 0; i < varDsc->lvFieldCnt; i++)
                {
                    if (comp->lvaGetDesc(varDsc->lvFieldLclStart + i)->TypeIs(TYP_SIMD12))
                    {
                        canEnregisterAsMultiReg = false;
                        break;
                    }
                }
#endif
            }
        }
    }
    else
    {
        canEnregisterAsSingleReg = varTypeIsSIMD(lclNode);
#ifdef TARGET_XARCH
        if (lclNode->OperIs(GT_STORE_LCL_VAR) && varTypeIsStruct(lclNode->Data()) && !lclNode->Data()->OperIs(GT_CALL))
        {
            canEnregisterAsSingleReg = false;
        }
#endif // TARGET_XARCH
    }

    if (canEnregisterAsSingleReg || canEnregisterAsMultiReg)
    {
        if (canEnregisterAsMultiReg)
        {
            lclNode->SetMultiReg();
        }
    }
    else
    {
        comp->lvaSetVarDoNotEnregister(lclNode->GetLclNum() DEBUGARG(DoNotEnregisterReason::BlockOp));
    }
#endif // FEATURE_MULTIREG_RET || defined(FEATURE_HW_INTRINSICS)

    return canEnregisterAsSingleReg || canEnregisterAsMultiReg;
}

//------------------------------------------------------------------------
// Containment Analysis
//------------------------------------------------------------------------
void Lowering::ContainCheckNode(GenTree* node)
{
    switch (node->gtOper)
    {
        case GT_STORE_LCL_VAR:
        case GT_STORE_LCL_FLD:
            ContainCheckStoreLoc(node->AsLclVarCommon());
            break;

        case GT_EQ:
        case GT_NE:
        case GT_LT:
        case GT_LE:
        case GT_GE:
        case GT_GT:
        case GT_TEST_EQ:
        case GT_TEST_NE:
        case GT_CMP:
        case GT_TEST:
        case GT_JCMP:
            ContainCheckCompare(node->AsOp());
            break;

        case GT_SELECT:
            ContainCheckSelect(node->AsConditional());
            break;

        case GT_ADD:
        case GT_SUB:
#if !defined(TARGET_64BIT)
        case GT_ADD_LO:
        case GT_ADD_HI:
        case GT_SUB_LO:
        case GT_SUB_HI:
#endif
        case GT_AND:
        case GT_OR:
        case GT_XOR:
            ContainCheckBinary(node->AsOp());
            break;

#if defined(TARGET_X86)
        case GT_MUL_LONG:
#endif
        case GT_MUL:
        case GT_MULHI:
            ContainCheckMul(node->AsOp());
            break;
        case GT_DIV:
        case GT_MOD:
        case GT_UDIV:
        case GT_UMOD:
            ContainCheckDivOrMod(node->AsOp());
            break;
        case GT_LSH:
        case GT_RSH:
        case GT_RSZ:
        case GT_ROL:
        case GT_ROR:
#ifndef TARGET_64BIT
        case GT_LSH_HI:
        case GT_RSH_LO:
#endif
            ContainCheckShiftRotate(node->AsOp());
            break;
        case GT_CAST:
            ContainCheckCast(node->AsCast());
            break;
        case GT_BITCAST:
            ContainCheckBitCast(node->AsUnOp());
            break;
        case GT_LCLHEAP:
            ContainCheckLclHeap(node->AsOp());
            break;
        case GT_RETURN:
            ContainCheckRet(node->AsOp());
            break;
        case GT_RETURNTRAP:
            ContainCheckReturnTrap(node->AsOp());
            break;
        case GT_STOREIND:
            ContainCheckStoreIndir(node->AsStoreInd());
            break;
        case GT_IND:
            ContainCheckIndir(node->AsIndir());
            break;
        case GT_PUTARG_REG:
        case GT_PUTARG_STK:
            // The regNum must have been set by the lowering of the call.
            assert(node->GetRegNum() != REG_NA);
            break;
#ifdef TARGET_XARCH
        case GT_INTRINSIC:
            ContainCheckIntrinsic(node->AsOp());
            break;
#endif // TARGET_XARCH
#ifdef FEATURE_HW_INTRINSICS
        case GT_HWINTRINSIC:
            ContainCheckHWIntrinsic(node->AsHWIntrinsic());
            break;
#endif // FEATURE_HW_INTRINSICS
        default:
            break;
    }
}

//------------------------------------------------------------------------
// ContainCheckReturnTrap: determine whether the source of a RETURNTRAP should be contained.
//
// Arguments:
//    node - pointer to the GT_RETURNTRAP node
//
void Lowering::ContainCheckReturnTrap(GenTreeOp* node)
{
#ifdef TARGET_XARCH
    assert(node->OperIs(GT_RETURNTRAP));
    // This just turns into a compare of its child with an int + a conditional call
    if (node->gtOp1->isIndir())
    {
        MakeSrcContained(node, node->gtOp1);
    }
#endif // TARGET_XARCH
}

//------------------------------------------------------------------------
// ContainCheckLclHeap: determine whether the source of a GT_LCLHEAP node should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckLclHeap(GenTreeOp* node)
{
    assert(node->OperIs(GT_LCLHEAP));
    GenTree* size = node->gtOp1;
    if (size->IsCnsIntOrI())
    {
        MakeSrcContained(node, size);
    }
}

//------------------------------------------------------------------------
// ContainCheckRet: determine whether the source of a node should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckRet(GenTreeUnOp* ret)
{
    assert(ret->OperIs(GT_RETURN, GT_SWIFT_ERROR_RET));

#if !defined(TARGET_64BIT)
    if (ret->TypeIs(TYP_LONG))
    {
        GenTree* op1 = ret->AsOp()->GetReturnValue();
        noway_assert(op1->OperIs(GT_LONG));
        MakeSrcContained(ret, op1);
    }
#endif // !defined(TARGET_64BIT)
#if FEATURE_MULTIREG_RET
    if (ret->TypeIs(TYP_STRUCT))
    {
        GenTree* op1 = ret->AsOp()->GetReturnValue();
        // op1 must be either a lclvar or a multi-reg returning call
        if (op1->OperIs(GT_LCL_VAR))
        {
            const LclVarDsc* varDsc = comp->lvaGetDesc(op1->AsLclVarCommon());
            // This must be a multi-reg return or an HFA of a single element.
            assert(varDsc->lvIsMultiRegRet);

            // Mark var as contained if not enregisterable.
            if (!varDsc->IsEnregisterableLcl())
            {
                if (!op1->IsMultiRegLclVar())
                {
                    MakeSrcContained(ret, op1);
                }
            }
        }
    }
#endif // FEATURE_MULTIREG_RET
}

//------------------------------------------------------------------------
// TryRemoveCast:
//   Try to remove a cast node by changing its operand.
//
// Arguments:
//    node - Cast node
//
// Returns:
//   True if the cast was removed.
//
bool Lowering::TryRemoveCast(GenTreeCast* node)
{
    if (comp->opts.OptimizationDisabled())
    {
        return false;
    }

    if (node->gtOverflow())
    {
        return false;
    }

    GenTree* op = node->CastOp();
    if (!op->OperIsConst())
    {
        return false;
    }

    GenTree* folded = comp->gtFoldExprConst(node);
    assert(folded == node);
    if (folded->OperIs(GT_CAST))
    {
        return false;
    }

    op->SetUnusedValue();
    return true;
}

//------------------------------------------------------------------------
// TryRemoveBitCast:
//   Try to remove a bitcast node by changing its operand.
//
// Arguments:
//    node - Bitcast node
//
// Returns:
//   True if the bitcast was removed.
//
bool Lowering::TryRemoveBitCast(GenTreeUnOp* node)
{
    if (comp->opts.OptimizationDisabled())
    {
        return false;
    }

    GenTree* op = node->gtGetOp1();
    assert(genTypeSize(node) == genTypeSize(genActualType(op)));

    bool changed = false;
#ifdef FEATURE_SIMD
    bool isConst = op->OperIs(GT_CNS_INT, GT_CNS_DBL, GT_CNS_VEC);
#else
    bool isConst = op->OperIs(GT_CNS_INT, GT_CNS_DBL);
#endif

    if (isConst)
    {
        uint8_t bits[sizeof(simd_t)];
        assert(sizeof(bits) >= genTypeSize(genActualType(op)));
        if (op->OperIs(GT_CNS_INT))
        {
            ssize_t cns = op->AsIntCon()->IconValue();
            assert(sizeof(ssize_t) >= genTypeSize(genActualType(op)));
            memcpy(bits, &cns, genTypeSize(genActualType(op)));
        }
#ifdef FEATURE_SIMD
        else if (op->OperIs(GT_CNS_VEC))
        {
            memcpy(bits, &op->AsVecCon()->gtSimdVal, genTypeSize(op));
        }
#endif
        else
        {
            if (op->TypeIs(TYP_FLOAT))
            {
                float floatVal = FloatingPointUtils::convertToSingle(op->AsDblCon()->DconValue());
                memcpy(bits, &floatVal, sizeof(float));
            }
            else
            {
                double doubleVal = op->AsDblCon()->DconValue();
                memcpy(bits, &doubleVal, sizeof(double));
            }
        }

        GenTree* newCon = comp->gtNewGenericCon(node->TypeGet(), bits);
        BlockRange().InsertAfter(op, newCon);
        BlockRange().Remove(op);

        node->gtOp1 = op = newCon;

        changed = true;
    }
    else if (op->OperIs(GT_LCL_FLD, GT_IND))
    {
        op->ChangeType(node->TypeGet());
        changed = true;
    }

    if (!changed)
    {
        return false;
    }

    LIR::Use use;
    if (BlockRange().TryGetUse(node, &use))
    {
        use.ReplaceWith(op);
    }
    else
    {
        op->SetUnusedValue();
    }

    BlockRange().Remove(node);
    return true;
}

//------------------------------------------------------------------------
// ContainCheckBitCast: determine whether the source of a BITCAST should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckBitCast(GenTreeUnOp* node)
{
    GenTree* const op1 = node->gtGetOp1();
    if (op1->OperIs(GT_LCL_VAR) && (genTypeSize(op1) == genTypeSize(node)))
    {
        if (IsContainableMemoryOp(op1) && IsSafeToContainMem(node, op1))
        {
            MakeSrcContained(node, op1);
        }
        else if (IsSafeToMarkRegOptional(node, op1))
        {
            MakeSrcRegOptional(node, op1);
        }
    }
}

//------------------------------------------------------------------------
// TryLowerBlockStoreAsGcBulkCopyCall: Lower a block store node as a CORINFO_HELP_BULK_WRITEBARRIER call
//
// Arguments:
//    blkNode - The block store node to lower
//
bool Lowering::TryLowerBlockStoreAsGcBulkCopyCall(GenTreeBlk* blk)
{
    if (comp->opts.OptimizationDisabled())
    {
        return false;
    }

    // Replace STORE_BLK (struct copy) with CORINFO_HELP_BULK_WRITEBARRIER which performs
    // bulk copy for byrefs.
    const unsigned bulkCopyThreshold = 4;
    if (!blk->OperIs(GT_STORE_BLK) || blk->OperIsInitBlkOp() || blk->IsVolatile() ||
        (blk->GetLayout()->GetGCPtrCount() < bulkCopyThreshold))
    {
        return false;
    }

    GenTree* dest = blk->Addr();
    GenTree* data = blk->Data();

    if (data->OperIs(GT_IND))
    {
        if (data->AsIndir()->IsVolatile())
        {
            return false;
        }

        // Drop GT_IND nodes
        BlockRange().Remove(data);
        data = data->AsIndir()->Addr();
    }
    else
    {
        assert(data->OperIs(GT_LCL_VAR, GT_LCL_FLD));

        // Convert local to LCL_ADDR
        unsigned lclOffset = data->AsLclVarCommon()->GetLclOffs();
        data->ChangeOper(GT_LCL_ADDR);
        data->ChangeType(TYP_I_IMPL);
        data->AsLclFld()->SetLclOffs(lclOffset);
        data->ClearContained();
    }

    // Size is a constant
    GenTreeIntCon* size = comp->gtNewIconNode((ssize_t)blk->GetLayout()->GetSize(), TYP_I_IMPL);
    BlockRange().InsertBefore(data, size);

    // A hacky way to safely call fgMorphTree in Lower
    GenTree* destPlaceholder = comp->gtNewZeroConNode(dest->TypeGet());
    GenTree* dataPlaceholder = comp->gtNewZeroConNode(genActualType(data));
    GenTree* sizePlaceholder = comp->gtNewZeroConNode(genActualType(size));

    GenTreeCall* call = comp->gtNewHelperCallNode(CORINFO_HELP_BULK_WRITEBARRIER, TYP_VOID, destPlaceholder,
                                                  dataPlaceholder, sizePlaceholder);
    comp->fgMorphArgs(call);

    LIR::Range range      = LIR::SeqTree(comp, call);
    GenTree*   rangeStart = range.FirstNode();
    GenTree*   rangeEnd   = range.LastNode();

    BlockRange().InsertBefore(blk, std::move(range));
    blk->gtBashToNOP();

    LIR::Use destUse;
    LIR::Use sizeUse;
    BlockRange().TryGetUse(destPlaceholder, &destUse);
    BlockRange().TryGetUse(sizePlaceholder, &sizeUse);
    destUse.ReplaceWith(dest);
    sizeUse.ReplaceWith(size);
    destPlaceholder->SetUnusedValue();
    sizePlaceholder->SetUnusedValue();

    LIR::Use dataUse;
    BlockRange().TryGetUse(dataPlaceholder, &dataUse);
    dataUse.ReplaceWith(data);
    dataPlaceholder->SetUnusedValue();

    LowerRange(rangeStart, rangeEnd);

    // Finally move all GT_PUTARG_* nodes
    // Re-use the existing logic for CFG call args here
    MoveCFGCallArgs(call);

    BlockRange().Remove(destPlaceholder);
    BlockRange().Remove(sizePlaceholder);
    BlockRange().Remove(dataPlaceholder);

    // Add implicit nullchecks for dest and data if needed:
    //
    auto wrapWithNullcheck = [&](GenTree* node) {
        if (comp->fgAddrCouldBeNull(node))
        {
            LIR::Use nodeUse;
            BlockRange().TryGetUse(node, &nodeUse);
            GenTree* nodeClone = comp->gtNewLclvNode(nodeUse.ReplaceWithLclVar(comp), genActualType(node));
            GenTree* nullcheck = comp->gtNewNullCheck(nodeClone);
            BlockRange().InsertAfter(nodeUse.Def(), nodeClone, nullcheck);
            LowerNode(nullcheck);
        }
    };
    wrapWithNullcheck(dest);
    wrapWithNullcheck(data);

    return true;
}

//------------------------------------------------------------------------
// LowerBlockStoreAsHelperCall: Lower a block store node as a memset/memcpy call
//
// Arguments:
//    blkNode - The block store node to lower
//
void Lowering::LowerBlockStoreAsHelperCall(GenTreeBlk* blkNode)
{
    // We shouldn't be using helper calls for blocks on heap containing GC pointers.
    // due to atomicity guarantees.
    assert(!blkNode->IsZeroingGcPointersOnHeap());

    LIR::Use use;
    assert(!BlockRange().TryGetUse(blkNode, &use));

    const bool isVolatile = blkNode->IsVolatile();

    GenTree* dest = blkNode->Addr();
    GenTree* data = blkNode->Data();
    GenTree* size;

    CorInfoHelpFunc helper;

    // Is it Memset ...
    if (blkNode->OperIsInitBlkOp())
    {
        helper = CORINFO_HELP_MEMSET;

        // Drop GT_INIT_VAL nodes
        if (data->OperIsInitVal())
        {
            BlockRange().Remove(data);
            data = data->gtGetOp1();
        }
    }
    else
    {
        // ... or Memcpy?
        helper = CORINFO_HELP_MEMCPY;

        if (data->OperIs(GT_IND))
        {
            // Drop GT_IND nodes
            BlockRange().Remove(data);
            data = data->AsIndir()->Addr();
        }
        else
        {
            assert(data->OperIs(GT_LCL_VAR, GT_LCL_FLD));

            // Convert local to LCL_ADDR
            unsigned lclOffset = data->AsLclVarCommon()->GetLclOffs();

            data->ChangeOper(GT_LCL_ADDR);
            data->ChangeType(TYP_I_IMPL);
            data->AsLclFld()->SetLclOffs(lclOffset);
            data->ClearContained();
        }
    }

    // Size is a constant
    size = comp->gtNewIconNode(blkNode->Size(), TYP_I_IMPL);
    BlockRange().InsertBefore(data, size);

    // A hacky way to safely call fgMorphTree in Lower
    GenTree* destPlaceholder = comp->gtNewZeroConNode(dest->TypeGet());
    GenTree* dataPlaceholder = comp->gtNewZeroConNode(genActualType(data));
    GenTree* sizePlaceholder = comp->gtNewZeroConNode(genActualType(size));

    const bool isMemzero = helper == CORINFO_HELP_MEMSET ? data->IsIntegralConst(0) : false;

    GenTreeCall* call;
    if (isMemzero)
    {
        BlockRange().Remove(data);
        call = comp->gtNewHelperCallNode(CORINFO_HELP_MEMZERO, TYP_VOID, destPlaceholder, sizePlaceholder);
    }
    else
    {
        call = comp->gtNewHelperCallNode(helper, TYP_VOID, destPlaceholder, dataPlaceholder, sizePlaceholder);
    }
    comp->fgMorphArgs(call);

    LIR::Range range      = LIR::SeqTree(comp, call);
    GenTree*   rangeStart = range.FirstNode();
    GenTree*   rangeEnd   = range.LastNode();

    BlockRange().InsertBefore(blkNode, std::move(range));
    blkNode->gtBashToNOP();

    LIR::Use destUse;
    LIR::Use sizeUse;
    BlockRange().TryGetUse(destPlaceholder, &destUse);
    BlockRange().TryGetUse(sizePlaceholder, &sizeUse);
    destUse.ReplaceWith(dest);
    sizeUse.ReplaceWith(size);
    destPlaceholder->SetUnusedValue();
    sizePlaceholder->SetUnusedValue();

    if (!isMemzero)
    {
        LIR::Use dataUse;
        BlockRange().TryGetUse(dataPlaceholder, &dataUse);
        dataUse.ReplaceWith(data);
        dataPlaceholder->SetUnusedValue();
    }

    LowerRange(rangeStart, rangeEnd);

    // Finally move all GT_PUTARG_* nodes
    // Re-use the existing logic for CFG call args here
    MoveCFGCallArgs(call);

    BlockRange().Remove(destPlaceholder);
    BlockRange().Remove(sizePlaceholder);
    if (!isMemzero)
    {
        BlockRange().Remove(dataPlaceholder);
    }

    // Wrap with memory barriers if the block store was volatile
    // Note: on XARCH these half-barriers only have optimization inhibiting effects, and do not emit anything
    if (isVolatile)
    {
        GenTree* firstBarrier  = comp->gtNewMemoryBarrier(BARRIER_STORE_ONLY);
        GenTree* secondBarrier = comp->gtNewMemoryBarrier(BARRIER_LOAD_ONLY);
        BlockRange().InsertBefore(call, firstBarrier);
        BlockRange().InsertAfter(call, secondBarrier);
        LowerNode(firstBarrier);
        LowerNode(secondBarrier);
    }
}

//------------------------------------------------------------------------
// GetLoadStoreCoalescingData: given a STOREIND/IND node, get the data needed to perform
//    store/load coalescing including pointer to the previous node.
//
// Arguments:
//    comp     - the compiler instance
//    ind      - the STOREIND/IND node
//    data     - [OUT] the data needed for store/load coalescing
//
// Return Value:
//    true if the data was successfully retrieved, false otherwise.
//
bool Lowering::GetLoadStoreCoalescingData(GenTreeIndir* ind, LoadStoreCoalescingData* data) const
{
    // Don't merge volatile load/stores.
    if (ind->IsVolatile())
    {
        return false;
    }

    const bool isStore = ind->OperIs(GT_STOREIND, GT_STORE_BLK);
    const bool isLoad  = ind->OperIs(GT_IND);

    auto isNodeInvariant = [](Compiler* comp, GenTree* node, bool allowNull) {
        if (node == nullptr)
        {
            return allowNull;
        }
        if (node->OperIsConst())
        {
            return true;
        }
        // We can allow bigger trees here, but it's not clear if it's worth it.
        return node->OperIs(GT_LCL_VAR) && !comp->lvaVarAddrExposed(node->AsLclVar()->GetLclNum());
    };

    if (isStore)
    {
        // For stores, Data() is expected to be an invariant node
        if (!isNodeInvariant(comp, ind->Data(), false))
        {
            return false;
        }
    }
    else if (!isLoad)
    {
        return false;
    }

    data->targetType = ind->TypeGet();
    data->value      = isStore ? ind->Data() : nullptr;
    if (ind->Addr()->OperIs(GT_LEA))
    {
        GenTree* base  = ind->Addr()->AsAddrMode()->Base();
        GenTree* index = ind->Addr()->AsAddrMode()->Index();
        if (!isNodeInvariant(comp, base, false))
        {
            // Base must be a local. It's possible for it to be nullptr when index is not null,
            // but let's ignore such cases.
            return false;
        }

        if (!isNodeInvariant(comp, index, true))
        {
            // Index should be either nullptr or a local.
            return false;
        }

        data->baseAddr = base == nullptr ? nullptr : base;
        data->index    = index == nullptr ? nullptr : index;
        data->scale    = ind->Addr()->AsAddrMode()->GetScale();
        data->offset   = ind->Addr()->AsAddrMode()->Offset();
    }
    else if (isNodeInvariant(comp, ind->Addr(), true))
    {
        // Address is just a local, no offset, scale is 1
        data->baseAddr = ind->Addr();
        data->index    = nullptr;
        data->scale    = 1;
        data->offset   = 0;
    }
    else
    {
        // Address is not LEA or local.
        return false;
    }

    bool isClosedRange = false;
    // Make sure there are no other unexpected nodes in-between.
    LIR::ReadOnlyRange range = BlockRange().GetTreeRange(ind, &isClosedRange);
    if (!isClosedRange)
    {
        return false;
    }

    data->rangeStart = range.FirstNode();
    data->rangeEnd   = range.LastNode();

    return true;
}

//------------------------------------------------------------------------
// LowerStoreIndirCoalescing: If the given IND/STOREIND node is followed by a similar
//    IND/STOREIND node, try to merge them into a single store of a twice wider type. Example:
//
//    *  STOREIND  int
//    +--*  LCL_VAR   byref  V00
//    \--*  CNS_INT   int    0x1
//
//    *  STOREIND  int
//    +--*  LEA(b+4)  byref
//    |  \--*  LCL_VAR   byref  V00
//    \--*  CNS_INT   int    0x2
//
//    We can merge these two into into a single store of 8 bytes with (0x1 | (0x2 << 32)) as the value
//
//    *  STOREIND  long
//    +--*  LEA(b+0)  byref
//    |  \--*  LCL_VAR   byref  V00
//    \--*  CNS_INT   long  0x200000001
//
//   NOTE: Our memory model allows us to do this optimization, see Memory-model.md:
//     * Adjacent non-volatile writes to the same location can be coalesced. (see Memory-model.md)
//
// Arguments:
//    ind - the current STOREIND node
//
void Lowering::LowerStoreIndirCoalescing(GenTreeIndir* ind)
{
// LA, RISC-V and ARM32 more likely to receive a terrible performance hit from
// unaligned accesses making this optimization questionable.
#if defined(TARGET_XARCH) || defined(TARGET_ARM64)
    if (!comp->opts.OptimizationEnabled())
    {
        return;
    }

    if (!ind->OperIs(GT_STOREIND, GT_STORE_BLK))
    {
        // Load coalescing is not yet supported
        return;
    }

    // We're going to do it in a loop while we see suitable STOREINDs to coalesce.
    // E.g.: we have the following LIR sequence:
    //
    //     ...addr nodes...
    //   STOREIND(int)
    //     ...addr nodes...
    //   STOREIND(short)
    //     ...addr nodes...
    //   STOREIND(short) <-- we're here
    //
    // First we merge two 'short' stores, then we merge the result with the 'int' store
    // to get a single store of 8 bytes.
    do
    {
        LoadStoreCoalescingData currData;
        LoadStoreCoalescingData prevData;

        // Get coalescing data for the current STOREIND
        if (!GetLoadStoreCoalescingData(ind, &currData))
        {
            return;
        }

        GenTree* prevTree = currData.rangeStart->gtPrev;
        // Now we need to find the previous STOREIND,
        // we can ignore any NOPs or IL_OFFSETs in-between
        while ((prevTree != nullptr) && prevTree->OperIs(GT_NOP, GT_IL_OFFSET))
        {
            prevTree = prevTree->gtPrev;
        }

        // It's not a store - bail out.
        if ((prevTree == nullptr) || !prevTree->OperIs(GT_STOREIND, GT_STORE_BLK))
        {
            return;
        }

        // Get coalescing data for the previous STOREIND
        GenTreeIndir* prevInd = prevTree->AsIndir();
        if (!GetLoadStoreCoalescingData(prevInd, &prevData))
        {
            return;
        }

        if (!currData.IsAddressEqual(prevData, /*ignoreOffset*/ true))
        {
            // Non-offset part of the address is not the same - bail out.
            return;
        }

        // The same offset means that we're storing to the same location of the same width.
        // Just remove the previous store then.
        if (prevData.offset == currData.offset)
        {
            BlockRange().Remove(prevData.rangeStart, prevData.rangeEnd);
            continue;
        }

        // TODO-ARM64-CQ: enable TYP_REF if we find a case where it's beneficial.
        // The algorithm does support TYP_REF (with null value), but it seems to be not worth
        // it on ARM64 where it's pretty efficient to do "stp xzr, xzr, [addr]" to clear two
        // items at once. Although, it may be profitable to do "stp q0, q0, [addr]".
        if (!varTypeIsIntegral(ind) && !varTypeIsSIMD(ind))
        {
            return;
        }

        assert(ind->OperIs(GT_STOREIND));
        assert(prevInd->OperIs(GT_STOREIND));
        assert(prevData.IsStore());
        assert(currData.IsStore());

        // For now, only non-relocatable constants are supported for data.
        if (!prevData.value->OperIsConst() || !currData.value->OperIsConst())
        {
            return;
        }

        if (prevData.value->IsCnsIntOrI() && prevData.value->AsIntCon()->ImmedValNeedsReloc(comp))
        {
            return;
        }

        if (currData.value->IsCnsIntOrI() && currData.value->AsIntCon()->ImmedValNeedsReloc(comp))
        {
            return;
        }

        // Otherwise, the difference between two offsets has to match the size of the type.
        // We don't support overlapping stores.
        if (abs(prevData.offset - currData.offset) != (int)genTypeSize(prevData.targetType))
        {
            return;
        }

        // For now, we require the current STOREIND to have LEA (previous store may not have it)
        // So we can easily adjust the offset, consider making it more flexible in future.
        if (!ind->Addr()->OperIs(GT_LEA))
        {
            return;
        }

        // Now the hardest part: decide whether it's safe to use an unaligned write.
        //
        // IND<byte> is always fine (and all IND<X> created here from such)
        // IND<simd> is not required to be atomic per our Memory Model
        bool allowsNonAtomic =
            ((ind->gtFlags & GTF_IND_ALLOW_NON_ATOMIC) != 0) && ((prevInd->gtFlags & GTF_IND_ALLOW_NON_ATOMIC) != 0);

        if (!allowsNonAtomic && currData.baseAddr->OperIs(GT_LCL_VAR) &&
            (currData.baseAddr->AsLclVar()->GetLclNum() == comp->info.compRetBuffArg))
        {
            // RetBuf is a private stack memory, so we don't need to worry about atomicity.
            allowsNonAtomic = true;
        }

        if (!allowsNonAtomic && (genTypeSize(ind) > 1) && !varTypeIsSIMD(ind))
        {
            // TODO-CQ: if we see that the target is a local memory (non address exposed)
            // we can use any type (including SIMD) for a new load.

            // Ignore indices for now, they can invalidate our alignment assumptions.
            // Although, we can take scale into account.
            if (currData.index != nullptr)
            {
                return;
            }

            // Base address being TYP_REF gives us a hint that data is pointer-aligned.
            if (!currData.baseAddr->TypeIs(TYP_REF))
            {
                return;
            }

            // Check whether the combined indir is still aligned.
            bool isCombinedIndirAtomic = (genTypeSize(ind) < TARGET_POINTER_SIZE) &&
                                         (min(prevData.offset, currData.offset) % (genTypeSize(ind) * 2)) == 0;

            if (genTypeSize(ind) == TARGET_POINTER_SIZE)
            {
#ifdef TARGET_ARM64
                // Per Arm Architecture Reference Manual for A-profile architecture:
                //
                // * Writes from SIMD and floating-point registers of a 128-bit value that is 64-bit aligned in memory
                //   are treated as a pair of single - copy atomic 64 - bit writes.
                //
                // Thus, we can allow 2xLONG -> SIMD, same for TYP_REF (for value being null)
                //
                // And we assume on ARM64 TYP_LONG/TYP_REF are always 64-bit aligned, otherwise
                // we're already doing a load that has no atomicity guarantees.
                isCombinedIndirAtomic = true;
#endif
            }

            if (!isCombinedIndirAtomic)
            {
                return;
            }
        }

        // Since we're merging two stores of the same type, the new type is twice wider.
        var_types oldType             = ind->TypeGet();
        var_types newType             = TYP_UNDEF;
        bool      tryReusingPrevValue = false;
        switch (oldType)
        {
            case TYP_BYTE:
            case TYP_UBYTE:
                newType = TYP_USHORT;
                break;

            case TYP_SHORT:
            case TYP_USHORT:
                newType = TYP_INT;
                break;

#ifdef TARGET_64BIT
            case TYP_INT:
                newType = TYP_LONG;
                break;

#if defined(FEATURE_HW_INTRINSICS)
            case TYP_LONG:
            case TYP_REF:
                // TLDR: we should be here only if one of the conditions is true:
                // 1) Both GT_INDs have GTF_IND_ALLOW_NON_ATOMIC flag
                // 2) ARM64: Data is at least 8-byte aligned
                // 3) AMD64: Data is at least 16-byte aligned on AMD/Intel with AVX+
                //
                newType = TYP_SIMD16;
                if ((oldType == TYP_REF) &&
                    (!currData.value->IsIntegralConst(0) || !prevData.value->IsIntegralConst(0)))
                {
                    // For TYP_REF we only support null values. In theory, we can also support frozen handles, e.g.:
                    //
                    //   arr[1] = "hello";
                    //   arr[0] = "world";
                    //
                    // but we don't want to load managed references into SIMD registers (we can only do so
                    // when we can issue a nongc region for a block)
                    return;
                }
                break;

#if defined(TARGET_AMD64)
            case TYP_SIMD16:
                if (comp->getPreferredVectorByteLength() >= 32)
                {
                    newType = TYP_SIMD32;
                    break;
                }
                tryReusingPrevValue = true;
                break;

            case TYP_SIMD32:
                if (comp->getPreferredVectorByteLength() >= 64)
                {
                    newType = TYP_SIMD64;
                    break;
                }
                tryReusingPrevValue = true;
                break;
#elif defined(TARGET_ARM64) // TARGET_AMD64
            case TYP_SIMD16:
                tryReusingPrevValue = true;
                break;

#endif // TARGET_ARM64
#endif // FEATURE_HW_INTRINSICS
#endif // TARGET_64BIT

            // TYP_FLOAT and TYP_DOUBLE aren't needed here - they're expected to
            // be converted to TYP_INT/TYP_LONG for constant value.
            //
            // TYP_UINT and TYP_ULONG are not legal for GT_IND.
            //
            default:
                return;
        }

        // If we can't merge these two stores into a single store, we can at least
        // cache prevData.value to a local and reuse it in currData.
        // Normally, LSRA is expected to do this for us, but it's not always the case for SIMD.
        if (tryReusingPrevValue)
        {
#if defined(FEATURE_HW_INTRINSICS)
            LIR::Use use;
            if (currData.value->OperIs(GT_CNS_VEC) && GenTree::Compare(prevData.value, currData.value) &&
                BlockRange().TryGetUse(prevData.value, &use))
            {
                GenTree* prevValueTmp = comp->gtNewLclvNode(use.ReplaceWithLclVar(comp), prevData.value->TypeGet());
                BlockRange().InsertBefore(currData.value, prevValueTmp);
                BlockRange().Remove(currData.value);
                ind->Data() = prevValueTmp;
            }
#endif // FEATURE_HW_INTRINSICS
            return;
        }

        assert(newType != TYP_UNDEF);

        // We should not be here for stores requiring write barriers.
        assert(!comp->codeGen->gcInfo.gcIsWriteBarrierStoreIndNode(ind->AsStoreInd()));
        assert(!comp->codeGen->gcInfo.gcIsWriteBarrierStoreIndNode(prevInd->AsStoreInd()));

        // Delete previous STOREIND entirely
        BlockRange().Remove(prevData.rangeStart, prevData.rangeEnd);

        // It's not expected to be contained yet, but just in case...
        ind->Data()->ClearContained();

        // We know it's always LEA for now
        GenTreeAddrMode* addr = ind->Addr()->AsAddrMode();

        // Update offset to be the minimum of the two
        addr->SetOffset(min(prevData.offset, currData.offset));

        // Update type for both STOREIND and val
        ind->gtType         = newType;
        ind->Data()->gtType = newType;

#if defined(TARGET_AMD64) && defined(FEATURE_HW_INTRINSICS)
        // Upgrading two SIMD stores to a wider SIMD store.
        // Only on x64 since ARM64 has no options above SIMD16
        if (varTypeIsSIMD(oldType))
        {
            int8_t* lowerCns = prevData.value->AsVecCon()->gtSimdVal.i8;
            int8_t* upperCns = currData.value->AsVecCon()->gtSimdVal.i8;

            // if the previous store was at a higher address, swap the constants
            if (prevData.offset > currData.offset)
            {
                std::swap(lowerCns, upperCns);
            }

            simd_t   newCns   = {};
            uint32_t oldWidth = genTypeSize(oldType);
            memcpy(newCns.i8, lowerCns, oldWidth);
            memcpy(newCns.i8 + oldWidth, upperCns, oldWidth);

            ind->Data()->AsVecCon()->gtSimdVal = newCns;
            continue;
        }
#endif

        size_t lowerCns = (size_t)prevData.value->AsIntCon()->IconValue();
        size_t upperCns = (size_t)currData.value->AsIntCon()->IconValue();

        // if the previous store was at a higher address, swap the constants
        if (prevData.offset > currData.offset)
        {
            std::swap(lowerCns, upperCns);
        }

#if defined(TARGET_64BIT) && defined(FEATURE_HW_INTRINSICS)
        // We're promoting two TYP_LONG/TYP_REF into TYP_SIMD16
        // All legality checks were done above.
        if (varTypeIsSIMD(newType))
        {
            // Replace two 64bit constants with a single 128bit constant
            int8_t val[16];
            memcpy(val, &lowerCns, 8);
            memcpy(val + 8, &upperCns, 8);
            GenTreeVecCon* vecCns = comp->gtNewVconNode(newType, &val);

            BlockRange().InsertAfter(ind->Data(), vecCns);
            BlockRange().Remove(ind->Data());
            ind->gtOp2 = vecCns;
            continue;
        }
#endif // TARGET_64BIT && FEATURE_HW_INTRINSICS

        // Trim the constants to the size of the type, e.g. for TYP_SHORT and TYP_USHORT
        // the mask will be 0xFFFF, for TYP_INT - 0xFFFFFFFF.
        size_t mask = ~(size_t(0)) >> (sizeof(size_t) - genTypeSize(oldType)) * BITS_PER_BYTE;
        lowerCns &= mask;
        upperCns &= mask;

        size_t val = (lowerCns | (upperCns << (genTypeSize(oldType) * BITS_PER_BYTE)));
        JITDUMP("Coalesced two stores into a single store with value %lld\n", (int64_t)val);

        ind->Data()->AsIntCon()->gtIconVal = (ssize_t)val;
        if (genTypeSize(oldType) == 1)
        {
            // A mark for future foldings that this IND doesn't need to be atomic.
            ind->gtFlags |= GTF_IND_ALLOW_NON_ATOMIC;
        }

    } while (true);
#endif // TARGET_XARCH || TARGET_ARM64
}

//------------------------------------------------------------------------
// LowerStoreIndirCommon: a common logic to lower StoreIndir.
//
// Arguments:
//    ind - the store indirection node we are lowering.
//
// Return Value:
//    Next node to lower.
//
GenTree* Lowering::LowerStoreIndirCommon(GenTreeStoreInd* ind)
{
    assert(!ind->TypeIs(TYP_STRUCT));

    TryRetypingFloatingPointStoreToIntegerStore(ind);

#if defined(TARGET_ARM64)
    // Verify containment safety before creating an LEA that must be contained.
    //
    const bool isContainable = IsInvariantInRange(ind->Addr(), ind);
#else
    const bool isContainable = true;
#endif
    TryCreateAddrMode(ind->Addr(), isContainable, ind);

    if (comp->codeGen->gcInfo.gcIsWriteBarrierStoreIndNode(ind))
    {
        return ind->gtNext;
    }

#ifndef TARGET_XARCH
    if (ind->Data()->IsIconHandle(GTF_ICON_OBJ_HDL))
    {
        const ssize_t handle = ind->Data()->AsIntCon()->IconValue();
        if (!comp->info.compCompHnd->isObjectImmutable(reinterpret_cast<CORINFO_OBJECT_HANDLE>(handle)))
        {
            // On platforms with weaker memory model we need to make sure we use a store with the release semantic
            // when we publish a potentially mutable object
            // See relevant discussions https://github.com/dotnet/runtime/pull/76135#issuecomment-1257258310 and
            // https://github.com/dotnet/runtime/pull/76112#discussion_r980639782

            // This can be relaxed to "just make sure to use stlr/memory barrier" if needed
            ind->gtFlags |= GTF_IND_VOLATILE;
        }
    }
#endif

    LowerStoreIndirCoalescing(ind);
    return LowerStoreIndir(ind);
}

//------------------------------------------------------------------------
// LowerIndir: a common logic to lower IND load or NullCheck.
//
// Arguments:
//    ind - the ind node we are lowering.
//
GenTree* Lowering::LowerIndir(GenTreeIndir* ind)
{
    GenTree* next = ind->gtNext;

    assert(ind->OperIs(GT_IND, GT_NULLCHECK));
    // Process struct typed indirs separately unless they are unused;
    // they only appear as the source of a block copy operation or a return node.
    if (!ind->TypeIs(TYP_STRUCT) || ind->IsUnusedValue())
    {
#ifndef TARGET_XARCH
        // On non-xarch, whether or not we can contain an address mode will depend on the access width
        // which may be changed when transforming an unused indir, so do that first.
        // On xarch, it is the opposite: we transform to indir/nullcheck based on whether we contained the
        // address mode, so in that case we must do this transformation last.
        if (ind->OperIs(GT_NULLCHECK) || ind->IsUnusedValue())
        {
            TransformUnusedIndirection(ind, comp, m_block);
        }
#endif

#ifdef TARGET_ARM64
        LIR::Use use;
        if (ind->OperIs(GT_IND) && ind->IsVolatile() && varTypeIsFloating(ind) && BlockRange().TryGetUse(ind, &use))
        {
            // Convert "IND<float>(addr)" to "BitCast<float>(IND<int>(addr))"
            // for volatile loads since there is no ldar for SIMD regs
            var_types targetType = ind->TypeGet();
            ind->ChangeType(ind->TypeIs(TYP_DOUBLE) ? TYP_LONG : TYP_INT);

            // Now it might be eligible for some addressing modes with LDAPUR:
            const bool isContainable = IsInvariantInRange(ind->Addr(), ind);
            TryCreateAddrMode(ind->Addr(), isContainable, ind);

            // Wrap the resulting IND into BITCAST:
            GenTree* castOp = comp->gtNewBitCastNode(targetType, ind);
            BlockRange().InsertAfter(ind, castOp);
            use.ReplaceWith(castOp);
            return castOp;
        }
#endif

        // TODO-Cleanup: We're passing isContainable = true but ContainCheckIndir rejects
        // address containment in some cases so we end up creating trivial (reg + offset)
        // or (reg + reg) LEAs that are not necessary.

#if defined(TARGET_ARM64)
        // Verify containment safety before creating an LEA that must be contained.
        //
        const bool isContainable = IsInvariantInRange(ind->Addr(), ind);
#else
        const bool isContainable = true;
#endif

        TryCreateAddrMode(ind->Addr(), isContainable, ind);
        ContainCheckIndir(ind);

#ifdef TARGET_XARCH
        if (ind->OperIs(GT_NULLCHECK) || ind->IsUnusedValue())
        {
            TransformUnusedIndirection(ind, comp, m_block);
        }
#endif
    }
    else
    {
        // If the `ADDR` node under `STORE_BLK(dstAddr, IND(struct(ADDR))`
        // is a complex one it could benefit from an `LEA` that is not contained.
        const bool isContainable = false;
        TryCreateAddrMode(ind->Addr(), isContainable, ind);
    }

#ifdef TARGET_ARM64
    if (comp->opts.OptimizationEnabled() && ind->OperIs(GT_IND))
    {
        OptimizeForLdpStp(ind);
    }
#endif

    return next;
}

#ifdef TARGET_ARM64

// Max distance that we will try to move an indirection backwards to become
// adjacent to another indirection. As an empirical observation, increasing
// this number to 32 for the smoke_tests collection resulted in 3684 -> 3796
// cases passing the distance check, but 82 out of these 112 extra cases were
// then rejected due to interference. So 16 seems like a good number to balance
// the throughput costs.
const int LDP_STP_REORDERING_MAX_DISTANCE = 16;

//------------------------------------------------------------------------
// OptimizeForLdp: Record information about an indirection, and try to optimize
// it by moving it to be adjacent with a previous indirection such that they
// can be transformed into 'ldp'.
//
// Arguments:
//    ind - Indirection to record and to try to move.
//
// Returns:
//    True if the optimization was successful.
//
bool Lowering::OptimizeForLdpStp(GenTreeIndir* ind)
{
    if (!ind->TypeIs(TYP_INT, TYP_LONG, TYP_FLOAT, TYP_DOUBLE, TYP_SIMD8, TYP_SIMD16) || ind->IsVolatile())
    {
        return false;
    }

    target_ssize_t offs = 0;
    GenTree*       addr = ind->Addr();
    comp->gtPeelOffsets(&addr, &offs);

    if (!addr->OperIs(GT_LCL_VAR))
    {
        return false;
    }

    // Every indirection takes an expected 2+ nodes, so we only expect at most
    // half the reordering distance to be candidates for the optimization.
    int maxCount = min(m_blockIndirs.Height(), LDP_STP_REORDERING_MAX_DISTANCE / 2);
    for (int i = 0; i < maxCount; i++)
    {
        SavedIndir& prev = m_blockIndirs.TopRef(i);
        if (prev.AddrBase->GetLclNum() != addr->AsLclVar()->GetLclNum())
        {
            continue;
        }

        GenTreeIndir* prevIndir = prev.Indir;
        if ((prevIndir == nullptr) || (prevIndir->TypeGet() != ind->TypeGet()))
        {
            continue;
        }

        if (prevIndir->gtNext == nullptr)
        {
            // Deleted by other optimization
            continue;
        }

        if (prevIndir->OperIsStore() != ind->OperIsStore())
        {
            continue;
        }

        JITDUMP("[%06u] and [%06u] are indirs off the same base with offsets +%03u and +%03u\n",
                Compiler::dspTreeID(ind), Compiler::dspTreeID(prevIndir), (unsigned)offs, (unsigned)prev.Offset);
        if (std::abs(offs - prev.Offset) == genTypeSize(ind))
        {
            JITDUMP("  ..and they are amenable to ldp/stp optimization\n");
            if (TryMakeIndirsAdjacent(prevIndir, ind))
            {
                // Do not let the previous one participate in
                // another instance; that can cause us to e.g. convert
                // *(x+4), *(x+0), *(x+8), *(x+12) =>
                // *(x+4), *(x+8), *(x+0), *(x+12)
                prev.Indir = nullptr;
                return true;
            }
            break;
        }
        else
        {
            JITDUMP("  ..but at non-adjacent offset\n");
        }
    }

    m_blockIndirs.Emplace(ind, addr->AsLclVar(), offs);
    return false;
}

//------------------------------------------------------------------------
// TryMakeIndirsAdjacent: Try to prove that it is legal to move an indirection
// to be adjacent to a previous indirection. If successful, perform the move.
//
// Arguments:
//    prevIndir - Previous indirection
//    indir     - Indirection to try to move to be adjacent to 'prevIndir'
//
// Returns:
//    True if the optimization was successful.
//
bool Lowering::TryMakeIndirsAdjacent(GenTreeIndir* prevIndir, GenTreeIndir* indir)
{
    GenTree* cur = prevIndir;
    for (int i = 0; i < LDP_STP_REORDERING_MAX_DISTANCE; i++)
    {
        // No nodes should be marked yet
        assert((cur->gtLIRFlags & LIR::Flags::Mark) == 0);

        cur = cur->gtNext;
        if (cur == indir)
            break;

        // We can reorder indirs with some calls, but introducing a LIR edge
        // that spans a call can introduce spills (or callee-saves).
        if (cur->IsCall())
        {
            JITDUMP("  ..but they are separated by node [%06u] that kills registers\n", Compiler::dspTreeID(cur));
            return false;
        }
    }

    if (cur != indir)
    {
        JITDUMP("  ..but they are too far separated\n");
        return false;
    }

    JITDUMP(
        "  ..and they are close. Trying to move the following range (where * are nodes part of the data flow):\n\n");
#ifdef DEBUG
    bool     isClosed;
    GenTree* startDumpNode = BlockRange().GetTreeRange(prevIndir, &isClosed).FirstNode();
    GenTree* endDumpNode   = indir->gtNext;

    auto dumpWithMarks = [=]() {
        if (!comp->verbose)
        {
            return;
        }

        for (GenTree* node = startDumpNode; node != endDumpNode; node = node->gtNext)
        {
            const char* prefix;
            if (node == prevIndir)
                prefix = "1. ";
            else if (node == indir)
                prefix = "2. ";
            else if ((node->gtLIRFlags & LIR::Flags::Mark) != 0)
                prefix = "*  ";
            else
                prefix = "   ";

            comp->gtDispLIRNode(node, prefix);
        }
    };

#endif

    // Unmark tree when we exit the current scope
    auto code = [this, indir] {
        UnmarkTree(indir);
    };
    jitstd::utility::scoped_code<decltype(code)> finally(code);

    MarkTree(indir);

    INDEBUG(dumpWithMarks());
    JITDUMP("\n");

    if ((prevIndir->gtLIRFlags & LIR::Flags::Mark) != 0)
    {
        JITDUMP("Previous indir is part of the data flow of current indir\n");
        return false;
    }

    m_scratchSideEffects.Clear();

    bool sawData = false;
    for (GenTree* cur = prevIndir->gtNext; cur != indir; cur = cur->gtNext)
    {
        if ((cur->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            // 'cur' is part of data flow of 'indir', so we will be moving the
            // currently recorded effects past 'cur'.
            if (m_scratchSideEffects.InterferesWith(comp, cur, true))
            {
                JITDUMP("Giving up due to interference with [%06u]\n", Compiler::dspTreeID(cur));
                return false;
            }

            if (indir->OperIsStore())
            {
                sawData |= cur == indir->Data();
            }
        }
        else
        {
            // Not part of dataflow; add its effects that will move past
            // 'indir'.
            m_scratchSideEffects.AddNode(comp, cur);
        }
    }

    if (m_scratchSideEffects.InterferesWith(comp, indir, true))
    {
        if (!indir->OperIsLoad())
        {
            JITDUMP("Have conservative interference with last store. Giving up.\n");
            return false;
        }

        // Try a bit harder, making use of the following facts:
        //
        // 1. We know the indir is non-faulting, so we do not need to worry
        // about reordering exceptions
        //
        // 2. We can reorder with non-volatile INDs even if they have
        // GTF_ORDER_SIDEEFF; these indirs only have GTF_ORDER_SIDEEFF due to
        // being non-faulting
        //
        // 3. We can also reorder with non-volatile STOREINDs if we can prove
        // no aliasing. We can do that for two common cases:
        //    * The addresses are based on the same local but at distinct offset ranges
        //    * The addresses are based off TYP_REF bases at distinct offset ranges

        JITDUMP("Have conservative interference with last indir. Trying a smarter interference check...\n");

        GenTree*       indirAddr = indir->Addr();
        target_ssize_t offs      = 0;
        comp->gtPeelOffsets(&indirAddr, &offs);

        bool checkLocal = indirAddr->OperIsLocal();
        if (checkLocal)
        {
            unsigned lclNum = indirAddr->AsLclVarCommon()->GetLclNum();
            checkLocal = !comp->lvaGetDesc(lclNum)->IsAddressExposed() && !m_scratchSideEffects.WritesLocal(lclNum);
        }

        // Helper lambda to check if a single node interferes with 'indir'.
        auto interferes = [=](GenTree* node) {
            if (((node->gtFlags & GTF_ORDER_SIDEEFF) != 0) && node->OperSupportsOrderingSideEffect())
            {
                // Cannot normally reorder GTF_ORDER_SIDEEFF and GTF_GLOB_REF,
                // except for some of the known cases described above.
                if (!node->OperIs(GT_IND, GT_BLK, GT_STOREIND, GT_STORE_BLK) || node->AsIndir()->IsVolatile())
                {
                    return true;
                }
            }

            AliasSet::NodeInfo nodeInfo(comp, node);

            if (nodeInfo.WritesAddressableLocation())
            {
                if (!node->OperIs(GT_STOREIND, GT_STORE_BLK))
                {
                    return true;
                }

                GenTreeIndir*  store     = node->AsIndir();
                GenTree*       storeAddr = store->Addr();
                target_ssize_t storeOffs = 0;
                comp->gtPeelOffsets(&storeAddr, &storeOffs);

                bool distinct = (storeOffs + (target_ssize_t)store->Size() <= offs) ||
                                (offs + (target_ssize_t)indir->Size() <= storeOffs);

                if (checkLocal && GenTree::Compare(indirAddr, storeAddr) && distinct)
                {
                    JITDUMP("Cannot interfere with [%06u] since they are off the same local V%02u and indir range "
                            "[%03u..%03u) does not interfere with store range [%03u..%03u)\n",
                            Compiler::dspTreeID(node), indirAddr->AsLclVarCommon()->GetLclNum(), (unsigned)offs,
                            (unsigned)offs + indir->Size(), (unsigned)storeOffs, (unsigned)storeOffs + store->Size());
                }
                // Two indirs off of TYP_REFs cannot overlap if their offset ranges are distinct.
                else if (indirAddr->TypeIs(TYP_REF) && storeAddr->TypeIs(TYP_REF) && distinct)
                {
                    JITDUMP("Cannot interfere with [%06u] since they are both off TYP_REF bases and indir range "
                            "[%03u..%03u) does not interfere with store range [%03u..%03u)\n",
                            Compiler::dspTreeID(node), (unsigned)offs, (unsigned)offs + indir->Size(),
                            (unsigned)storeOffs, (unsigned)storeOffs + store->Size());
                }
                else
                {
                    return true;
                }
            }

            return false;
        };

        for (GenTree* cur = indir->gtPrev; cur != prevIndir; cur = cur->gtPrev)
        {
            if ((cur->gtLIRFlags & LIR::Flags::Mark) != 0)
            {
                continue;
            }

            if (interferes(cur))
            {
                JITDUMP("Indir [%06u] interferes with [%06u]\n", Compiler::dspTreeID(indir), Compiler::dspTreeID(cur));
                return false;
            }
        }
    }

    JITDUMP("Interference checks passed: can move unrelated nodes past second indir.\n");

    if (sawData)
    {
        // If the data node of 'indir' is between 'prevIndir' and 'indir' then
        // try to move the previous indir up to happen after the data
        // computation. We will be moving all nodes unrelated to the data flow
        // past 'indir', so we only need to check interference between
        // 'prevIndir' and all nodes that are part of 'indir's dataflow.
        m_scratchSideEffects.Clear();
        m_scratchSideEffects.AddNode(comp, prevIndir);

        for (GenTree* cur = prevIndir->gtNext;; cur = cur->gtNext)
        {
            if ((cur->gtLIRFlags & LIR::Flags::Mark) != 0)
            {
                if (m_scratchSideEffects.InterferesWith(comp, cur, true))
                {
                    JITDUMP("Cannot move prev indir [%06u] up past [%06u] to get it past the data computation\n",
                            Compiler::dspTreeID(prevIndir), Compiler::dspTreeID(cur));
                    return false;
                }
            }

            if (cur == indir->Data())
            {
                break;
            }
        }
    }

    // For some hardware the ldr -> ldp transformation can result in missed
    // store-to-load forwarding opportunities, which can seriously harm
    // performance. Here we do a best effort check to see if one of the loads
    // we are combining may be loading from a store that reaches the load
    // without redefining the address.
    if (prevIndir->OperIsLoad() && indir->OperIsLoad() && IsStoreToLoadForwardingCandidateInLoop(prevIndir, indir))
    {
        JITDUMP("Avoiding making indirs adjacent; this may be the target of a store-to-load forwarding candidate\n");
        return false;
    }

    JITDUMP("Moving nodes that are not part of data flow of [%06u]\n\n", Compiler::dspTreeID(indir));

    GenTree* previous = prevIndir;
    for (GenTree* node = prevIndir->gtNext;;)
    {
        GenTree* next = node->gtNext;

        if ((node->gtLIRFlags & LIR::Flags::Mark) != 0)
        {
            // Part of data flow. Move it to happen right after 'previous'.
            BlockRange().Remove(node);
            BlockRange().InsertAfter(previous, node);
            previous = node;
        }

        if (node == indir)
        {
            break;
        }

        node = next;
    }

    if (sawData)
    {
        // For some reason LSRA is not able to reuse a constant if both LIR
        // temps are live simultaneously, so skip moving in those cases and
        // expect LSRA to reuse the constant instead.
        if (indir->Data()->OperIs(GT_CNS_INT, GT_CNS_DBL) && GenTree::Compare(indir->Data(), prevIndir->Data()))
        {
            JITDUMP("Not moving previous indir since we are expecting constant reuse for the data\n");
        }
        else
        {
            BlockRange().Remove(prevIndir);
            BlockRange().InsertAfter(indir->Data(), prevIndir);
        }
    }

    JITDUMP("Result:\n\n");
    INDEBUG(dumpWithMarks());
    JITDUMP("\n");
    return true;
}

//------------------------------------------------------------------------
// IsStoreToLoadForwardingCandidateInLoop: Check if one of the specified
// indirections may be the target of store-to-load forwarding from an indirect
// store that reaches one of the loads and that happens within the same loop.
// In those cases the transformation to 'ldp' can break this hardware
// optimization for some hardware.
//
// Arguments:
//   prevIndir - First indirection
//   indir     - Second indirection
//
// Returns:
//   True if so.
//
bool Lowering::IsStoreToLoadForwardingCandidateInLoop(GenTreeIndir* prevIndir, GenTreeIndir* indir)
{
    if (comp->m_dfsTree == nullptr)
    {
        comp->m_dfsTree = comp->fgComputeDfs();
    }

    if (!comp->m_dfsTree->HasCycle())
    {
        return false;
    }

    if (comp->m_loops == nullptr)
    {
        comp->m_loops       = FlowGraphNaturalLoops::Find(comp->m_dfsTree);
        comp->m_blockToLoop = BlockToNaturalLoopMap::Build(comp->m_loops);
    }

    FlowGraphNaturalLoop* loop = comp->m_blockToLoop->GetLoop(m_block);
    if (loop == nullptr)
    {
        return false;
    }

    GenTree*       addr1 = prevIndir->Addr();
    target_ssize_t offs1;
    comp->gtPeelOffsets(&addr1, &offs1);
    unsigned lcl1 = addr1->OperIs(GT_LCL_VAR) ? addr1->AsLclVarCommon()->GetLclNum() : BAD_VAR_NUM;

    GenTree*       addr2 = indir->Addr();
    target_ssize_t offs2;
    comp->gtPeelOffsets(&addr2, &offs2);
    unsigned lcl2 = addr1->OperIs(GT_LCL_VAR) ? addr2->AsLclVarCommon()->GetLclNum() : BAD_VAR_NUM;

    unsigned budget = 100;

    // Starting at an end node, go backwards until the specified first node and look for
    // 1) Definitions of the base address local, which invalidates future store-to-load forwarding
    // 2) Stores to the same address as is being loaded later, which allows store-to-load forwarding
    auto checkNodes = [=, &budget](GenTree* lastNode, GenTree* firstNode, bool* hasStore, bool* hasDef) {
        *hasStore = false;
        *hasDef   = false;

        for (GenTree* curNode = lastNode;; curNode = curNode->gtPrev)
        {
            if (curNode->OperIs(GT_STORE_LCL_VAR))
            {
                unsigned lclNum = curNode->AsLclVarCommon()->GetLclNum();
                if ((lclNum == lcl1) || (lclNum == lcl2))
                {
                    *hasDef = true;
                    return true;
                }
            }
            else if (curNode->OperIs(GT_STOREIND))
            {
                GenTreeIndir*  storeInd       = curNode->AsIndir();
                GenTree*       storeIndirAddr = storeInd->Addr();
                target_ssize_t storeIndirOffs;
                comp->gtPeelOffsets(&storeIndirAddr, &storeIndirOffs);

                if (storeIndirAddr->OperIs(GT_LCL_VAR) && ((storeIndirOffs == offs1) || (storeIndirOffs == offs2)))
                {
                    unsigned storeIndirAddrLcl = storeIndirAddr->AsLclVarCommon()->GetLclNum();
                    if ((storeIndirAddrLcl == lcl1) || (storeIndirAddrLcl == lcl2))
                    {
                        JITDUMP("Store at [%06u] may allow store-to-load forwarding of indir [%06u]\n",
                                Compiler::dspTreeID(curNode),
                                Compiler::dspTreeID(storeIndirAddrLcl == lcl1 ? prevIndir : indir));

                        *hasStore = true;
                        return true;
                    }
                }
            }

            if (curNode == firstNode)
            {
                break;
            }

            if (--budget == 0)
            {
                return false;
            }
        }

        return true;
    };

    bool hasStore;
    bool hasDef;
    if (!checkNodes(prevIndir, LIR::AsRange(m_block).FirstNode(), &hasStore, &hasDef))
    {
        // Out of budget
        return false;
    }

    if (hasStore)
    {
        // Have a store before the indir; it could be store-to-load forwarded.
        return true;
    }

    if (hasDef)
    {
        // Have a def before the indir; it would break store-to-load
        // forwarding. No preds to push then, so we are done.
        return false;
    }

    // Now we've checked range before the indirs; continue with its preds
    // inside the loop. We will check the range after the indirs once we get to
    // it.
    BitVecTraits traits = comp->m_dfsTree->PostOrderTraits();
    BitVec       visited(BitVecOps::MakeEmpty(&traits));

    ArrayStack<BasicBlock*> stack(comp->getAllocator(CMK_ArrayStack));

    auto pushPreds = [=, &traits, &visited, &stack](BasicBlock* block) {
        for (BasicBlock* pred : block->PredBlocks())
        {
            if (loop->ContainsBlock(pred) && BitVecOps::TryAddElemD(&traits, visited, pred->bbPostorderNum))
            {
                stack.Push(pred);
            }
        }
    };

    pushPreds(m_block);

    while (stack.Height() > 0)
    {
        BasicBlock* block = stack.Pop();

        LIR::Range& range = LIR::AsRange(block);

        GenTree* firstNode = block == m_block ? prevIndir : range.FirstNode();

        if ((firstNode != nullptr) && !checkNodes(range.LastNode(), firstNode, &hasStore, &hasDef))
        {
            // Out of budget
            return false;
        }

        if (hasStore)
        {
            // This would be store-to-load forwardable.
            return true;
        }

        if (hasDef)
        {
            // Redefinition of base local; skip pushing preds
            continue;
        }

        pushPreds(block);
    }

    return false;
}

//------------------------------------------------------------------------
// MarkTree: Mark trees involved in the computation of 'node' recursively.
//
// Arguments:
//    node - Root node.
//
void Lowering::MarkTree(GenTree* node)
{
    node->gtLIRFlags |= LIR::Flags::Mark;
    node->VisitOperands([=](GenTree* op) {
        MarkTree(op);
        return GenTree::VisitResult::Continue;
    });
}

//------------------------------------------------------------------------
// UnmarkTree: Unmark trees involved in the computation of 'node' recursively.
//
// Arguments:
//    node - Root node.
//
void Lowering::UnmarkTree(GenTree* node)
{
    node->gtLIRFlags &= ~LIR::Flags::Mark;
    node->VisitOperands([=](GenTree* op) {
        UnmarkTree(op);
        return GenTree::VisitResult::Continue;
    });
}

#endif // TARGET_ARM64

//------------------------------------------------------------------------
// IsContainableLclAddr: Can a given local address be contained?
//
// Most local addresses can be contained, however, there are two edge cases
// where this is not true:
// 1. When the resulting memory access will go beyond the local's location.
// 2. When the resulting access may go past a UINT16_MAX.
// Both of these requirements are imposed by the emitter.
//
// Arguments:
//    lclAddr    - The local address node
//    accessSize - The access size (of an indirection)
//
// Return Value:
//    Whether an indirection of "accessSize" may contain "lclAddr".
//
bool Lowering::IsContainableLclAddr(GenTreeLclFld* lclAddr, unsigned accessSize) const
{
    if (CheckedOps::AddOverflows<int32_t>(lclAddr->GetLclOffs(), accessSize, CheckedOps::Unsigned) ||
        !comp->IsValidLclAddr(lclAddr->GetLclNum(), lclAddr->GetLclOffs() + accessSize - 1))
    {
        // We depend on containment for correctness of liveness updates in codegen. Therefore, all
        // locals that may "return false" here MUST be address-exposed. Local morph ensures this.
        assert(comp->lvaGetDesc(lclAddr)->IsAddressExposed());
        return false;
    }

    return true;
}

//------------------------------------------------------------------------
// TransformUnusedIndirection: change the opcode and the type of the unused indirection.
//
// Arguments:
//    ind   - Indirection to transform.
//    comp  - Compiler instance.
//    block - Basic block of the indirection.
//
void Lowering::TransformUnusedIndirection(GenTreeIndir* ind, Compiler* comp, BasicBlock* block)
{
    // A nullcheck is essentially the same as an indirection with no use.
    // The difference lies in whether a target register must be allocated.
    // On XARCH we can generate a compare with no target register as long as the address
    // is not contained.
    // On ARM64 we can generate a load to REG_ZR in all cases.
    // However, on ARM we must always generate a load to a register.
    // In the case where we require a target register, it is better to use GT_IND, since
    // GT_NULLCHECK is a non-value node and would therefore require an internal register
    // to use as the target. That is non-optimal because it will be modeled as conflicting
    // with the source register(s).
    // So, to summarize:
    // - On ARM64, always use GT_NULLCHECK for a dead indirection.
    // - On ARM, always use GT_IND.
    // - On XARCH, use GT_IND if we have a contained address, and GT_NULLCHECK otherwise.
    // In all cases we try to preserve the original type and never make it wider to avoid AVEs.
    // For structs we conservatively lower it to BYTE. For 8-byte primitives we lower it to TYP_INT
    // on XARCH as an optimization.
    //
    assert(ind->OperIs(GT_NULLCHECK, GT_IND, GT_BLK));

    ind->ChangeType(comp->gtTypeForNullCheck(ind));

#if defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    bool useNullCheck = true;
#elif defined(TARGET_ARM)
    bool useNullCheck = false;
#else  // TARGET_XARCH
    bool useNullCheck = !ind->Addr()->isContained();
    ind->ClearDontExtend();
#endif // !TARGET_XARCH

    if (useNullCheck && !ind->OperIs(GT_NULLCHECK))
    {
        comp->gtChangeOperToNullCheck(ind);
        ind->ClearUnusedValue();
    }
    else if (!useNullCheck && !ind->OperIs(GT_IND))
    {
        ind->ChangeOper(GT_IND);
        ind->SetUnusedValue();
    }
}

//------------------------------------------------------------------------
// LowerLclHeap: a common logic to lower LCLHEAP.
//
// Arguments:
//    blkNode - the LCLHEAP node we are lowering.
//
void Lowering::LowerLclHeap(GenTree* node)
{
    assert(node->OperIs(GT_LCLHEAP));

#if defined(TARGET_XARCH)
    if (node->gtGetOp1()->IsCnsIntOrI())
    {
        GenTreeIntCon* sizeNode = node->gtGetOp1()->AsIntCon();
        ssize_t        size     = sizeNode->IconValue();

        if (size == 0)
        {
            // Replace with null for LCLHEAP(0)
            node->BashToZeroConst(TYP_I_IMPL);
            BlockRange().Remove(sizeNode);
            return;
        }

        if (comp->info.compInitMem)
        {
            ssize_t alignedSize = ALIGN_UP(size, STACK_ALIGN);
            if ((size > UINT_MAX) || (alignedSize > UINT_MAX))
            {
                // Size is too big - don't mark sizeNode as contained
                return;
            }

            LIR::Use use;
            if (BlockRange().TryGetUse(node, &use))
            {
                // Align LCLHEAP size for more efficient zeroing via BLK
                sizeNode->SetIconValue(alignedSize);

                // Emit STORE_BLK to zero it
                //
                //  *  STORE_BLK struct<alignedSize> (init) (Unroll)
                //  +--*  LCL_VAR   long   V01
                //  \--*  CNS_INT   int    0
                //
                GenTree*    heapLcl  = comp->gtNewLclvNode(use.ReplaceWithLclVar(comp), TYP_I_IMPL);
                GenTree*    zero     = comp->gtNewIconNode(0);
                GenTreeBlk* storeBlk = new (comp, GT_STORE_BLK)
                    GenTreeBlk(GT_STORE_BLK, TYP_STRUCT, heapLcl, zero, comp->typGetBlkLayout((unsigned)alignedSize));
                storeBlk->gtFlags |= (GTF_IND_UNALIGNED | GTF_ASG | GTF_EXCEPT | GTF_GLOB_REF);
                BlockRange().InsertAfter(use.Def(), heapLcl, zero, storeBlk);
            }
            else
            {
                // Value is unused and we don't mark the size node as contained
                return;
            }
        }
    }
#endif
    ContainCheckLclHeap(node->AsOp());
}

//------------------------------------------------------------------------
// LowerBlockStoreCommon: a common logic to lower STORE_BLK/DYN_BLK.
//
// Arguments:
//    blkNode - the store blk/obj node we are lowering.
//
void Lowering::LowerBlockStoreCommon(GenTreeBlk* blkNode)
{
    assert(blkNode->OperIs(GT_STORE_BLK));

    if (blkNode->ContainsReferences() && !blkNode->OperIsCopyBlkOp())
    {
        // and we only zero it (and that zero is better to be not hoisted/CSE'd)
        assert(blkNode->Data()->IsIntegralConst(0));
    }

    // Lose the type information stored in the source - we no longer need it.
    if (blkNode->Data()->OperIs(GT_BLK))
    {
        blkNode->Data()->SetOper(GT_IND);
        LowerIndir(blkNode->Data()->AsIndir());
    }

    if (TryTransformStoreObjAsStoreInd(blkNode))
    {
        return;
    }

    LowerBlockStore(blkNode);
    LowerStoreIndirCoalescing(blkNode);
}

//------------------------------------------------------------------------
// TryTransformStoreObjAsStoreInd: try to replace STORE_BLK as STOREIND.
//
// Arguments:
//    blkNode - the store node.
//
// Return value:
//    true if the replacement was made, false otherwise.
//
bool Lowering::TryTransformStoreObjAsStoreInd(GenTreeBlk* blkNode)
{
    assert(blkNode->OperIs(GT_STORE_BLK));
    if (!comp->opts.OptimizationEnabled())
    {
        return false;
    }

    var_types regType = blkNode->GetLayout()->GetRegisterType();
    if (regType == TYP_UNDEF)
    {
        return false;
    }

    GenTree* src = blkNode->Data();
    if (varTypeIsGC(regType))
    {
        // TODO-CQ: STOREIND does not try to contain src if we need a barrier,
        // STORE_BLK generates better code currently.
        return false;
    }

    if (src->OperIsInitVal() && !src->IsConstInitVal())
    {
        return false;
    }

    if (src->IsConstInitVal())
    {
#if !defined(TARGET_XARCH)
        if (varTypeIsSIMD(regType))
        {
            // Platforms with zero-regs may produce better/more compact codegen
            return false;
        }
#endif

        assert(!blkNode->ContainsReferences());
        if (src->OperIsInitVal())
        {
            BlockRange().Remove(src);
            src = src->gtGetOp1();
        }

        uint8_t  initVal = static_cast<uint8_t>(src->AsIntCon()->IconValue());
        GenTree* cnsVec  = comp->gtNewConWithPattern(regType, initVal);
        BlockRange().InsertAfter(src, cnsVec);
        BlockRange().Remove(src);
        blkNode->SetData(cnsVec);
    }
    else if (varTypeIsStruct(src))
    {
        src->ChangeType(regType);
        LowerNode(blkNode->Data());
    }
    else
    {
        unreached();
    }

    JITDUMP("Replacing STORE_BLK with STOREIND for [%06u]\n", blkNode->gtTreeID);
    blkNode->ChangeOper(GT_STOREIND);
    blkNode->ChangeType(regType);

#if defined(TARGET_XARCH)
    if (varTypeIsSmall(regType) && src->OperIs(GT_IND, GT_LCL_FLD))
    {
        src->SetDontExtend();
    }
#endif // TARGET_XARCH

    LowerStoreIndirCommon(blkNode->AsStoreInd());
    return true;
}

//------------------------------------------------------------------------
// TryRetypingFloatingPointStoreToIntegerStore: Retype an FP memory store.
//
// On some targets, integer stores are cheaper and/or smaller than their
// floating-point counterparts, because, e. g., integer immediates can be
// encoded inline while FP ones need to be loaded from the data section.
//
// Arguments:
//    store - The store node
//
void Lowering::TryRetypingFloatingPointStoreToIntegerStore(GenTree* store)
{
    assert(store->OperIsStore());

    if (!varTypeIsFloating(store))
    {
        return;
    }

    // Convert "STOREIND<float>(addr, floatVal)" to "STORIND<int>(addr, BitCast<int>(floatVal))"
    // for volatile stores since there is no stlr for SIMD regs
#ifdef TARGET_ARM64
    if (store->OperIs(GT_STOREIND) && store->AsStoreInd()->IsVolatile())
    {
        GenTreeStoreInd* ind = store->AsStoreInd();
        ind->ChangeType(ind->TypeIs(TYP_DOUBLE) ? TYP_LONG : TYP_INT);
        GenTree* castOp = comp->gtNewBitCastNode(ind->TypeGet(), ind->Data());
        BlockRange().InsertAfter(ind->Data(), castOp);
        ind->Data() = castOp;
        LowerNode(castOp);
        return;
    }
#endif

    // We only want to transform memory stores, not definitions of candidate locals.
    //
    if (store->OperIs(GT_STORE_LCL_VAR) && !comp->lvaGetDesc(store->AsLclVar())->lvDoNotEnregister)
    {
        return;
    }

    GenTree* value = store->Data();
    assert(store->TypeGet() == value->TypeGet());

    // Optimize *x = DCON to *x = ICON which can be slightly faster and/or smaller.
    //
    if (value->IsCnsFltOrDbl())
    {
        double    dblCns = value->AsDblCon()->DconValue();
        ssize_t   intCns = 0;
        var_types type   = TYP_UNKNOWN;
        // XARCH: we can always contain the immediates.
        // ARM64: zero can always be contained, other cases will use immediates from the data
        //        section and it is not a clear win to switch them to inline integers.
        // ARM:   FP constants are assembled from integral ones, so it is always profitable
        //        to directly use the integers as it avoids the int -> float conversion.

#if defined(TARGET_XARCH) || defined(TARGET_ARM)
        bool shouldSwitchToInteger = true;
#else // TARGET_ARM64 || TARGET_LOONGARCH64 || TARGET_RISCV64
        bool shouldSwitchToInteger = FloatingPointUtils::isPositiveZero(dblCns);
#endif

        if (shouldSwitchToInteger)
        {
            if (store->TypeIs(TYP_FLOAT))
            {
                float fltCns = static_cast<float>(dblCns);
                intCns       = *reinterpret_cast<INT32*>(&fltCns);
                type         = TYP_INT;
            }
#ifdef TARGET_64BIT
            else
            {
                assert(store->TypeIs(TYP_DOUBLE));
                intCns = *reinterpret_cast<INT64*>(&dblCns);
                type   = TYP_LONG;
            }
#endif
        }

        if (type != TYP_UNKNOWN)
        {
            value->BashToConst(intCns, type);

            assert(!store->OperIsLocalStore() || comp->lvaGetDesc(store->AsLclVarCommon())->lvDoNotEnregister);
            if (store->OperIs(GT_STORE_LCL_VAR))
            {
                store->SetOper(GT_STORE_LCL_FLD);
            }
            store->ChangeType(type);
        }
    }
}

//----------------------------------------------------------------------------------------------
// Lowering::TryLowerAndNegativeOne:
//    If safe, lowers a tree AND(X, CNS(-1)) to X.
//
// Arguments:
//    node - GT_AND node of integral type
//    nextNode - out parameter that represents the 'gtNext' of the given node if the transformation was successful
//
// Return Value:
//    Returns the true if the transformation was successful; false if it was not.
bool Lowering::TryLowerAndNegativeOne(GenTreeOp* node, GenTree** nextNode)
{
    assert(node->OperIs(GT_AND));
    assert(nextNode != nullptr);

    if (!varTypeIsIntegral(node))
        return false;

    if (node->gtSetFlags())
        return false;

    if (node->isContained())
        return false;

    GenTree* op2 = node->gtGetOp2();

    if (!op2->IsIntegralConst(-1))
        return false;

#ifndef TARGET_64BIT
    assert(op2->TypeIs(TYP_INT));
#endif // !TARGET_64BIT

    GenTree* op1 = node->gtGetOp1();

    LIR::Use use;
    if (BlockRange().TryGetUse(node, &use))
    {
        use.ReplaceWith(op1);
    }
    else
    {
        op1->SetUnusedValue();
    }

    *nextNode = node->gtNext;

    BlockRange().Remove(op2);
    BlockRange().Remove(node);

    return true;
}

#if defined(TARGET_AMD64) || defined(TARGET_ARM64)
//------------------------------------------------------------------------
// CanConvertOpToCCMP : Checks whether operand can be converted to CCMP
//
// Arguments:
//    operand - operand to check for CCMP conversion
//    tree    - parent of the operand
//
// Return Value:
//    true if operand can be converted to CCMP
//
bool Lowering::CanConvertOpToCCMP(GenTree* operand, GenTree* tree)
{
    return operand->OperIsCmpCompare() && varTypeIsIntegralOrI(operand->gtGetOp1()) &&
           IsInvariantInRange(operand, tree);
}

//------------------------------------------------------------------------
// TryLowerAndOrToCCMP : Lower AND/OR of two conditions into test + CCMP + SETCC nodes.
//
// Arguments:
//    tree - pointer to the node
//    next - [out] Next node to lower if this function returns true
//
// Return Value:
//    false if no changes were made
//
bool Lowering::TryLowerAndOrToCCMP(GenTreeOp* tree, GenTree** next)
{
    assert(tree->OperIs(GT_AND, GT_OR));

    if (!comp->opts.OptimizationEnabled())
    {
        return false;
    }

    GenTree* op1 = tree->gtGetOp1();
    GenTree* op2 = tree->gtGetOp2();

    if ((op1->OperIsCmpCompare() && varTypeIsIntegralOrI(op1->gtGetOp1())) ||
        (op2->OperIsCmpCompare() && varTypeIsIntegralOrI(op2->gtGetOp1())))
    {
        JITDUMP("[%06u] is a potential candidate for CCMP:\n", Compiler::dspTreeID(tree));
        DISPTREERANGE(BlockRange(), tree);
        JITDUMP("\n");
    }

    // Find out whether an operand is eligible to be converted to a conditional
    // compare. It must be a normal integral relop; for example, we cannot
    // conditionally perform a floating point comparison and there is no "ctst"
    // instruction that would allow us to conditionally implement
    // TEST_EQ/TEST_NE.
    //
    // For the other operand we can allow more arbitrary operations that set
    // the condition flags; the final transformation into the flags def is done
    // by TryLowerConditionToFlagsNode.
    //
    GenCondition cond1;
    bool         canConvertOp2ToCCMP = CanConvertOpToCCMP(op2, tree);
    bool         canConvertOp1ToCCMP = CanConvertOpToCCMP(op1, tree);

    if (canConvertOp2ToCCMP &&
        (!canConvertOp1ToCCMP ||
         ((op2->gtGetOp1()->IsIntegralConst() || !op2->gtGetOp1()->isContained()) &&
          (op2->gtGetOp2() == nullptr || op2->gtGetOp2()->IsIntegralConst() || !op2->gtGetOp2()->isContained()))) &&
        TryLowerConditionToFlagsNode(tree, op1, &cond1, false))
    {
        // Fall through, converting op2 to the CCMP
    }
    else if (canConvertOp1ToCCMP &&
             (!op1->gtGetOp1()->isContained() || op1->gtGetOp1()->IsIntegralConst() ||
              IsContainableMemoryOp(op1->gtGetOp1())) &&
             (op1->gtGetOp2() == nullptr || !op1->gtGetOp2()->isContained() || op1->gtGetOp2()->IsIntegralConst() ||
              IsContainableMemoryOp(op1->gtGetOp2())) &&
             TryLowerConditionToFlagsNode(tree, op2, &cond1, false))
    {
        std::swap(op1, op2);
    }
    else
    {
        JITDUMP("  ..could not turn [%06u] or [%06u] into a def of flags, bailing\n", Compiler::dspTreeID(op1),
                Compiler::dspTreeID(op2));
        return false;
    }

    BlockRange().Remove(op2);
    BlockRange().InsertBefore(tree, op2);

    GenCondition cond2 = GenCondition::FromRelop(op2);
    op2->SetOper(GT_CCMP);
    op2->gtType = TYP_VOID;
    op2->gtFlags |= GTF_SET_FLAGS;

    op2->gtGetOp1()->ClearContained();
    op2->gtGetOp2()->ClearContained();

    GenTreeCCMP* ccmp = op2->AsCCMP();

    if (tree->OperIs(GT_AND))
    {
        // If the first comparison succeeds then do the second comparison.
        ccmp->gtCondition = cond1;
        // Otherwise set the condition flags to something that makes the second
        // one fail.
        ccmp->gtFlagsVal = TruthifyingFlags(GenCondition::Reverse(cond2));
    }
    else
    {
        // If the first comparison fails then do the second comparison.
        ccmp->gtCondition = GenCondition::Reverse(cond1);
        // Otherwise set the condition flags to something that makes the second
        // one succeed.
        ccmp->gtFlagsVal = TruthifyingFlags(cond2);
    }

    ContainCheckConditionalCompare(ccmp);

    tree->SetOper(GT_SETCC);
    tree->AsCC()->gtCondition = cond2;

    JITDUMP("Conversion was legal. Result:\n");
    DISPTREERANGE(BlockRange(), tree);
    JITDUMP("\n");

    *next = tree->gtNext;
    return true;
}

//------------------------------------------------------------------------
// ContainCheckConditionalCompare: determine whether the source of a compare within a compare chain should be contained.
//
// Arguments:
//    node - pointer to the node
//
void Lowering::ContainCheckConditionalCompare(GenTreeCCMP* cmp)
{
    GenTree* op2 = cmp->gtOp2;

    if (op2->IsCnsIntOrI() && !op2->AsIntCon()->ImmedValNeedsReloc(comp))
    {
        target_ssize_t immVal = (target_ssize_t)op2->AsIntCon()->gtIconVal;

        if (emitter::emitIns_valid_imm_for_ccmp(immVal))
        {
            MakeSrcContained(cmp, op2);
        }
    }
}
#endif

#if defined(FEATURE_HW_INTRINSICS)
//----------------------------------------------------------------------------------------------
// Lowering::InsertNewSimdCreateScalarUnsafeNode: Inserts a new simd CreateScalarUnsafe node
//
//  Arguments:
//    simdType        - The return type of SIMD node being created
//    op1             - The value of the lowest element of the simd value
//    simdBaseJitType - the base JIT type of SIMD type of the intrinsic
//    simdSize        - the size of the SIMD type of the intrinsic
//
// Returns:
//    The inserted CreateScalarUnsafe node
//
// Remarks:
//    If the created node is a vector constant, op1 will be removed from the block range
//
GenTree* Lowering::InsertNewSimdCreateScalarUnsafeNode(var_types   simdType,
                                                       GenTree*    op1,
                                                       CorInfoType simdBaseJitType,
                                                       unsigned    simdSize)
{
    assert(varTypeIsSIMD(simdType));

    GenTree* result = comp->gtNewSimdCreateScalarUnsafeNode(simdType, op1, simdBaseJitType, simdSize);
    BlockRange().InsertAfter(op1, result);

    if (result->IsCnsVec())
    {
        BlockRange().Remove(op1);
    }
    return result;
}
#endif // FEATURE_HW_INTRINSICS

//----------------------------------------------------------------------------------------------
// Lowering::RequireOutgoingArgSpace: Record that the compilation will require
// outgoing arg space of at least the specified size.
//
//  Arguments:
//    node - The node that is the reason for the requirement.
//    size - The minimal required size of the outgoing arg space.
//
void Lowering::RequireOutgoingArgSpace(GenTree* node, unsigned size)
{
#if FEATURE_FIXED_OUT_ARGS
    if (size <= m_outgoingArgSpaceSize)
    {
        return;
    }

    JITDUMP("Bumping outgoing arg space size from %u to %u for [%06u]\n", m_outgoingArgSpaceSize, size,
            Compiler::dspTreeID(node));
    m_outgoingArgSpaceSize = size;
#endif
}

//----------------------------------------------------------------------------------------------
// Lowering::FinalizeOutgoingArgSpace: Finalize and allocate the outgoing arg
// space area.
//
void Lowering::FinalizeOutgoingArgSpace()
{
#if FEATURE_FIXED_OUT_ARGS
    // Finish computing the outgoing args area size
    //
    // Need to make sure the MIN_ARG_AREA_FOR_CALL space is added to the frame if:
    // 1. there are calls to THROW_HELPER methods.
    // 2. we are generating profiling Enter/Leave/TailCall hooks. This will ensure
    //    that even methods without any calls will have outgoing arg area space allocated.
    // 3. We will be generating calls to PInvoke helpers. TODO: This shouldn't be required because
    //    if there are any calls to PInvoke methods, there should be a call that we processed
    //    above. However, we still generate calls to PInvoke prolog helpers even if we have dead code
    //    eliminated all the calls.
    // 4. We will be generating a stack cookie check. In this case we can call a helper to fail fast.
    //
    // An example for these two cases is Windows Amd64, where the ABI requires to have 4 slots for
    // the outgoing arg space if the method makes any calls.
    if (m_outgoingArgSpaceSize < MIN_ARG_AREA_FOR_CALL)
    {
        if (comp->compUsesThrowHelper || comp->compIsProfilerHookNeeded() ||
            (comp->compMethodRequiresPInvokeFrame() && !comp->opts.ShouldUsePInvokeHelpers()) ||
            comp->getNeedsGSSecurityCookie())
        {
            m_outgoingArgSpaceSize = MIN_ARG_AREA_FOR_CALL;
            JITDUMP("Bumping outgoing arg space size to %u for possible helper or profile hook call",
                    m_outgoingArgSpaceSize);
        }
    }

    // If a function has localloc, we will need to move the outgoing arg space when the
    // localloc happens. When we do this, we need to maintain stack alignment. To avoid
    // leaving alignment-related holes when doing this move, make sure the outgoing
    // argument space size is a multiple of the stack alignment by aligning up to the next
    // stack alignment boundary.
    if (comp->compLocallocUsed)
    {
        m_outgoingArgSpaceSize = roundUp(m_outgoingArgSpaceSize, STACK_ALIGN);
        JITDUMP("Bumping outgoing arg space size to %u for localloc", m_outgoingArgSpaceSize);
    }

    assert((m_outgoingArgSpaceSize % TARGET_POINTER_SIZE) == 0);

    // Publish the final value and mark it as read only so any update
    // attempt later will cause an assert.
    comp->lvaOutgoingArgSpaceSize = m_outgoingArgSpaceSize;
    comp->lvaOutgoingArgSpaceSize.MarkAsReadOnly();
    comp->lvaGetDesc(comp->lvaOutgoingArgSpaceVar)->GrowBlockLayout(comp->typGetBlkLayout(m_outgoingArgSpaceSize));

    SetFramePointerFromArgSpaceSize();
#endif
}

//----------------------------------------------------------------------------------------------
// Lowering::SetFramePointerFromArgSpaceSize:
//   Set the frame pointer from the arg space size. This is a quirk because
//   StackLevelSetter used to do this even outside x86.
//
void Lowering::SetFramePointerFromArgSpaceSize()
{
    unsigned stackLevelSpace = m_outgoingArgSpaceSize;

    if (comp->compTailCallUsed)
    {
        // StackLevelSetter also used to count tailcalls.
        for (BasicBlock* block : comp->Blocks())
        {
            GenTreeCall* tailCall;
            if (block->endsWithTailCall(comp, true, false, &tailCall))
            {
                stackLevelSpace = max(stackLevelSpace, tailCall->gtArgs.OutgoingArgsStackSize());
            }
        }
    }

    unsigned stackLevel =
        (max(stackLevelSpace, (unsigned)MIN_ARG_AREA_FOR_CALL) - MIN_ARG_AREA_FOR_CALL) / TARGET_POINTER_SIZE;

    if (stackLevel >= 4)
    {
        comp->codeGen->setFramePointerRequired(true);
    }
}
