//===-- RISCVAsmParser.cpp - Parse RISCV assembly to MCInst instructions --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "MCTargetDesc/RISCVMCExpr.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

namespace {
struct RISCVOperand;

struct ParseError {
    SMLoc ErrorLoc;
    Twine ErrorMsg;
};

template<typename T>
struct ParseSuccess {
    T Val;
    SMLoc StartLoc;
    SMLoc EndLoc;

    ParseSuccess(const T& V, const SMLoc S, const SMLoc E)
        : Val{V}, StartLoc{S}, EndLoc{E} {}

    template<typename U>
    ParseSuccess(const ParseSuccess<U>& R)
        : Val{R.Val}, StartLoc{R.StartLoc}, EndLoc{R.EndLoc} {}
};

template<>
struct ParseSuccess<void> {
    SMLoc StartLoc;
    SMLoc EndLoc;
};

template<typename T>
class ParseResult {
    enum class KindTy { Error, Success } Kind;

    union {
        ParseSuccess<T> Success;
        ParseError Error;
    };

public:
    ParseResult(const ParseSuccess<T>& S) : Kind{KindTy::Success}, Success{S} {};
    ParseResult(const ParseError& E) : Kind{KindTy::Error}, Error{E} {};

    operator bool() const { return Kind == KindTy::Success; }

    ParseSuccess<T>& operator*() { return Kind == KindTy::Success ? Success : nullptr; }
    const ParseSuccess<T>& operator*() const { return Kind == KindTy::Success ? Success : nullptr; }

    ParseSuccess<T>* operator->() { return Kind == KindTy::Success ? &Success : nullptr; }
    const ParseSuccess<T>* operator->() const { return Kind == KindTy::Success ? &Success : nullptr; }

    ParseError* getError() { return Kind == KindTy::Error ? &Error : nullptr; }
    const ParseError* getError() const { return Kind == KindTy::Error ? &Error : nullptr; }
};

class RISCVAsmParser : public MCTargetAsmParser {
  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

  bool generateImmOutOfRangeError(OperandVector &Operands, uint64_t ErrorInfo,
                                  int Lower, int Upper, Twine Msg);

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  Optional<unsigned> matchCPURegisterName(StringRef Symbol, bool Reg32Bit) const;

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool ParseDirective(AsmToken DirectiveID) override;

// Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "RISCVGenAsmMatcher.inc"

  ParseResult<void> parseLeftParen();
  ParseResult<void> parseRightParen();
  ParseResult<StringRef> peekIdentifier();
  ParseResult<StringRef> parseIdentifier();
  ParseResult<unsigned> parseRegister(bool Reg32Bit);
  ParseResult<const MCExpr*> parseExpression();
  ParseResult<const MCExpr*> parseImmediate();

  bool parseOperand(OperandVector &Operands, bool Reg32Bit);

public:
  enum RISCVMatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "RISCVGenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  static bool classifySymbolRef(const MCExpr *Expr,
                                RISCVMCExpr::VariantKind &Kind,
                                int64_t &Addend);

  RISCVAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

  const MCExpr *createTargetUnaryExpr(const MCExpr *E,
                                      AsmToken::TokenKind OperatorToken,
                                      MCContext &Ctx) override {
    switch(OperatorToken) {
    default:
      llvm_unreachable("Unknown token");
      return nullptr;
    case AsmToken::PercentHi:
      return RISCVMCExpr::create(E, RISCVMCExpr::VK_RISCV_HI, Ctx);
    case AsmToken::PercentLo:
      return RISCVMCExpr::create(E, RISCVMCExpr::VK_RISCV_LO, Ctx);
    case AsmToken::PercentPcrel_Hi:
      return RISCVMCExpr::create(E, RISCVMCExpr::VK_RISCV_PCREL_HI, Ctx);

    }
  }
};

/// RISCVOperand - Instances of this class represent a parsed machine
/// instruction
struct RISCVOperand : public MCParsedAsmOperand {
  struct MemTy {
    const MCExpr *Offset;
    unsigned Reg;
  };

  enum KindTy {
    Token,
    Register,
    Memory,
    Immediate,
  } Kind;

  SMLoc StartLoc, EndLoc;

  union {
    StringRef Tok;
    unsigned Reg;
    const MCExpr* Imm;
    MemTy Mem;
  };

