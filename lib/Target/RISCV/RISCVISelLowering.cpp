//===-- RISCVISelLowering.cpp - RISCV DAG Lowering Implementation  --------===//
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

#include "RISCVISelLowering.h"
#include "RISCV.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVRegisterInfo.h"
#include "RISCVSubtarget.h"
#include "RISCVTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-lower"

STATISTIC(NumLoopByVals, "Number of loops generated for byval arguments");

RISCVTargetLowering::RISCVTargetLowering(const TargetMachine &TM,
                                         const RISCVSubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {

  // Set up the register classes.
  addRegisterClass(MVT::i32, &RISCV::GPRRegClass);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(RISCV::X2_32);

  // Load extented operations for i1 types must be promoted
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD,  VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1,  Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1,  Promote);
  }

  // Handle integer types.
  for (unsigned I = MVT::FIRST_INTEGER_VALUETYPE;
       I <= MVT::LAST_INTEGER_VALUETYPE;
       ++I) {
    MVT VT = MVT::SimpleValueType(I);
    if (isTypeLegal(VT)) {
      if(Subtarget->hasM()) {
        if(Subtarget->isRV64() && VT==MVT::i32)
          setOperationAction(ISD::MUL  , VT, Promote);
        if(Subtarget->isRV32() && VT==MVT::i64)
          setOperationAction(ISD::MUL  , VT, Expand);
        setOperationAction(ISD::MUL  , VT, Legal);
        setOperationAction(ISD::MULHS, VT, Legal);
        setOperationAction(ISD::MULHU, VT, Legal);
        setOperationAction(ISD::SDIV , VT, Legal);
        setOperationAction(ISD::UDIV , VT, Legal);
        setOperationAction(ISD::SREM , VT, Legal);
        setOperationAction(ISD::UREM , VT, Legal);
      }else{
        setOperationAction(ISD::MUL  , VT, Expand);
        setOperationAction(ISD::MULHS, VT, Expand);
        setOperationAction(ISD::MULHU, VT, Expand);
        setOperationAction(ISD::SDIV , VT, Expand);
        setOperationAction(ISD::UDIV , VT, Expand);
        setOperationAction(ISD::SREM , VT, Expand);
        setOperationAction(ISD::UREM , VT, Expand);
      }

      //No support at all
      setOperationAction(ISD::SDIVREM, VT, Expand);
      setOperationAction(ISD::UDIVREM, VT, Expand);
      //RISCV doesn't support  [ADD,SUB][E,C]
      setOperationAction(ISD::ADDE, VT, Expand);
      setOperationAction(ISD::SUBE, VT, Expand);
      setOperationAction(ISD::ADDC, VT, Expand);
      setOperationAction(ISD::SUBC, VT, Expand);
      //RISCV doesn't support s[hl,rl,ra]_parts
      setOperationAction(ISD::SHL_PARTS, VT, Expand);
      setOperationAction(ISD::SRL_PARTS, VT, Expand);
      setOperationAction(ISD::SRA_PARTS, VT, Expand);
      //RISCV doesn't support rotl
      setOperationAction(ISD::ROTL, VT, Expand);
      setOperationAction(ISD::ROTR, VT, Expand);
      setOperationAction(ISD::SMUL_LOHI, VT, Expand);
      setOperationAction(ISD::UMUL_LOHI, VT, Expand);

      // Expand ATOMIC_LOAD and ATOMIC_STORE using ATOMIC_CMP_SWAP.
      // FIXME: probably much too conservative.
      setOperationAction(ISD::ATOMIC_LOAD,  VT, Expand);
      setOperationAction(ISD::ATOMIC_STORE, VT, Expand);

      // No special instructions for these.
      setOperationAction(ISD::CTPOP,           VT, Expand);
      setOperationAction(ISD::CTTZ,            VT, Expand);
      setOperationAction(ISD::CTLZ,            VT, Expand);
      setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Expand);
      setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Expand);

    }
  }

  // TODO: add all necessary setOperationAction calls

  setOperationAction(ISD::JumpTable, MVT::i32, Custom);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  setOperationAction(ISD::ROTR, MVT::i32, Expand);
  setOperationAction(ISD::ROTL, MVT::i32, Expand);

  setOperationAction(ISD::BR_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT, MVT::i32, Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Expand);

  setOperationAction(ISD::VASTART,          MVT::Other, Custom);
  setOperationAction(ISD::VAARG,            MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,           MVT::Other, Expand);
  setOperationAction(ISD::VAEND,            MVT::Other, Expand);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);

  setOperationAction(ISD::STACKSAVE,        MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,     MVT::Other, Expand);

  setOperationAction(ISD::BSWAP,            MVT::i32,   Expand);
  setOperationAction(ISD::BSWAP,            MVT::i64,   Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i32, Custom);

  setBooleanContents(ZeroOrOneBooleanContent);

  // Function alignments (log2)
  setMinFunctionAlignment(3);
  setPrefFunctionAlignment(3);

  // inline memcpy() for kernel to see explicit copy
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = 128;
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = 128;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = 128;
}

SDValue RISCVTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return lowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return lowerBlockAddress(Op, DAG);
  case ISD::JumpTable:
    return lowerJumpTable(Op, DAG);
  case ISD::SELECT_CC:
    return lowerSELECT_CC(Op, DAG);
  case ISD::VASTART:
    return lowerVASTART(Op, DAG);
  case ISD::RETURNADDR:
    return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  default:
    report_fatal_error("unimplemented operand");
  }
}

