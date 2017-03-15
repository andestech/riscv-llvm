//===-- RISCVISelLowering.h - RISCV DAG Lowering Interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that RISCV uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVISELLOWERING_H
#define LLVM_LIB_TARGET_RISCV_RISCVISELLOWERING_H

#include "RISCV.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Target/TargetLowering.h"

namespace llvm {
class RISCVSubtarget;
namespace RISCVISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET_FLAG,
  CALL,
  // Add pseudo op to model memcpy for struct byval.
  COPY_STRUCT_BYVAL,
  SELECT_CC
};
}

class RISCVTargetLowering : public TargetLowering {
  const RISCVSubtarget *Subtarget;

public:
  explicit RISCVTargetLowering(const TargetMachine &TM, const RISCVSubtarget &STI);

  // Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  // This method returns the name of a target specific DAG node.
  const char *getTargetNodeName(unsigned Opcode) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

  MachineBasicBlock *EmitStructByval(MachineInstr &MI,
                                     MachineBasicBlock *MBB) const;
private:
  // Lower incoming arguments, copy physregs into vregs
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                         Type *Ty) const override {
    return true;
  }
  SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG) const;

  SDValue getReturnAddressFrameIndex(SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;


  typedef SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPassVector;

  SDValue LowerMemOpCallTo(SDValue Chain, SDValue StackPtr, SDValue Arg,
                           const SDLoc &dl, SelectionDAG &DAG,
                           const CCValAssign &VA,
                           ISD::ArgFlagsTy Flags) const;

  void HandleByVal(CCState *, unsigned &, unsigned) const override;

  /// copyByValArg - Copy argument registers which were used to pass a byval
  /// argument to the stack. Create a stack frame object for the byval
  /// argument.
  void copyByValRegs(SDValue Chain, const SDLoc &DL,
                     std::vector<SDValue> &OutChains, SelectionDAG &DAG,
                     const ISD::ArgFlagsTy &Flags,
                     SmallVectorImpl<SDValue> &InVals,
                     const Argument *FuncArg, const CCValAssign &VA,
                     CCState &CCInfo) const;

  /// passByValArg - Pass a byval argument in registers or on stack.
  void passByValArg(SDValue Chain, const SDLoc &DL,
                    RegsToPassVector &RegsToPass,
                    SmallVectorImpl<SDValue> &MemOpChains, SDValue StackPtr,
                    MachineFrameInfo &MFI, SelectionDAG &DAG, SDValue Arg,
                    unsigned FirstReg, unsigned LastReg,
                    const ISD::ArgFlagsTy &Flags, bool isLittle,
                    const CCValAssign &VA) const;

  /// RestoreVarArgRegs - Restore variable function arguments passed in
  /// registers to the stack. Also create a stack frame object for the
  /// first variable argument.
  void RestoreVarArgRegs(std::vector<SDValue> &OutChains, SDValue Chain,
                         const SDLoc &DL, SelectionDAG &DAG,
                         CCState &State) const;

  TargetLowering::ConstraintType
  getConstraintType(StringRef Constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;


};
}

#endif