  // Constructors
  explicit RISCVOperand(StringRef Tok_, SMLoc S, SMLoc E)
    : Kind(Token), StartLoc(S), EndLoc(E), Tok(Tok_) {}

  explicit RISCVOperand(unsigned Reg_, SMLoc S, SMLoc E)
    : Kind(Register), StartLoc(S), EndLoc(E), Reg(Reg_) {}

  explicit RISCVOperand(const MCExpr* const Imm_, SMLoc S, SMLoc E)
    : Kind(Immediate), StartLoc(S), EndLoc(E), Imm(Imm_) {}

  explicit RISCVOperand(const MCExpr* Offset, const unsigned Reg, SMLoc S, SMLoc E)
    : Kind(Memory), StartLoc(S), EndLoc(E), Mem{Offset, Reg} {}

  // Copy constructor
  RISCVOperand(const RISCVOperand &rhs)
    : MCParsedAsmOperand(),
      Kind(rhs.Kind),
      StartLoc(rhs.StartLoc),
      EndLoc(rhs.EndLoc) {
    switch (Kind) {
    case Token:
      Tok = rhs.Tok;
      break;
    case Register:
      Reg = rhs.Reg;
      break;
    case Immediate:
      Imm = rhs.Imm;
      break;
    case Memory:
      Mem = rhs.Mem;
      break;
    }
  }

  bool isToken() const override { return Kind == Token; }
  bool isReg() const override { return Kind == Register; }
  bool isImm() const override { return Kind == Immediate; }
  bool isMem() const override { return Kind == Memory; }

  bool isConstantImm() const {
    return isImm() && dyn_cast<MCConstantExpr>(getImm());
  }

  bool isGPRAsmReg() const {
    return isReg();
  }

  // Predicate methods for AsmOperands defined in RISCVInstrInfo.td

  bool isUImm4() const {
    return (isConstantImm() && isUInt<4>(getConstantImm()));
  }

  bool isUImm5() const {
    return (isConstantImm() && isUInt<5>(getConstantImm()));
  }

  bool isUImm6() const {
    return (isConstantImm() && isUInt<6>(getConstantImm()));
  }

  bool isS12Imm() const {
    return isSImm12();
  }