SDValue RISCVTargetLowering::lowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  int64_t Offset = N->getOffset();

  if (!isPositionIndependent()) {
    SDValue GAHi =
        DAG.getTargetGlobalAddress(GV, DL, Ty, Offset, RISCVII::MO_HI);
    SDValue GALo =
        DAG.getTargetGlobalAddress(GV, DL, Ty, Offset, RISCVII::MO_LO);
    SDValue MNHi = SDValue(DAG.getMachineNode(RISCV::LUI, DL, Ty, GAHi), 0);
    SDValue MNLo =
        SDValue(DAG.getMachineNode(RISCV::ADDI, DL, Ty, MNHi, GALo), 0);
    return MNLo;
  } else {
    llvm_unreachable("Unable to lowerGlobalAddress");
  }
}

SDValue RISCVTargetLowering::lowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  BlockAddressSDNode *BASDN = cast<BlockAddressSDNode>(Op);
  const BlockAddress *BA = BASDN->getBlockAddress();

  if (!isPositionIndependent()) {
    SDValue BAHi =
        DAG.getTargetBlockAddress(BA, Ty, 0, RISCVII::MO_HI);
    SDValue BALo =
        DAG.getTargetBlockAddress(BA, Ty, 0, RISCVII::MO_LO);
    SDValue MNHi = SDValue(DAG.getMachineNode(RISCV::LUI, DL, Ty, BAHi), 0);
    SDValue MNLo =
        SDValue(DAG.getMachineNode(RISCV::ADDI, DL, Ty, MNHi, BALo), 0);
    return MNLo;
  } else {
    llvm_unreachable("Unable to lowerBlockAddress");
  }
}

SDValue RISCVTargetLowering::lowerExternalSymbol(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  ExternalSymbolSDNode *N = cast<ExternalSymbolSDNode>(Op);
  const char *Sym = N->getSymbol();

  // TODO: should also handle gp-relative loads

  if (!isPositionIndependent()) {
    SDValue GAHi = DAG.getTargetExternalSymbol(Sym, Ty, RISCVII::MO_HI);
    SDValue GALo = DAG.getTargetExternalSymbol(Sym, Ty, RISCVII::MO_LO);
    SDValue MNHi = SDValue(DAG.getMachineNode(RISCV::LUI, DL, Ty, GAHi), 0);
    SDValue MNLo =
        SDValue(DAG.getMachineNode(RISCV::ADDI, DL, Ty, MNHi, GALo), 0);
    return MNLo;
  } else {
    llvm_unreachable("Unable to lowerExternalSymbol");
  }
}

SDValue RISCVTargetLowering::lowerJumpTable(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  JumpTableSDNode *N = cast<JumpTableSDNode>(Op);

  if (!isPositionIndependent()) {
    SDValue GAHi =
        DAG.getTargetJumpTable(N->getIndex(), Ty, RISCVII::MO_HI);
    SDValue GALo =
        DAG.getTargetJumpTable(N->getIndex(), Ty, RISCVII::MO_LO);
    SDValue MNHi = SDValue(DAG.getMachineNode(RISCV::LUI, DL, Ty, GAHi), 0);
    SDValue MNLo =
        SDValue(DAG.getMachineNode(RISCV::ADDI, DL, Ty, MNHi, GALo), 0);
    return MNLo;
  } else {
    llvm_unreachable("Unable to lowerGlobalAddress");
  }
}

SDValue RISCVTargetLowering::lowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  switch (CC) {
  default:
    break;
  case ISD::SETGT:
  case ISD::SETLE:
  case ISD::SETUGT:
  case ISD::SETULE:
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
    break;
  }

  SDValue TargetCC = DAG.getConstant(CC, DL, MVT::i32);

  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::Glue);
  SDValue Ops[] = {LHS, RHS, TargetCC, TrueV, FalseV};

  return DAG.getNode(RISCVISD::SELECT_CC, DL, VTs, Ops);
}

SDValue RISCVTargetLowering::lowerVASTART(SDValue Op,
                                          SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  RISCVMachineFunctionInfo *FuncInfo = MF.getInfo<RISCVMachineFunctionInfo>();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Frame index of first vararg argument
  SDValue FrameIndex =
      DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  // Create a store of the frame index to the location operand
  return DAG.getStore(Op.getOperand(0), SDLoc(Op), FrameIndex, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue
RISCVTargetLowering::getReturnAddressFrameIndex(SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  RISCVMachineFunctionInfo *FuncInfo = MF.getInfo<RISCVMachineFunctionInfo>();
  int ReturnAddrIndex = FuncInfo->getRAIndex();
  auto PtrVT = getPointerTy(MF.getDataLayout());

  if (ReturnAddrIndex == 0) {
    // Set up a frame object for the return address.
    uint64_t SlotSize = MF.getDataLayout().getPointerSize();
    ReturnAddrIndex = MF.getFrameInfo().CreateFixedObject(SlotSize, -SlotSize,
                                                          true);
    FuncInfo->setRAIndex(ReturnAddrIndex);
  }

  return DAG.getFrameIndex(ReturnAddrIndex, PtrVT);
}

SDValue RISCVTargetLowering::LowerRETURNADDR(SDValue Op,
                                             SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDLoc dl(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  if (Depth > 0) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset =
        DAG.getConstant(DAG.getDataLayout().getPointerSize(), dl, MVT::i32);
    return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, PtrVT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Just load the return address.
  SDValue RetAddrFI = getReturnAddressFrameIndex(DAG);
  return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), RetAddrFI,
                     MachinePointerInfo());
}

SDValue RISCVTargetLowering::LowerFRAMEADDR(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                                         RISCV::X8_32, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue RISCVTargetLowering::LowerConstantPool(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);
  const Constant *C = N->getConstVal();

  if (!isPositionIndependent()) {
    uint8_t OpFlagHi = RISCVII::MO_HI;
    uint8_t OpFlagLo = RISCVII::MO_LO;

    SDValue Hi = DAG.getTargetConstantPool(C, MVT::i32, N->getAlignment(),
                                           N->getOffset(), OpFlagHi);
    SDValue Lo = DAG.getTargetConstantPool(C, MVT::i32, N->getAlignment(),
                                           N->getOffset(), OpFlagLo);

    SDValue MNHi = SDValue(DAG.getMachineNode(RISCV::LUI, DL, Ty, Hi), 0);
    SDValue MNLo =
        SDValue(DAG.getMachineNode(RISCV::ADDI, DL, Ty, MNHi, Lo), 0);
    return MNLo;
  } else {
    llvm_unreachable("Unable to LowerConstantPool");
  }
}

MachineBasicBlock *
RISCVTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  case RISCV::COPY_STRUCT_BYVAL_I32:
    ++NumLoopByVals;
    return EmitStructByval(MI, BB);
  default: break;
  }

  assert(MI.getOpcode() == RISCV::Select && "Unexpected instr type to insert");

  // To "insert" a SELECT instruction, we actually have to insert the diamond
  // control-flow pattern.  The incoming instruction knows the destination vreg
  // to set, the condition code register to branch on, the true/false values to
  // select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  // ThisMBB:
  // ...
  //  TrueVal = ...
  //  jmp_XX r1, r2 goto Copy1MBB
  //  fallthrough --> Copy0MBB
  MachineBasicBlock *ThisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *Copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *Copy1MBB = F->CreateMachineBasicBlock(LLVM_BB);

  F->insert(I, Copy0MBB);
  F->insert(I, Copy1MBB);
  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi node for the select.
  Copy1MBB->splice(Copy1MBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
  Copy1MBB->transferSuccessorsAndUpdatePHIs(BB);
  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(Copy0MBB);
  BB->addSuccessor(Copy1MBB);

  // Insert Branch if Flag
  unsigned LHS = MI.getOperand(1).getReg();
  unsigned RHS = MI.getOperand(2).getReg();
  int CC = MI.getOperand(3).getImm();
  unsigned Opcode = -1;
  switch (CC) {
  case ISD::SETEQ:
    Opcode = RISCV::BEQ;
    break;
  case ISD::SETNE:
    Opcode = RISCV::BNE;
    break;
  case ISD::SETLT:
    Opcode = RISCV::BLT;
    break;
  case ISD::SETGE:
    Opcode = RISCV::BGE;
    break;
  case ISD::SETULT:
    Opcode = RISCV::BLTU;
    break;
  case ISD::SETUGE:
    Opcode = RISCV::BGEU;
    break;
  default:
    report_fatal_error("unimplemented select CondCode " + Twine(CC));
  }

  BuildMI(BB, DL, TII.get(Opcode))
    .addReg(LHS)
    .addReg(RHS)
    .addMBB(Copy1MBB);

  // Copy0MBB:
  //  %FalseValue = ...
  //  # fallthrough to Copy1MBB
  BB = Copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(Copy1MBB);

  // Copy1MBB:
  //  %Result = phi [ %FalseValue, Copy0MBB ], [ %TrueValue, ThisMBB ]
  // ...
  BB = Copy1MBB;
  BuildMI(*BB, BB->begin(), DL, TII.get(RISCV::PHI), MI.getOperand(0).getReg())
      .addReg(MI.getOperand(5).getReg())
      .addMBB(Copy0MBB)
      .addReg(MI.getOperand(4).getReg())
      .addMBB(ThisMBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

//===----------------------------------------------------------------------===//
//                       RISCV Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
TargetLowering::ConstraintType
RISCVTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return C_RegisterClass;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
RISCVTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC Constraint Letters
    switch (Constraint[0]) {
    default: break;
    case 'r':   // GENERAL_REGS
      return std::make_pair(0U, &RISCV::GPRRegClass);
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}


//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#include "RISCVGenCallingConv.inc"

// addLiveIn - This helper function adds the specified physical register to the
// MachineFunction as a live in value.  It also creates a corresponding
// virtual register for it.
static unsigned
addLiveIn(MachineFunction &MF, unsigned PReg, const TargetRegisterClass *RC)
{
  unsigned VReg = MF.getRegInfo().createVirtualRegister(RC);
  MF.getRegInfo().addLiveIn(PReg, VReg);
  return VReg;
}
static const MCPhysReg RISCVArgRegs[8] = {
    RISCV::X10_32, RISCV::X11_32, RISCV::X12_32, RISCV::X13_32,
    RISCV::X14_32, RISCV::X15_32, RISCV::X16_32, RISCV::X17_32
};

// RestoreVarArgRegs - Store VarArg register to the stack
void RISCVTargetLowering::RestoreVarArgRegs(std::vector<SDValue> &OutChains,
                                            SDValue Chain, const SDLoc &DL,
                                            SelectionDAG &DAG,
                                            CCState &State) const {
  ArrayRef<MCPhysReg> ArgRegs = makeArrayRef(RISCVArgRegs);
  unsigned Idx = State.getFirstUnallocated(ArgRegs);
  unsigned RegSizeInBytes = 4;
  MVT RegTy = MVT::getIntegerVT(RegSizeInBytes * 8);
  const TargetRegisterClass *RC = getRegClassFor(RegTy);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  RISCVMachineFunctionInfo *RISCVFI = MF.getInfo<RISCVMachineFunctionInfo>();
  // VaArgOffset is the va_start offset from stack pointer
  int VaArgOffset = 0;

  // All ArgRegs use to pass arguments
  if (ArgRegs.size() == Idx) {
    VaArgOffset = State.getNextStackOffset();
  //
  // ---------------- <-- va_start
  // | outgoing args|
  // ---------------- <-- sp
  // |   A0 ~ A7    |
  // ----------------
  }
  else {
    VaArgOffset = -(int)(RegSizeInBytes * (ArgRegs.size() - Idx));
  // E.g Idx = A3 which means f (a, b, c, ...)
  //
  // ---------------- <-- sp
  // |   A3 ~ A7    |
  // ---------------- <-- va_start
  // |   A0 ~ A2    |
  // ----------------
  }

  // Record the frame index of the first variable argument
  // which is a value necessary to VASTART.
  int VA_FI = MFI.CreateFixedObject(RegSizeInBytes, VaArgOffset, true);
  RISCVFI->setVarArgsFrameIndex(VA_FI);

  // Copy the integer registers that have not been used for argument passing
  // to the argument register save area. The save area is allocated in the
  // callee's stack frame.
  // For above case Idx = A3, generate store to push A3~A7 to callee stack
  // frame.
  for (unsigned I = Idx; I < ArgRegs.size();
       ++I, VaArgOffset += RegSizeInBytes) {
    unsigned Reg = addLiveIn(MF, ArgRegs[I], RC);
    SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegTy);
    int FI = MFI.CreateFixedObject(RegSizeInBytes, VaArgOffset, true);
    SDValue PtrOff = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    SDValue Store =
        DAG.getStore(Chain, DL, ArgValue, PtrOff, MachinePointerInfo());
    cast<StoreSDNode>(Store.getNode())->getMemOperand()->setValue(
        (Value *)nullptr);
    OutChains.push_back(Store);
  }
}

void RISCVTargetLowering::copyByValRegs(
    SDValue Chain, const SDLoc &DL, std::vector<SDValue> &OutChains,
    SelectionDAG &DAG, const ISD::ArgFlagsTy &Flags,
    SmallVectorImpl<SDValue> &InVals, const Argument *FuncArg,
    const CCValAssign &VA, CCState &CCInfo) const {
  ArrayRef<MCPhysReg> ArgRegs = makeArrayRef(RISCVArgRegs);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned FirstReg, LastReg;
  unsigned ByValIdx = CCInfo.getInRegsParamsProcessed();
  unsigned ByValArgsCount = CCInfo.getInRegsParamsCount();

  // The ByVal Argument pass by register
  if (ByValIdx < ByValArgsCount) {
    CCInfo.getInRegsParamInfo(ByValIdx, FirstReg, LastReg);
  // The ByVal Argument pass by stack
  } else {
    unsigned FirstRegIdx = CCInfo.getFirstUnallocated(ArgRegs);
    FirstReg = FirstRegIdx == 8 ? (unsigned)RISCV::X17_32 : ArgRegs[FirstRegIdx];
    LastReg = RISCV::X17_32;
  }

  unsigned GPRSizeInBytes = 4;
  unsigned NumRegs = LastReg - FirstReg;
  unsigned RegAreaSize = NumRegs * GPRSizeInBytes;
  unsigned FrameObjSize = std::max(Flags.getByValSize(), RegAreaSize);
  int FrameObjOffset;
  ArrayRef<MCPhysReg> ByValArgRegs = makeArrayRef(RISCVArgRegs);

  if (RegAreaSize)
    FrameObjOffset =
        - (int)((ByValArgRegs.size() - FirstReg) * GPRSizeInBytes);
  else
    FrameObjOffset = VA.getLocMemOffset();

  // Create frame object.
  EVT PtrTy = getPointerTy(DAG.getDataLayout());
  int FI = MFI.CreateFixedObject(FrameObjSize, FrameObjOffset, true);
  SDValue FIN = DAG.getFrameIndex(FI, PtrTy);
  InVals.push_back(FIN);

  if (!NumRegs)
    return;

  // Copy arg registers.
  MVT RegTy = MVT::getIntegerVT(GPRSizeInBytes * 8);
  const TargetRegisterClass *RC = getRegClassFor(RegTy);

  for (unsigned I = 0; I < NumRegs; ++I) {
    unsigned ArgReg = ByValArgRegs[FirstReg + I];
    unsigned VReg = addLiveIn(MF, ArgReg, RC);
    unsigned Offset = I * GPRSizeInBytes;
    SDValue StorePtr = DAG.getNode(ISD::ADD, DL, PtrTy, FIN,
                                   DAG.getConstant(Offset, DL, PtrTy));
    SDValue Store = DAG.getStore(Chain, DL, DAG.getRegister(VReg, RegTy),
                                 StorePtr, MachinePointerInfo(FuncArg, Offset));
    OutChains.push_back(Store);
  }
}

void RISCVTargetLowering::HandleByVal(CCState *State, unsigned &Size,
                                      unsigned Align) const {
  assert(Size && "Byval argument's size shouldn't be 0.");

  unsigned RegSizeInBytes = 4;
  ArrayRef<MCPhysReg> IntArgRegs = makeArrayRef(RISCVArgRegs);
  const MCPhysReg *ShadowRegs = IntArgRegs.data();

  Align = std::max(Align, 4U);

  unsigned Reg = State->AllocateReg(IntArgRegs);

  if (!Reg)
    return;

  unsigned AlignInRegs = Align / 4;

  // Calculate the register number we have to waste
  // to correct the ByVal arugment padding.
  unsigned Waste = (((RISCV::X17_32 - Reg) / 2) + 1) % AlignInRegs;

  for (unsigned i = 0; i < Waste; ++i)
    Reg = State->AllocateReg(IntArgRegs);

  if (!Reg)
    return;

  unsigned Excess = 4 * (RISCV::X10_32 - Reg);

  // Special case when NSAA != SP and parameter size greater than size of
  // all remained GPR regs. In that case we can't split parameter, we must
  // send it to stack.
  const unsigned NSAAOffset = State->getNextStackOffset();
  if (NSAAOffset != 0 && Size > Excess) {
    while (State->AllocateReg(IntArgRegs))
      ;
    return;
  }

  unsigned FirstRegIndex = 0;
  unsigned NumRegs = 0;

  if (State->getCallingConv() != CallingConv::Fast) {

    // Register number in RISCVGenRegisterInfo.inc interleave with 64 bit
    // registers, so we have to divide the register number by 2.
    FirstRegIndex = ((Reg - RISCV::X10_32) / 2);

    // Mark the registers allocated.
    Size = alignTo(Size, RegSizeInBytes);

    // Allocate the registers for ByVal arguments.
    for (unsigned I = FirstRegIndex; Size > 0 && (I < IntArgRegs.size());
         Size -= RegSizeInBytes, ++I, ++NumRegs)
      State->AllocateReg(IntArgRegs[I], ShadowRegs[I]);
  }

  // Specify the registers for passing ByVal arguments.
  State->addInRegsParamInfo(FirstRegIndex, FirstRegIndex + NumRegs);
}

// Transform physical registers into virtual registers
SDValue RISCVTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  RISCVMachineFunctionInfo *FI = MF.getInfo<RISCVMachineFunctionInfo>();

  FI->setVarArgsFrameIndex(0);
 
  // Used with vargs to accumulate store chains.
  std::vector<SDValue> OutChains;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;

  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  const Function *Func = DAG.getMachineFunction().getFunction();
  Function::const_arg_iterator FuncArg = Func->arg_begin();

  CCAssignFn *CC = IsVarArg ? CC_RISCV32_VAR : CC_RISCV32;
  CCInfo.AnalyzeFormalArguments(Ins, CC);

  unsigned CurArgIdx = 0;

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    unsigned offset = 0;
    CCValAssign &VA = ArgLocs[i];
    if (Ins[i].isOrigArg()) {
      std::advance(FuncArg, Ins[i].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[i].getOrigArgIndex();
    }
    ISD::ArgFlagsTy Flags = Ins[i].Flags;

    if (Flags.isByVal()) {
      assert(Ins[i].isOrigArg() && "Byval arguments cannot be implicit");

      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");

      copyByValRegs(Chain, DL, OutChains, DAG, Flags, InVals, &*FuncArg,
                    VA, CCInfo);
      CCInfo.nextInRegsParam();

      continue;
    }

    bool IsRegLoc = VA.isRegLoc();
    // Arguments stored on registers
    if (IsRegLoc) {
      MVT RegVT = VA.getLocVT();
      unsigned ArgReg = VA.getLocReg();
      const TargetRegisterClass *RC = getRegClassFor(RegVT);

      // Transform the arguments stored on
      // physical registers into virtual ones
      unsigned Reg = addLiveIn(DAG.getMachineFunction(), ArgReg, RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, Reg, RegVT);

      InVals.push_back(ArgValue);
    } else { // VA.isRegLoc()
      MVT LocVT = VA.getLocVT();

      // sanity check
      assert(VA.isMemLoc());

      // The stack pointer offset is relative to the caller stack frame.
      int FrameIdx = MFI.CreateFixedObject(LocVT.getSizeInBits() / 8,
                                      VA.getLocMemOffset(), true);
      // Create load nodes to retrieve arguments from the stack
      SDValue FIN = DAG.getFrameIndex(FrameIdx,
                                      getPointerTy(DAG.getDataLayout()));
      SDValue ArgValue = DAG.getLoad(
          LocVT, DL, Chain, FIN,
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(),
                                            FrameIdx));
      OutChains.push_back(ArgValue.getValue(1));

      InVals.push_back(ArgValue);
    }
  }

  // Push VarArg Registers to the stack
  if (IsVarArg)
    RestoreVarArgRegs(OutChains, Chain, DL, DAG, CCInfo);

  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens when on varg functions
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  }

  return Chain;
}