  bool isSImm6() const {
    if (isConstantImm()) {
      return isInt<6>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      if (!RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend))
        return false;
      return VK == RISCVMCExpr::VK_RISCV_LO;
    }
    return false;
  }

  bool isSImm12() const {
    if (isConstantImm()) {
      return isInt<12>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      if (!RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend))
        return false;
      return VK == RISCVMCExpr::VK_RISCV_LO;
    }
    return false;
  }

  bool isUImm12() const {
    return (isConstantImm() && isUInt<12>(getConstantImm()));
  }

  bool isUImm10_2Lsb0() const {
    if (isConstantImm()) {
      return isShiftedUInt<8, 2>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      return RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend);
    }
    return false;
  }

  bool isSImm10_4Lsb0() const {
    if (isConstantImm()) {
      return isShiftedInt<6, 4>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      return RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend);
    }
    return false;
  }

  bool isSImm9Lsb0() const {
    if (isConstantImm()) {
      return isShiftedInt<8, 1>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      return RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend);
    }
    return false;
  }

  bool isSImm12Lsb0() const {
    if (isConstantImm()) {
      return isShiftedInt<11, 1>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      return RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend);
    }
    return false;
  }

  bool isSImm13Lsb0() const {
    if (isConstantImm()) {
      return isShiftedInt<12, 1>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      return RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend);
    }
    return false;
  }

  bool isUImm20() const {
    if (isConstantImm()) {
      return isUInt<20>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      if (!RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend))
        return false;
      return VK == RISCVMCExpr::VK_RISCV_HI || VK == RISCVMCExpr::VK_RISCV_PCREL_HI;
    }
    return false;
  }

  // Reg + imm12s
  bool isAddrRegImm12s() const {
    return isMem();
  }

  // Sp + imm6u << 2
  bool isAddrSpImm6uWord() const {
    if (!isMem()) return false;
    if (Mem.Reg != RISCV::X2_32 &&
        Mem.Reg != RISCV::X2_64)
      return false;
    if (!Mem.Offset) return true;
    if (!dyn_cast<MCConstantExpr>(Mem.Offset))
      return false;
    int64_t Val = static_cast<const MCConstantExpr*>(Mem.Offset)->getValue();
    return isUIntN(6 + 2, Val) && (Val % 4 == 0);
  }

   // Sp + imm6u << 3
  bool isAddrSpImm6uDouble() const {
    if (!isMem()) return false;
    if (Mem.Reg != RISCV::X2_32 &&
        Mem.Reg != RISCV::X2_64)
      return false;
    if (!Mem.Offset) return true;
    if (!dyn_cast<MCConstantExpr>(Mem.Offset))
      return false;
    int64_t Val = static_cast<const MCConstantExpr*>(Mem.Offset)->getValue();
    return isUIntN(6 + 3, Val) && (Val % 8 == 0);
  }
 
  bool isRVC3BitReg(unsigned Reg) const {
    if (Reg >= RISCV::X8_64 && Reg <= RISCV::X15_64)
      return true;
    if (Reg >= RISCV::X8_32 && Reg <= RISCV::X15_32)
      return true;
    return false;
  }

  // Reg + imm5u << 2
  bool isAddrRegImm5uWord() const {
    if (!isMem()) return false;
    if (!isRVC3BitReg(Mem.Reg))
      return false;
    if (!Mem.Offset) return true;
    if (!dyn_cast<MCConstantExpr>(Mem.Offset))
      return false;
    int64_t Val = static_cast<const MCConstantExpr*>(Mem.Offset)->getValue();
    return isUIntN(5 + 2, Val) && (Val % 4 == 0);
  }

  // Reg + imm5u << 3
  bool isAddrRegImm5uDouble() const {
    if (!isMem()) return false;
    if (!isRVC3BitReg(Mem.Reg))
      return false;
    if (!Mem.Offset) return true;
    if (!dyn_cast<MCConstantExpr>(Mem.Offset))
      return false;
    int64_t Val = static_cast<const MCConstantExpr*>(Mem.Offset)->getValue();
    return isUIntN(5 + 3, Val) && (Val % 8 == 0);
  }

  bool isSImm21Lsb0() const {
    if (isConstantImm()) {
      return isShiftedInt<20, 1>(getConstantImm());
    } else if (isImm()) {
      RISCVMCExpr::VariantKind VK;
      int64_t Addend;
      return RISCVAsmParser::classifySymbolRef(getImm(), VK, Addend);
    }
    return false;
  }

  /// getStartLoc - Gets location of the first token of this operand
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Gets location of the last token of this operand
  SMLoc getEndLoc() const override { return EndLoc; }

  unsigned getReg() const override {
    assert(isReg() && "Invalid type access as register!");
    return Reg;
  }

  const MCExpr* getImm() const {
    assert(isImm() && "Invalid type access as immediate!");
    return Imm;
  }

  int64_t getConstantImm() const {
    assert(isConstantImm() && "Invalid type access as constant immediate!");
    return static_cast<const MCConstantExpr*>(Imm)->getValue();
  }

  StringRef getToken() const {
    assert(isToken() && "Invalid type access as token!");
    return Tok;
  }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case Immediate:
      OS << *getImm();
      break;
    case Register:
      OS << "<register x" << getReg() << ">";
      break;
    case Token:
      OS << "'" << getToken() << "'";
      break;
    case Memory:
      OS << "<Mem " << *Mem.Offset << '(' << Mem.Reg << ")>";
      break;
    }
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    assert(Expr && "Expr shouldn't be null!");
    if (auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  // Used by the TableGen Code
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExpr(Inst, getImm());
  }

  void addAddrRegImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Mem.Reg));
    addExpr (Inst, Mem.Offset);
  }

  void addAddrRegImm12sOperands(MCInst &Inst, unsigned N) const {
    addAddrRegImmOperands(Inst, N);
  }
  void addAddrSpImm6uWordOperands(MCInst &Inst, unsigned N) const {
    addAddrRegImmOperands(Inst, N);
  }
  void addAddrSpImm6uDoubleOperands(MCInst &Inst, unsigned N) const {
    addAddrRegImmOperands(Inst, N);
  }
  void addAddrRegImm5uWordOperands(MCInst &Inst, unsigned N) const {
    addAddrRegImmOperands(Inst, N);
  }
  void addAddrRegImm5uDoubleOperands(MCInst &Inst, unsigned N) const {
    addAddrRegImmOperands(Inst, N);
  }
};
} // end anonymous namespace.

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#define GET_SUBTARGET_FEATURE_NAME
#include "RISCVGenAsmMatcher.inc"

bool RISCVAsmParser::generateImmOutOfRangeError(
    OperandVector &Operands, uint64_t ErrorInfo, int Lower, int Upper,
    Twine Msg = "immediate must be an integer in the range") {
  SMLoc ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
  return Error(ErrorLoc, Msg + " [" + Twine(Lower) + ", " + Twine(Upper) + "]");
}

bool RISCVAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  MCInst Inst;

  switch (MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm)) {
  default:
    break;
  case Match_Success:
    Inst.setLoc(IDLoc);
    Out.EmitInstruction(Inst, getSTI());
    return false;
  case Match_MissingFeature:
    return Error(IDLoc, "instruction use requires an option to be enabled");
  case Match_MnemonicFail:
    return Error(IDLoc, "unrecognized instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0U) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");

      ErrorLoc = ((RISCVOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_InvalidUImm4:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 4) - 1);
  case Match_InvalidUImm5:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 5) - 1);
  case Match_InvalidSImm12:
    return generateImmOutOfRangeError(Operands, ErrorInfo, -(1 << 11),
                                      (1 << 11) - 1);
  case Match_InvalidUImm12:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 12) - 1);
  case Match_InvalidSImm13Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 12), (1 << 12) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  case Match_InvalidUImm20:
    return generateImmOutOfRangeError(Operands, ErrorInfo, 0, (1 << 20) - 1);
  case Match_InvalidSImm21Lsb0:
    return generateImmOutOfRangeError(
        Operands, ErrorInfo, -(1 << 20), (1 << 20) - 2,
        "immediate must be a multiple of 2 bytes in the range");
  }

  llvm_unreachable("Unknown match type detected!");
}