/// LowerMemOpCallTo - Store the argument to the stack.
SDValue RISCVTargetLowering::LowerMemOpCallTo(SDValue Chain, SDValue StackPtr,
                                              SDValue Arg, const SDLoc &dl,
                                              SelectionDAG &DAG,
                                              const CCValAssign &VA,
                                              ISD::ArgFlagsTy Flags) const {
  unsigned LocMemOffset = VA.getLocMemOffset();
  SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
  PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(DAG.getDataLayout()),
                       StackPtr, PtrOff);
  return DAG.getStore(
      Chain, dl, Arg, PtrOff,
      MachinePointerInfo::getStack(DAG.getMachineFunction(), LocMemOffset));
}

// Copy byVal arg to registers and stack.
void RISCVTargetLowering::passByValArg(
    SDValue Chain, const SDLoc &DL,
    RegsToPassVector &RegsToPass,
    SmallVectorImpl<SDValue> &MemOpChains, SDValue StackPtr,
    MachineFrameInfo &MFI, SelectionDAG &DAG, SDValue Arg, unsigned FirstReg,
    unsigned LastReg, const ISD::ArgFlagsTy &Flags, bool isLittle,
    const CCValAssign &VA) const {
  unsigned ByValSizeInBytes = Flags.getByValSize();
  unsigned OffsetInBytes = 0; // From beginning of struct
  unsigned RegSizeInBytes = 4;
  unsigned Alignment = std::min(Flags.getByValAlign(), RegSizeInBytes);

  EVT PtrTy = getPointerTy(DAG.getDataLayout()),
      RegTy = MVT::getIntegerVT(RegSizeInBytes * 8);
  unsigned NumRegs = LastReg - FirstReg;

  if (NumRegs) {
    ArrayRef<MCPhysReg> ArgRegs = makeArrayRef(RISCVArgRegs);
    bool LeftoverBytes = (NumRegs * RegSizeInBytes > ByValSizeInBytes);
    unsigned I = 0;

    // Copy words to registers.
    for (; I < NumRegs - LeftoverBytes; ++I, OffsetInBytes += RegSizeInBytes) {
      SDValue LoadPtr = DAG.getNode(ISD::ADD, DL, PtrTy, Arg,
                                    DAG.getConstant(OffsetInBytes, DL, PtrTy));
      SDValue LoadVal = DAG.getLoad(RegTy, DL, Chain, LoadPtr,
                                    MachinePointerInfo(), Alignment);
      MemOpChains.push_back(LoadVal.getValue(1));
      unsigned ArgReg = ArgRegs[FirstReg + I];
      RegsToPass.push_back(std::make_pair(ArgReg, LoadVal));
    }

    // Return if the struct has been fully copied.
    if (ByValSizeInBytes == OffsetInBytes)
      return;

    // Copy the remainder of the byval argument with sub-word loads and shifts.
    if (LeftoverBytes) {
      SDValue Val;

      for (unsigned LoadSizeInBytes = RegSizeInBytes / 2, TotalBytesLoaded = 0;
           OffsetInBytes < ByValSizeInBytes; LoadSizeInBytes /= 2) {
        unsigned RemainingSizeInBytes = ByValSizeInBytes - OffsetInBytes;

        if (RemainingSizeInBytes < LoadSizeInBytes)
          continue;

        // Load subword.
        SDValue LoadPtr = DAG.getNode(ISD::ADD, DL, PtrTy, Arg,
                                      DAG.getConstant(OffsetInBytes, DL,
                                                      PtrTy));
        SDValue LoadVal = DAG.getExtLoad(
            ISD::ZEXTLOAD, DL, RegTy, Chain, LoadPtr, MachinePointerInfo(),
            MVT::getIntegerVT(LoadSizeInBytes * 8), Alignment);
        MemOpChains.push_back(LoadVal.getValue(1));

        // Shift the loaded value.
        unsigned Shamt;

        if (isLittle)
          Shamt = TotalBytesLoaded * 8;
        else
          Shamt = (RegSizeInBytes - (TotalBytesLoaded + LoadSizeInBytes)) * 8;

        SDValue Shift = DAG.getNode(ISD::SHL, DL, RegTy, LoadVal,
                                    DAG.getConstant(Shamt, DL, MVT::i32));

        if (Val.getNode())
          Val = DAG.getNode(ISD::OR, DL, RegTy, Val, Shift);
        else
          Val = Shift;

        OffsetInBytes += LoadSizeInBytes;
        TotalBytesLoaded += LoadSizeInBytes;
        Alignment = std::min(Alignment, LoadSizeInBytes);
      }

      unsigned ArgReg = ArgRegs[FirstReg + I];
      RegsToPass.push_back(std::make_pair(ArgReg, Val));
      return;
    }
  }

  // Copy remainder of byval arg to it with memcpy.
  unsigned MemCpySize = ByValSizeInBytes - OffsetInBytes;
  SDValue Src = DAG.getNode(ISD::ADD, DL, PtrTy, Arg,
                            DAG.getConstant(OffsetInBytes, DL, PtrTy));
  SDValue Dst = DAG.getNode(ISD::ADD, DL, PtrTy, StackPtr,
                            DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));
  Chain = DAG.getMemcpy(Chain, DL, Dst, Src,
                        DAG.getConstant(MemCpySize, DL, PtrTy),
                        Alignment, /*isVolatile=*/false, /*AlwaysInline=*/false,
                        /*isTailCall=*/false,
                        MachinePointerInfo(), MachinePointerInfo());
  MemOpChains.push_back(Chain);
}