Optional<unsigned> RISCVAsmParser::matchCPURegisterName(StringRef Name,
                                                        bool Reg32Bit) const {
  if (getSTI().getFeatureBits()[RISCV::FeatureRV32] || Reg32Bit) {
      return StringSwitch<Optional<unsigned>>(Name)
               .Cases("zero", "x0" , Optional<unsigned>(RISCV::X0_32 ))
               .Cases("ra"  , "x1" , Optional<unsigned>(RISCV::X1_32 ))
               .Cases("sp"  , "x2" , Optional<unsigned>(RISCV::X2_32 ))
               .Cases("gp"  , "x3" , Optional<unsigned>(RISCV::X3_32 ))
               .Cases("tp"  , "x4" , Optional<unsigned>(RISCV::X4_32 ))
               .Cases("t0"  , "x5" , Optional<unsigned>(RISCV::X5_32 ))
               .Cases("t1"  , "x6" , Optional<unsigned>(RISCV::X6_32 ))
               .Cases("t2"  , "x7" , Optional<unsigned>(RISCV::X7_32 ))
               .Cases("s0"  , "x8" , Optional<unsigned>(RISCV::X8_32 ))
               .Cases("s1"  , "x9" , Optional<unsigned>(RISCV::X9_32 ))
               .Cases("a0"  , "x10", Optional<unsigned>(RISCV::X10_32))
               .Cases("a1"  , "x11", Optional<unsigned>(RISCV::X11_32))
               .Cases("a2"  , "x12", Optional<unsigned>(RISCV::X12_32))
               .Cases("a3"  , "x13", Optional<unsigned>(RISCV::X13_32))
               .Cases("a4"  , "x14", Optional<unsigned>(RISCV::X14_32))
               .Cases("a5"  , "x15", Optional<unsigned>(RISCV::X15_32))
               .Cases("a6"  , "x16", Optional<unsigned>(RISCV::X16_32))
               .Cases("a7"  , "x17", Optional<unsigned>(RISCV::X17_32))
               .Cases("s2"  , "x18", Optional<unsigned>(RISCV::X18_32))
               .Cases("s3"  , "x19", Optional<unsigned>(RISCV::X19_32))
               .Cases("s4"  , "x20", Optional<unsigned>(RISCV::X20_32))
               .Cases("s5"  , "x21", Optional<unsigned>(RISCV::X21_32))
               .Cases("s6"  , "x22", Optional<unsigned>(RISCV::X22_32))
               .Cases("s7"  , "x23", Optional<unsigned>(RISCV::X23_32))
               .Cases("s8"  , "x24", Optional<unsigned>(RISCV::X24_32))
               .Cases("s9"  , "x25", Optional<unsigned>(RISCV::X25_32))
               .Cases("s10" , "x26", Optional<unsigned>(RISCV::X26_32))
               .Cases("s11" , "x27", Optional<unsigned>(RISCV::X27_32))
               .Cases("t3"  , "x28", Optional<unsigned>(RISCV::X28_32))
               .Cases("t4"  , "x29", Optional<unsigned>(RISCV::X29_32))
               .Cases("t5"  , "x30", Optional<unsigned>(RISCV::X30_32))
               .Cases("t6"  , "x31", Optional<unsigned>(RISCV::X31_32))
               .Default(None);
  } else if (getSTI().getFeatureBits()[RISCV::FeatureRV64]) {
      return StringSwitch<Optional<unsigned>>(Name)
               .Cases("zero", "x0" , Optional<unsigned>(RISCV::X0_64 ))
               .Cases("ra"  , "x1" , Optional<unsigned>(RISCV::X1_64 ))
               .Cases("sp"  , "x2" , Optional<unsigned>(RISCV::X2_64 ))
               .Cases("gp"  , "x3" , Optional<unsigned>(RISCV::X3_64 ))
               .Cases("tp"  , "x4" , Optional<unsigned>(RISCV::X4_64 ))
               .Cases("t0"  , "x5" , Optional<unsigned>(RISCV::X5_64 ))
               .Cases("t1"  , "x6" , Optional<unsigned>(RISCV::X6_64 ))
               .Cases("t2"  , "x7" , Optional<unsigned>(RISCV::X7_64 ))
               .Cases("s0"  , "x8" , Optional<unsigned>(RISCV::X8_64 ))
               .Cases("s1"  , "x9" , Optional<unsigned>(RISCV::X9_64 ))
               .Cases("a0"  , "x10", Optional<unsigned>(RISCV::X10_64))
               .Cases("a1"  , "x11", Optional<unsigned>(RISCV::X11_64))
               .Cases("a2"  , "x12", Optional<unsigned>(RISCV::X12_64))
               .Cases("a3"  , "x13", Optional<unsigned>(RISCV::X13_64))
               .Cases("a4"  , "x14", Optional<unsigned>(RISCV::X14_64))
               .Cases("a5"  , "x15", Optional<unsigned>(RISCV::X15_64))
               .Cases("a6"  , "x16", Optional<unsigned>(RISCV::X16_64))
               .Cases("a7"  , "x17", Optional<unsigned>(RISCV::X17_64))
               .Cases("s2"  , "x18", Optional<unsigned>(RISCV::X18_64))
               .Cases("s3"  , "x19", Optional<unsigned>(RISCV::X19_64))
               .Cases("s4"  , "x20", Optional<unsigned>(RISCV::X20_64))
               .Cases("s5"  , "x21", Optional<unsigned>(RISCV::X21_64))
               .Cases("s6"  , "x22", Optional<unsigned>(RISCV::X22_64))
               .Cases("s7"  , "x23", Optional<unsigned>(RISCV::X23_64))
               .Cases("s8"  , "x24", Optional<unsigned>(RISCV::X24_64))
               .Cases("s9"  , "x25", Optional<unsigned>(RISCV::X25_64))
               .Cases("s10" , "x26", Optional<unsigned>(RISCV::X26_64))
               .Cases("s11" , "x27", Optional<unsigned>(RISCV::X27_64))
               .Cases("t3"  , "x28", Optional<unsigned>(RISCV::X28_64))
               .Cases("t4"  , "x29", Optional<unsigned>(RISCV::X29_64))
               .Cases("t5"  , "x30", Optional<unsigned>(RISCV::X30_64))
               .Cases("t6"  , "x31", Optional<unsigned>(RISCV::X31_64))
               .Default(None);
  } else {
      llvm_unreachable("Not RV32/64");
  }
}

bool RISCVAsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  const ParseResult<unsigned> Reg = parseRegister(false);
  if (Reg) {
    RegNo = Reg->Val;
    StartLoc = Reg->StartLoc;
    EndLoc = Reg->EndLoc;
    return false;
  } else {
    return true;
  }
}