// Lower a call to a callseq_start + CALL + callseq_end chain, and add input 
// and output parameter nodes.
SDValue
RISCVTargetLowering::LowerCall(CallLoweringInfo &CLI,
                               SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CLI.IsTailCall = false;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;

  const TargetFrameLowering *TFL = Subtarget->getFrameLowering();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Analyze the operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState ArgCCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCAssignFn *CC = IsVarArg ? CC_RISCV32_VAR : CC_RISCV32;
  ArgCCInfo.AnalyzeCallOperands(Outs, CC);


  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NextStackOffset = ArgCCInfo.getNextStackOffset();

  // Chain is the output chain of the last Load/Store or CopyToReg node.
  // ByValChain is the output chain of the last Memcpy node created for copying
  // byval arguments to the stack.
  unsigned StackAlignment = TFL->getStackAlignment();
  NextStackOffset = alignTo(NextStackOffset, StackAlignment);
  SDValue NextStackOffsetVal = DAG.getIntPtrConstant(NextStackOffset, DL, true);

  if (!CLI.IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NextStackOffsetVal, DL);

  // Copy argument values to their designated locations.
  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  SDValue StackPtr = DAG.getCopyFromReg(Chain, DL, RISCV::X2_32,
                                        getPointerTy(DAG.getDataLayout()));

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue Arg = OutVals[I];
    ISD::ArgFlagsTy Flags = Outs[I].Flags;

    // ByVal Arg.
    if (Flags.isByVal()) {
      unsigned offset = 0;
      unsigned FirstByValReg, LastByValReg;
      unsigned ByValIdx = ArgCCInfo.getInRegsParamsProcessed();
      unsigned ByValArgsCount = ArgCCInfo.getInRegsParamsCount();

      if (ByValIdx < ByValArgsCount) {
        ArgCCInfo.getInRegsParamInfo(ByValIdx, FirstByValReg, LastByValReg);

        assert(Flags.getByValSize() &&
               "ByVal args of size 0 should have been ignored by front-end.");
        assert(ByValIdx < ArgCCInfo.getInRegsParamsCount());
        assert(!CLI.IsTailCall &&
               "Do not tail-call optimize if there is a byval argument.");
        passByValArg(Chain, DL, RegsToPass, MemOpChains, StackPtr, MFI, DAG, Arg,
                     FirstByValReg, LastByValReg, Flags, true, VA);
        ArgCCInfo.nextInRegsParam();

        // If parameter size outsides register area, "offset" value
        // helps us to calculate stack slot for remained part properly.
        offset = LastByValReg - FirstByValReg;
      }
      if (Flags.getByValSize() > 4*offset) {
        auto PtrVT = getPointerTy(DAG.getDataLayout());
        unsigned LocMemOffset = VA.getLocMemOffset();
        SDValue StkPtrOff = DAG.getIntPtrConstant(LocMemOffset, DL);
        SDValue Dst = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, StkPtrOff);
        SDValue SrcOffset = DAG.getIntPtrConstant(4*offset, DL);
        SDValue Src = DAG.getNode(ISD::ADD, DL, PtrVT, Arg, SrcOffset);
        SDValue SizeNode = DAG.getConstant(Flags.getByValSize() - 4*offset, DL,
                                           MVT::i32);
        SDValue AlignNode = DAG.getConstant(Flags.getByValAlign(), DL,
                                              MVT::i32);

        SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
        SDValue Ops[] = { Chain, Dst, Src, SizeNode, AlignNode};
        MemOpChains.push_back(DAG.getNode(RISCVISD::COPY_STRUCT_BYVAL, DL, VTs,
                                          Ops));
      }
      continue;
    }

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), Arg);
      break;
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      assert(VA.isMemLoc());
      MemOpChains.push_back(LowerMemOpCallTo(Chain, StackPtr, Arg,
                                             DL, DAG, VA, Flags));
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain and
  // flag operands which copy the outgoing args into registers.  The InFlag in
  // necessary since all emitted instructions must be stuck together.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, DL, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  if (isa<GlobalAddressSDNode>(Callee)) {
    Callee = lowerGlobalAddress(Callee, DAG);
  } else if (isa<ExternalSymbolSDNode>(Callee)) {
    Callee = lowerExternalSymbol(Callee, DAG);
  }

  // The first call operand is the chain and the second is the target address.
  std::vector<SDValue> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved(callee saved)
  // registers. Let register allocator could get correct register live time
  // cross call.
  const TargetRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CLI.CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(RISCVISD::CALL, DL, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NextStackOffsetVal,
                             DAG.getIntPtrConstant(0, DL, true), InFlag, DL);

  InFlag = Chain.getValue(1);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  CCInfo.AnalyzeCallResult(Ins, RetCC_RISCV32);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned I = 0; I != RVLocs.size(); ++I) {
    Chain = DAG.getCopyFromReg(Chain, DL, RVLocs[I].getLocReg(),
                               RVLocs[I].getValVT(), InFlag).getValue(1);
    InFlag = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

SDValue
RISCVTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {

  // Stores the assignment of the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;

  // Info about the registers and stack slot
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  CCInfo.AnalyzeReturn(Outs, RetCC_RISCV32);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0, e = RVLocs.size(); i < e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[i], Flag);

    // Guarantee that all emitted copies are stuck together
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain; // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode()) {
    RetOps.push_back(Flag);
  }

  return DAG.getNode(RISCVISD::RET_FLAG, DL, MVT::Other, RetOps);
}