ParseResult<void> RISCVAsmParser::parseLeftParen() {
  if (getLexer().is(AsmToken::LParen)) {
    const AsmToken& Tok = getLexer().Lex();
    return ParseSuccess<void> { Tok.getLoc(), Tok.getEndLoc() };
  } else {
    return ParseError { getLoc(), "expected '('" };
  }
}

ParseResult<void> RISCVAsmParser::parseRightParen() {
  if (getLexer().is(AsmToken::RParen)) {
    const AsmToken& Tok = getLexer().Lex();
    return ParseSuccess<void> { Tok.getLoc(), Tok.getEndLoc() };
  } else {
    return ParseError { getLoc(), "expected ')'" };
  }
}

ParseResult<StringRef> RISCVAsmParser::peekIdentifier() {
  if (getLexer().is(AsmToken::Identifier)) {
    const AsmToken& Tok = getLexer().getTok();
    return ParseSuccess<StringRef> { Tok.getIdentifier(), Tok.getLoc(), Tok.getEndLoc() };
  } else {
    return ParseError { getLoc(), "expected identifier" };
  }
}

ParseResult<StringRef> RISCVAsmParser::parseIdentifier() {
  const ParseResult<StringRef> Identifier = peekIdentifier();

  if (Identifier)
    getLexer().Lex(); // Eat identifier

  return Identifier;
}

ParseResult<unsigned> RISCVAsmParser::parseRegister(bool Reg32Bit) {
  const ParseResult<StringRef> Identifier = peekIdentifier();
  if (Identifier) {
    const Optional<unsigned> Reg = matchCPURegisterName(Identifier->Val, Reg32Bit);
    if (Reg) {
        getLexer().Lex();
        return ParseSuccess<unsigned> { *Reg, Identifier->StartLoc, Identifier->EndLoc };
    } else {
        return ParseError { getLoc(), "invalid register" };
    }
  } else {
    return ParseError { getLoc(), "expected register" };
  }
}

ParseResult<const MCExpr*> RISCVAsmParser::parseExpression() {
  const SMLoc StartLoc = getLoc();
  SMLoc EndLoc;
  const MCExpr* Expr;
  if (getParser().parseExpression(Expr, EndLoc))
    return ParseError { getLoc(), "invalid expression" };
  else
    return ParseSuccess<const MCExpr*> { Expr, StartLoc, EndLoc };
}

ParseResult<const MCExpr*> RISCVAsmParser::parseImmediate() {
  switch (getLexer().getKind()) {
    case AsmToken::LParen:
    case AsmToken::Minus:
    case AsmToken::Plus:
    case AsmToken::Integer:
    case AsmToken::String:
      return parseExpression();
    case AsmToken::Identifier: {
      const ParseResult<StringRef> Identifier = parseIdentifier();
      if (Identifier) {
        MCSymbol* Sym = getContext().getOrCreateSymbol(Identifier->Val);
        return ParseSuccess<const MCExpr*> { MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, getContext()),
                                             Identifier->StartLoc, Identifier->EndLoc };
      } else {
        return ParseError { getLoc(), "expected identifier" };
      }
    }
    case AsmToken::PercentPcrel_Hi:
    case AsmToken::PercentLo:
    case AsmToken::PercentHi: {
    const MCExpr *Expr;
    MCAsmParser &Parser = getParser();
    SMLoc S = Parser.getTok().getLoc(); // Start location of the operand.
    SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
    getParser().parseExpression(Expr);
    return ParseSuccess<const MCExpr*> {Expr, S, E};
    }
    default:
      return ParseError { getLoc(), "unknown operand" };
  }
}

/// Looks at a token type and creates the relevant operand
/// from this information, adding to Operands.
/// If operand was parsed, returns false, else true.
bool RISCVAsmParser::parseOperand(OperandVector &Operands, bool Reg32Bit) {
  const ParseResult<unsigned> Reg = parseRegister(Reg32Bit);
  if (Reg) {
    Operands.push_back(make_unique<RISCVOperand>(Reg->Val, Reg->StartLoc, Reg->EndLoc));
    return false;
  }

  const ParseResult<const MCExpr*> Expr = parseImmediate();
  if (const auto Err = Expr.getError())
    return Error(Err->ErrorLoc, Err->ErrorMsg);

  if (parseLeftParen()) { // offset(reg)
    const ParseResult<unsigned> Reg = parseRegister(Reg32Bit);
    if (!Reg)
      return Error(getLoc(), "expected register");

    const auto RParen = parseRightParen();
    if (const auto Err = RParen.getError())
      return Error(Err->ErrorLoc, Err->ErrorMsg);

    Operands.push_back(make_unique<RISCVOperand>(Expr->Val, Reg->Val, Expr->StartLoc, RParen->EndLoc));
    return false;
  } else {
    Operands.push_back(make_unique<RISCVOperand>(Expr->Val, Expr->StartLoc, Expr->EndLoc));
    return false;
  }
}

static bool use32BitReg(StringRef Name) {
  return StringSwitch<bool>(Name)
           .Case("addiw",   true)
           .Case("sllw",    true)
           .Case("srlw",    true)
           .Case("sraw",    true)
           .Case("slliw",   true)
           .Case("srliw",   true)
           .Case("sraiw",   true)
           .Case("subw",    true)
           .Case("addw",    true)
           .Case("mulw",    true)
           .Case("divw",    true)
           .Case("remw",    true)
           .Case("divuw",   true)
           .Case("remuw",   true)
           .Case("c.addw",  true)
           .Case("c.subw",  true)
           .Case("c.addiw", true)
           .Default(false);
}

bool RISCVAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  // First operand is token for instruction
  Operands.push_back(make_unique<RISCVOperand>(Name, NameLoc, NameLoc));

  // If there are no more operands, then finish
  if (getLexer().is(AsmToken::EndOfStatement) || getLexer().is(AsmToken::Exclaim)) {
    getParser().eatToEndOfStatement();
    return false;
  }

  bool Reg32Bit = use32BitReg(Name);

  // Parse first operand
  if (parseOperand(Operands, Reg32Bit))
    return true;

  // Parse until end of statement, consuming commas between operands
  while (getLexer().is(AsmToken::Comma)) {
    // Consume comma token
    getLexer().Lex();

    // Parse next operand
    if (parseOperand(Operands, Reg32Bit))
      return true;
  }

  if (getLexer().is(AsmToken::Exclaim)) {
    getParser().eatToEndOfStatement();
    return false;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool RISCVAsmParser::classifySymbolRef(const MCExpr *Expr,
                                       RISCVMCExpr::VariantKind &Kind,
                                       int64_t &Addend) {
  Kind = RISCVMCExpr::VK_RISCV_Invalid;
  Addend = 0;

  if (const RISCVMCExpr *RE = dyn_cast<RISCVMCExpr>(Expr)) {
    Kind = RE->getKind();
    Expr = RE->getSubExpr();
  }

  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr);
  if (CE) {
    return true;
  }

  const MCSymbolRefExpr *SE = dyn_cast<MCSymbolRefExpr>(Expr);
  if (SE) {
    // It's a simple symbol reference with no addend.
    return true;
  }

  const MCBinaryExpr *BE = dyn_cast<MCBinaryExpr>(Expr);
  if (!BE)
    return false;

  SE = dyn_cast<MCSymbolRefExpr>(BE->getLHS());
  if (!SE)
    return false;

  if (BE->getOpcode() != MCBinaryExpr::Add &&
      BE->getOpcode() != MCBinaryExpr::Sub)
    return false;

  // We are able to support the subtraction of two symbol references
  if (BE->getOpcode() == MCBinaryExpr::Sub &&
      isa<MCSymbolRefExpr>(BE->getLHS()) && isa<MCSymbolRefExpr>(BE->getRHS()))
    return true;

  // See if the addend is is a constant, otherwise there's more going
  // on here than we can deal with.
  auto AddendExpr = dyn_cast<MCConstantExpr>(BE->getRHS());
  if (!AddendExpr)
    return false;

  Addend = AddendExpr->getValue();
  if (BE->getOpcode() == MCBinaryExpr::Sub)
    Addend = -Addend;

  // It's some symbol reference + a constant addend
  return Kind != RISCVMCExpr::VK_RISCV_Invalid;
}

bool RISCVAsmParser::ParseDirective(AsmToken DirectiveID) { return true; }

extern "C" void LLVMInitializeRISCVAsmParser() {
  RegisterMCAsmParser<RISCVAsmParser> X(getTheRISCV32Target());
  RegisterMCAsmParser<RISCVAsmParser> Y(getTheRISCV64Target());
}