const char *RISCVTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((RISCVISD::NodeType)Opcode) {
  case RISCVISD::FIRST_NUMBER:
    break;
  case RISCVISD::RET_FLAG:
    return "RISCVISD::RET_FLAG";
  case RISCVISD::CALL:
    return "RISCVISD::CALL";
  case RISCVISD::SELECT_CC:
    return "RISCVISD::SELECT_CC";
  case RISCVISD::COPY_STRUCT_BYVAL:
    return "RISCVISD::COPY_STRUCT_BYVAL";
  }
  return nullptr;
}

MachineBasicBlock *
RISCVTargetLowering::EmitStructByval(MachineInstr &MI,
                                     MachineBasicBlock *BB) const {
  const TargetRegisterClass *TRC = nullptr;
  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const RISCVInstrInfo *TII = Subtarget->getInstrInfo();

  // This pseudo instruction has 3 operands: dst, src, size
  // We expand it to a loop if size > Subtarget->getMaxInlineSizeThreshold().
  // Otherwise, we will generate unrolled scalar copies.
  unsigned dest = MI.getOperand(0).getReg();
  unsigned src = MI.getOperand(1).getReg();
  unsigned SizeVal = MI.getOperand(2).getImm();
  unsigned Align = MI.getOperand(3).getImm();
  DebugLoc DL = MI.getDebugLoc();
  TRC = &RISCV::GPRRegClass;
  unsigned UnitSize = 0;
  unsigned LoadInst = RISCV::LW;
  unsigned StoreInst = RISCV::SW;

  if (Align & 1) {
    UnitSize = 1;
    LoadInst = RISCV::LB;
    StoreInst = RISCV::SB;
  } else if (Align & 2) {
    UnitSize = 2;
    LoadInst = RISCV::LH;
    StoreInst = RISCV::SH;
  } else
    UnitSize = 4;

  unsigned BytesLeft = SizeVal % UnitSize;
  unsigned LoopSize = SizeVal - BytesLeft;

  // Expand to a loop if SizeVal > MaxCopyThreshold.
  unsigned MaxCopyThreshold = 64;

  if (SizeVal <= MaxCopyThreshold) {
    // Use LDR and STR to copy.
    // [scratch, srcOut] = LDR_POST(srcIn, UnitSize)
    // [destOut] = STR_POST(scratch, destIn, UnitSize)
    for (unsigned i = 0; i < LoopSize; i+=UnitSize) {
      unsigned scratch = MRI.createVirtualRegister(TRC);

        BuildMI(*BB, MI, DL, TII->get(LoadInst))
          .addReg(scratch, RegState::Define)
          .addReg(src)
          .addImm(i);

        BuildMI(*BB, MI, DL, TII->get(StoreInst))
          .addReg(scratch)
          .addReg(dest)
          .addImm(i);

    }

    // Handle the leftover bytes with LDRB and STRB.
    // [scratch, srcOut] = LDRB_POST(srcIn, 1)
    // [destOut] = STRB_POST(scratch, destIn, 1)
    for (unsigned i = 0; i < BytesLeft; i++) {
      unsigned scratch = MRI.createVirtualRegister(TRC);

      BuildMI(*BB, MI, DL, TII->get(RISCV::LB))
        .addReg(scratch, RegState::Define)
        .addReg(src)
        .addImm(i);

      BuildMI(*BB, MI, DL, TII->get(RISCV::SB))
        .addReg(scratch)
        .addReg(dest)
        .addImm(i);
    }

    MI.eraseFromParent(); // The instruction is gone now.
    return BB;
  }

  // Generate loop to push ByVal arguments to the stack.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  MachineBasicBlock *loopMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(It, loopMBB);
  MF->insert(It, exitMBB);

  // Transfer the remainder of BB and its successor edges to exitMBB.
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  unsigned varEnd = MRI.createVirtualRegister(TRC);
  TII->loadImmediate (varEnd, LoopSize, *BB, MachineBasicBlock::iterator(MI), DL);
  BB->addSuccessor(loopMBB);

  // Generate the loop body:
  //   varPhi = PHI(varLoop, varEnd)
  //   srcPhi = PHI(srcLoop, src)
  //   destPhi = PHI(destLoop, dst)
  MachineBasicBlock *entryBB = BB;
  BB = loopMBB;
  unsigned varLoop = MRI.createVirtualRegister(TRC);
  unsigned varPhi = MRI.createVirtualRegister(TRC);
  unsigned srcLoop = MRI.createVirtualRegister(TRC);
  unsigned srcPhi = MRI.createVirtualRegister(TRC);
  unsigned destLoop = MRI.createVirtualRegister(TRC);
  unsigned destPhi = MRI.createVirtualRegister(TRC);
  unsigned data = MRI.createVirtualRegister(TRC);

  BuildMI(*BB, BB->begin(), DL, TII->get(RISCV::PHI), varPhi)
    .addReg(varLoop).addMBB(loopMBB)
    .addReg(varEnd).addMBB(entryBB);
  BuildMI(BB, DL, TII->get(RISCV::PHI), srcPhi)
    .addReg(srcLoop).addMBB(loopMBB)
    .addReg(src).addMBB(entryBB);
  BuildMI(BB, DL, TII->get(RISCV::PHI), destPhi)
    .addReg(destLoop).addMBB(loopMBB)
    .addReg(dest).addMBB(entryBB);


  // Load Argument
  BuildMI(*BB, BB->end(), DL, TII->get(LoadInst))
    .addReg(data, RegState::Define)
    .addReg(srcPhi)
    .addImm(0);

  // Store Argument to the stack
  BuildMI(*BB, BB->end(), DL, TII->get(StoreInst))
    .addReg(data)
    .addReg(destPhi)
    .addImm(0);

  // Increase src base by UnitSize.
  BuildMI(*BB, BB->end(), DL, TII->get(RISCV::ADDI))
    .addReg(srcLoop, RegState::Define)
    .addReg(srcPhi)
    .addImm(UnitSize);

  // Increase dest base by UnitSize.
  BuildMI(*BB, BB->end(), DL, TII->get(RISCV::ADDI))
    .addReg(destLoop, RegState::Define)
    .addReg(destPhi)
    .addImm(UnitSize);

  // Decrease loop variable by UnitSize.
  BuildMI(*BB, BB->end(), DL, TII->get(RISCV::ADDI))
    .addReg(varLoop, RegState::Define)
    .addReg(varPhi)
    .addImm(SignExtend64<12>(-UnitSize));


  BuildMI(*BB, BB->end(), DL, TII->get(RISCV::BNE))
    .addReg(varLoop)
    .addReg(RISCV::X0_32)
    .addMBB(loopMBB);

  // loopMBB can loop back to loopMBB or fall through to exitMBB.
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  // Add epilogue to handle BytesLeft.
  BB = exitMBB;
  auto StartOfExit = exitMBB->begin();

  //   [scratch, srcOut] = LDRB_POST(srcLoop, 1)
  //   [destOut] = STRB_POST(scratch, destLoop, 1)
  unsigned srcIn = srcLoop;
  unsigned destIn = destLoop;
  for (unsigned i = 0; i < BytesLeft; i++) {
    unsigned scratch = MRI.createVirtualRegister(TRC);

    BuildMI(*BB, StartOfExit, DL, TII->get(RISCV::LB))
      .addReg(scratch, RegState::Define)
      .addReg(srcIn)
      .addImm(i);

    BuildMI(*BB, StartOfExit, DL, TII->get(RISCV::SB))
      .addReg(scratch)
      .addReg(destIn)
      .addImm(i);
  }

  MI.eraseFromParent(); // The instruction is gone now.
  return BB;
}
