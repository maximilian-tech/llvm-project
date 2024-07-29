

#include "llvm-c/Core.h"
#include "llvm/ADT/EnumeratedArray.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
// #include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/BLAKE3.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include "llvm/Transforms/IPO/Attributor.h"
#include "llvm/Transforms/IPO/InputGeneration.h"
#include "llvm/Transforms/IPO/InputGenerationImpl.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
// #include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;

/**
 *
 *
 *
 */

static cl::opt<std::string>
    ClEntryPoint("record-entry-point",
                 cl::desc("Entry point identification (via name or #)."),
                 cl::Hidden, cl::init("main"));

/**
 *
 * CUSTOM LOGIC BEGIN
 *
 */

namespace {

llvm::Type *VoidTy, *FloatTy, *DoubleTy, *X86_FP80Ty;
IntegerType *Int1Ty, *Int8Ty, *Int16Ty, *Int32Ty, *Int64Ty, *Int128Ty;
PointerType *PtrTy;

const std::string Prefix = "__record_"; // Define your prefix

std::string getTypeName(const Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::TypeID::PointerTyID:
    return "ptr";
  case Type::TypeID::IntegerTyID:
    return "i" + std::to_string(Ty->getIntegerBitWidth());
  case Type::TypeID::FloatTyID:
    return "float";
  case Type::TypeID::DoubleTyID:
    return "double";
  case Type::TypeID::X86_FP80TyID:
    return "x86_fp80";
  default:
    return "unknown";
  };
}

struct Callbacks {
  Callbacks(Module &M)
      : InputGenMemmove(M.getOrInsertFunction(Prefix + "memmove", PtrTy, PtrTy,
                                              PtrTy, Int64Ty)),
        InputGenMemcpy(M.getOrInsertFunction(Prefix + "memcpy", PtrTy, PtrTy,
                                             PtrTy, Int64Ty)),
        InputGenMemset(M.getOrInsertFunction(Prefix + "memset", PtrTy, PtrTy,
                                             Int8Ty, Int64Ty)),
        UseCallback(
            M.getOrInsertFunction(Prefix + "use", VoidTy, PtrTy, Int32Ty)),
        CmpPtrCallback(M.getOrInsertFunction(Prefix + "cmp_ptr", VoidTy, PtrTy,
                                             PtrTy, Int32Ty))
  // UnreachableCallback(M.getOrInsertFunction(Prefix + "unreachable",
  //                                           VoidTy, Int32Ty, PtrTy))
  {

    // Initialize the arrays of types
    std::vector<Type *> Types = {Int1Ty,   Int8Ty,    Int16Ty, Int32Ty,
                                 Int64Ty,  Int128Ty,  PtrTy,   FloatTy,
                                 DoubleTy, X86_FP80Ty};

    // For each type in the Types array, generate the function callees
    for (Type *Ty : Types) {
      std::string typeName = getTypeName(Ty);
      InputGenMemoryAccessCallback[Ty] = M.getOrInsertFunction(
          Prefix + "access_" + typeName, VoidTy, PtrTy, Int64Ty, Int32Ty, PtrTy,
          Int32Ty, PtrTy, Int32Ty);
      StubValueGenCallback[Ty] =
          M.getOrInsertFunction(Prefix + "get_" + typeName, Ty, PtrTy, Int32Ty);
      ArgGenCallback[Ty] =
          M.getOrInsertFunction(Prefix + "arg_" + typeName, Ty, PtrTy, Int32Ty);
    }
  }

  const FunctionCallee InputGenMemmove;
  const FunctionCallee InputGenMemcpy;
  const FunctionCallee InputGenMemset;
  const FunctionCallee UseCallback;
  const FunctionCallee CmpPtrCallback;
  // const FunctionCallee UnreachableCallback;

  std::map<const Type *, FunctionCallee> InputGenMemoryAccessCallback;
  std::map<const Type *, FunctionCallee> StubValueGenCallback;
  std::map<const Type *, FunctionCallee> ArgGenCallback;
};

const Callbacks *Callback = nullptr;

struct InterestingMemoryAccess {
  Instruction *I = nullptr;
  Value *Addr = nullptr;
  Type *AccessTy;
  Value *V = nullptr;
  Value *MaybeMask = nullptr;

  enum KindTy { READ, WRITE, READ_THEN_WRITE, Last = READ_THEN_WRITE } Kind;

  static std::string kindAsStr(KindTy K) {
    switch (K) {
    case WRITE:
      return "write";
    case READ:
      return "read";
    case READ_THEN_WRITE:
      return "read_write";
    default:
      assert(false && "Unknown kind");
    }
  }
};

std::optional<InterestingMemoryAccess>
isInterestingMemoryAccess(Instruction &I) {
  InterestingMemoryAccess Access;
  Access.I = &I;

  /** memset/memmove/memcp */
  if (isa<MemIntrinsic>(I)) {
    return Access;
  }

  /** Basic Memory Accesses */
  if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
    // if (IndirectionGlobalLoads.count(LI))
    //   return std::nullopt;
    Access.Kind = InterestingMemoryAccess::READ;
    Access.AccessTy = LI->getType(); //
    Access.Addr =
        LI->getPointerOperand(); // This is the LLVM IR Adress, e.g. '%0'
  } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
    Access.Kind = InterestingMemoryAccess::WRITE;
    Access.V = SI->getValueOperand(); //
    Access.AccessTy = SI->getValueOperand()->getType();
    Access.Addr = SI->getPointerOperand(); //
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(&I)) {
    Access.Kind = InterestingMemoryAccess::READ_THEN_WRITE;
    Access.V = RMW->getValOperand();
    Access.AccessTy = RMW->getValOperand()->getType();
    Access.Addr = RMW->getPointerOperand();
  } else if (AtomicCmpXchgInst *XCHG = dyn_cast<AtomicCmpXchgInst>(&I)) {
    Access.Kind = InterestingMemoryAccess::READ_THEN_WRITE;
    Access.V = XCHG->getCompareOperand();
    Access.AccessTy = XCHG->getCompareOperand()->getType();
    Access.Addr = XCHG->getPointerOperand();
  }

  if (auto *Intrinsic = dyn_cast<IntrinsicInst>(&I)) {

    auto Id = Intrinsic->getIntrinsicID();
    if ((Id == Intrinsic::masked_load || Id == Intrinsic::masked_store)) {
      unsigned OpOffset = 0;

      if (Id == Intrinsic::masked_store) {
        // Masked store has an initial operand for the value.
        OpOffset = 1;
        Access.AccessTy = Intrinsic->getArgOperand(0)->getType();
        Access.V = Intrinsic->getArgOperand(0);
        Access.Kind = InterestingMemoryAccess::WRITE;
      } else {
        Access.AccessTy = Intrinsic->getType();
        Access.Kind = InterestingMemoryAccess::READ;
      }

      auto *BasePtr = Intrinsic->getOperand(0 + OpOffset);
      Access.MaybeMask = Intrinsic->getOperand(2 + OpOffset);
      Access.Addr = BasePtr;
    }
  }
  if (!Access.Addr) {
    return std::nullopt;
  }

  // For now, we simply cast our way through. This might not be the best
  // solution though. We might want to strip AS completely from the module in
  // the future. Type *PtrTy =
  // cast<PointerType>(Access.Addr->getType()->getScalarType()); if
  // (PtrTy->getPointerAddressSpace() != 0)
  //  return std::nullopt;

  // Ignore swifterror addresses.
  // swifterror memory addresses are mem2reg promoted by instruction
  // selection. As such they cannot have regular uses like an instrumentation
  // function and it makes no sense to track them as memory.

  /* ToDo if the above should be applied to this*/
  if (Access.Addr->isSwiftError())
    return std::nullopt;

  { /** This block may be not needed for memory accesses?? */
    // Peel off GEPs and BitCasts.
    auto *Addr = Access.Addr->stripInBoundsOffsets();

    if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
      // Do not instrument PGO counter updates.
      if (GV->hasSection()) {
        StringRef SectionName = GV->getSection();
        // Check if the global is in the PGO counters section.
        auto OF =
            Triple((&I)->getModule()->getTargetTriple()).getObjectFormat();
        auto name =
            getInstrProfSectionName(IPSK_cnts, OF, /*AddSegmentInfo=*/false);
        if (SectionName.ends_with(name)) {
          return std::nullopt;
        }
      }

      // Do not instrument accesses to LLVM internal variables.
      if (GV->getName().starts_with("__llvm")) {
        return std::nullopt;
      }
    }
  }
  return Access;
}

// Instrument memset/memmove/memcpy
void instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);

  IRB.SetCurrentDebugLocation(MI->getDebugLoc());

  if (isa<MemTransferInst>(MI)) {
    auto Callee = isa<MemMoveInst>(MI) ? Callback->InputGenMemmove
                                       : Callback->InputGenMemcpy;
    auto *Tgt = IRB.CreateAddrSpaceCast(MI->getOperand(0), PtrTy);
    auto *Src = IRB.CreateAddrSpaceCast(MI->getOperand(1), PtrTy);

    IRB.CreateCall(Callee, {Tgt, Src,
                            IRB.CreateZExtOrTrunc(
                                MI->getOperand(2),
                                Callee.getFunctionType()->getParamType(2))});
  } else if (isa<MemSetInst>(MI)) {
    auto *Tgt = IRB.CreateAddrSpaceCast(MI->getOperand(0), PtrTy);

    IRB.CreateCall(
        Callback->InputGenMemset,
        {Tgt, MI->getOperand(1),
         IRB.CreateZExtOrTrunc(
             MI->getOperand(2),
             Callback->InputGenMemset.getFunctionType()->getParamType(2))});
  }
  MI->eraseFromParent();
}

static Value *igGetUnderlyingObject(Value *Addr) {
  SmallVector<const Value *, 4> Objects;
  getUnderlyingObjects(Addr, Objects, /*LI=*/nullptr,
                       /*MaxLookup=*/12);

  Value *Object = Objects.size() == 1
                      ? const_cast<Value *>(Objects[0])
                      : getUnderlyingObject(Addr, /*MaxLookup=*/12);
  return Object;
}

void emitMemoryAccessCallback(IRBuilderBase &IRB, Value *Addr, Value *V,
                              Type *AccessTy, int32_t AllocSize,
                              InterestingMemoryAccess::KindTy Kind,
                              Value *Object, Value *ValueToReplace) {

  if (auto *GV = dyn_cast<GlobalVariable>(Addr);
      GV && isLibCGlobal(GV->getName()))
    return;

  Value *Val = ConstantInt::getNullValue(Int64Ty);
  if (V) {
    // If the value cannot fit in an i64, we need to pass it by reference.
    if (AllocSize > 8) {
      AllocaInst *Alloca = IRB.CreateAlloca(AccessTy);
      BasicBlock &EntryBlock =
          IRB.GetInsertBlock()->getParent()->getEntryBlock();
      Alloca->moveBefore(EntryBlock, EntryBlock.getFirstNonPHIOrDbgOrAlloca());
      IRB.CreateStore(V, Alloca);
      Val = IRB.CreateBitOrPointerCast(Alloca, Int64Ty);
    } else if (AccessTy->isIntOrIntVectorTy()) {
      Val = IRB.CreateZExtOrTrunc(V, Int64Ty);
    } else if (V->getType()->canLosslesslyBitCastTo(
                   IntegerType::get(IRB.getContext(), AllocSize * 8))) {
      Val = IRB.CreateZExtOrTrunc(
          IRB.CreateBitOrPointerCast(
              V, IntegerType::get(IRB.getContext(), AllocSize * 8)),
          Int64Ty);
    }
  }

  auto *Ptr = IRB.CreateAddrSpaceCast(Addr, PtrTy);
  auto *Base = IRB.CreateAddrSpaceCast(Object, PtrTy);
  SmallVector<Value *, 7> Args = {Ptr, Val,
                                  ConstantInt::get(Int32Ty, AllocSize), Base,
                                  ConstantInt::get(Int32Ty, Kind)};
  auto Hints = getBranchHints(ValueToReplace, IRB);
  Args.insert(Args.end(), Hints.begin(), Hints.end());
  if (isa<PointerType>(AccessTy) && AccessTy->getPointerAddressSpace())
    AccessTy = AccessTy->getPointerTo();
  auto Fn = InputGenMemoryAccessCallback[AccessTy];
  if (!Fn.getCallee()) {
    LLVM_DEBUG(dbgs() << "No memory access callback for " << *AccessTy << "\n");
    // TODO this should be easy enough to support - just 'load' a bunch of
    // int8's
    IRB.CreateIntrinsic(VoidTy, Intrinsic::trap, {});
  } else {
    IRB.CreateCall(Fn, Args);
  }
}

void InputGenInstrumenter::instrumentMaskedLoadOrStore(
    const InterestingMemoryAccess &Access, const DataLayout &DL) {
  auto *CI = dyn_cast<CallInst>(Access.I);
  if (!CI)
    llvm_unreachable("Unexpected");
  auto *F = CI->getCalledFunction();
  if (!F)
    llvm_unreachable("Unexpected");
  if (!(F->getIntrinsicID() == Intrinsic::masked_load ||
        F->getIntrinsicID() == Intrinsic::masked_store))
    llvm_unreachable("Unexpected");

  Value *Object = igGetUnderlyingObject(Access.Addr);
  if (isa<AllocaInst>(Object))
    return;

  Value *Mask = nullptr;
  if (F->getIntrinsicID() == Intrinsic::masked_load)
    Mask = Access.I->getOperand(2);
  if (F->getIntrinsicID() == Intrinsic::masked_store)
    Mask = Access.I->getOperand(3);
  assert(Mask);

  auto *VT = cast<VectorType>(Access.AccessTy);
  auto *ElTy = VT->getElementType();
  auto *MaskTy = cast<VectorType>(Mask->getType());
  if (MaskTy->getElementCount().isScalable())
    llvm_unreachable("Scalable vectors unsupported.");

  SplitBlockAndInsertForEachLane(
      MaskTy->getElementCount(), IntegerType::getInt64Ty(VT->getContext()),
      Access.I, [&](IRBuilderBase &IRB, Value *Idx) {
        Value *Cond = IRB.CreateExtractElement(Mask, Idx);
        Instruction *NewTerm = SplitBlockAndInsertIfThen(
            Cond, IRB.GetInsertBlock()->getTerminator(), /*Unreachable=*/false);
        IRB.SetInsertPoint(NewTerm);
        Value *GEP = IRB.CreateGEP(Access.AccessTy, Access.Addr, {Idx});
        Value *V = nullptr;
        switch (Access.Kind) {
        case InterestingMemoryAccess::READ:
          assert(Access.V == nullptr);
          break;
        case InterestingMemoryAccess::WRITE:
          assert(Access.V != nullptr);
          V = IRB.CreateExtractElement(Access.V, Idx);
          break;
        case InterestingMemoryAccess::READ_THEN_WRITE:
          // Unimplemented, but we abort() in the runtime
          break;
        }
        int32_t AllocSize = DL.getTypeAllocSize(ElTy);
        emitMemoryAccessCallback(IRB, GEP, V, ElTy, AllocSize, Access.Kind,
                                 Object, nullptr);
      });
}

void instrumentAddress(const InterestingMemoryAccess &Access,
                       const DataLayout &DL) {
  IRBuilder<> IRB(Access.I);
  IRB.SetCurrentDebugLocation(Access.I->getDebugLoc());

  Value *Object = igGetUnderlyingObject(Access.Addr);
  if (isa_and_present<AllocaInst>(Object))
    return;
  // assert(!IndirectionGlobalLoads.count(Access.I));

  std::function<void(Type *, Value *, Value *, Value *)> HandleType;
  HandleType = [&](Type *TheType, Value *TheAddr, Value *TheValue,
                   Value *ValueToReplace) {
    if (auto *ST = dyn_cast<StructType>(TheType)) {
      for (unsigned It = 0; It < ST->getNumElements(); It++) {
        Type *ElTy = ST->getElementType(It);
        auto *GEP = IRB.CreateConstGEP2_32(TheType, TheAddr, 0, It);
        Value *V = nullptr;
        switch (Access.Kind) {
        case InterestingMemoryAccess::READ:
          assert(TheValue == nullptr);
          break;
        case InterestingMemoryAccess::WRITE:
          assert(TheValue != nullptr);
          V = IRB.CreateExtractValue(TheValue, {It});
          break;
        case InterestingMemoryAccess::READ_THEN_WRITE:
          // Unimplemented, but we abort() in the runtime
          break;
        }
        HandleType(ElTy, GEP, V, nullptr);
      }
    } else if (auto *AT = dyn_cast<ArrayType>(TheType)) {
      Type *ElTy = AT->getElementType();
      for (unsigned It = 0; It < AT->getNumElements(); It++) {
        auto *GEP = IRB.CreateConstGEP2_32(TheType, TheAddr, 0, It);
        Value *V = nullptr;
        switch (Access.Kind) {
        case InterestingMemoryAccess::READ:
          assert(TheValue == nullptr);
          break;
        case InterestingMemoryAccess::WRITE:
          assert(TheValue != nullptr);
          V = IRB.CreateExtractValue(TheValue, {It});
          break;
        case InterestingMemoryAccess::READ_THEN_WRITE:
          // Unimplemented, but we abort() in the runtime
          break;
        }
        HandleType(ElTy, GEP, V, nullptr);
      }
    } else if (auto *VT = dyn_cast<VectorType>(TheType)) {
      Type *ElTy = VT->getElementType();
      if (!VT->getElementCount().isScalable()) {
        auto Count = VT->getElementCount().getFixedValue();
        for (unsigned It = 0; It < Count; It++) {
          auto *GEP = IRB.CreateConstGEP2_64(TheType, TheAddr, 0, It);
          Value *V = nullptr;
          switch (Access.Kind) {
          case InterestingMemoryAccess::READ:
            assert(TheValue == nullptr);
            break;
          case InterestingMemoryAccess::WRITE:
            assert(TheValue != nullptr);
            V = IRB.CreateExtractElement(TheValue, IRB.getInt64(It));
            break;
          case InterestingMemoryAccess::READ_THEN_WRITE:
            // Unimplemented, but we abort() in the runtime
            break;
          }
          HandleType(ElTy, GEP, V, nullptr);
        }
      } else {
        llvm_unreachable("Scalable vectors unsupported.");
      }
    } else {
      int32_t AllocSize = DL.getTypeAllocSize(TheType);
      emitMemoryAccessCallback(IRB, TheAddr, TheValue, TheType, AllocSize,
                               Access.Kind, Object, ValueToReplace);
    }
  };
  Value *ValueToReplace = nullptr;
  switch (Access.Kind) {
  case InterestingMemoryAccess::READ:
  case InterestingMemoryAccess::READ_THEN_WRITE:
    ValueToReplace = Access.I;
    break;
  case InterestingMemoryAccess::WRITE:
    ValueToReplace = nullptr;
    break;
  }
  HandleType(Access.AccessTy, Access.Addr, Access.V, ValueToReplace);
}

void instrumentMop(const InterestingMemoryAccess &Access,
                   const DataLayout &DL) {

  if (Access.MaybeMask)
    instrumentMaskedLoadOrStore(Access, DL);
  else
    instrumentAddress(Access, DL);
}

void instrumentFunction(Function &F) {
  // LLVM_DEBUG(dbgs() << "INPUTGEN instrumenting:\n" << F.getName() << "\n");

  // Declare or get the logMemoryAccess function

  // FunctionType *logFuncType = FunctionType::get(Type::getVoidTy(Context),
  // {Type::getInt8PtrTy(Context)}, false); FunctionCallee logFunc =
  // M->getOrInsertFunction("logMemoryAccess", logFuncType);

  SmallVector<InterestingMemoryAccess, 16> ToInstrumentMem;
  SmallVector<ICmpInst *, 16> ToInstrumentCmp;
  // SmallVector<UnreachableInst *, 16> ToInstrumentUnreachable;

  // Fill the set of memory operations to instrument.
  for (auto &I : instructions(F)) {
    if (auto IMA = isInterestingMemoryAccess(I)) {
      ToInstrumentMem.push_back(*IMA);
    } else if (auto *Cmp = dyn_cast<ICmpInst>(&I)) {
      ToInstrumentCmp.push_back(Cmp);
      // } else if (auto *Unreachable = dyn_cast<UnreachableInst>(&I)) {
      //   ToInstrumentUnreachable.push_back(Unreachable);
      // }
    }

    if (ToInstrumentMem.empty() && ToInstrumentCmp.empty()) //&&
    {
      // ToInstrumentUnreachable.empty()) {
      LLVM_DEBUG(dbgs() << "INPUTGEN nothing to instrument in " << F.getName()
                        << "\n");
    }

    auto DL = F.getParent()->getDataLayout();

    // for (auto *Unreachable : ToInstrumentUnreachable) {
    //   instrumentUnreachable(Unreachable);
    // }
    // for (auto *Cmp : ToInstrumentCmp) {
    //   instrumentCmp(Cmp);
    // }
    for (auto &IMA : ToInstrumentMem) {
      if (isa<MemIntrinsic>(IMA.I)) {
        instrumentMemIntrinsic(cast<MemIntrinsic>(IMA.I));
      } else {
        instrumentMop(IMA, DL);
      }

      NumInstrumented++;
    }

    LLVM_DEBUG(dbgs() << "INPUTGEN done instrumenting: "
                      << ToInstrumentMem.size() << " instructions in "
                      << F.getName() << "\n");
  }
}
/**
 *
 * CUSTOM LOGIC END
 *
 */
} // namespace

namespace {
struct Recorder : public PassInfoMixin<Recorder> {
  explicit Recorder();
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};
} // namespace
// namespace

void instrumentModule(Module &M) {
  // Not nice, as this is global variable now
  static Callbacks callbacks(M);
  Callback = &callbacks;

  for (auto &Fn : M) {
    instrumentFunction(Fn);
  }
}

void createRecordingEntryPoint(Function &F) {
  Module &M = *F.getParent();

  IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
  IRB.SetCurrentDebugLocation(F.getEntryBlock().getTerminator()->getDebugLoc());

  FunctionCallee PushFn =
      M.getOrInsertFunction(Prefix + "push", FunctionType::get(VoidTy, false));

  IRB.CreateCall(PushFn, {});

  for (auto &Arg : F.args()) {
    FunctionCallee ArgPtrFn = M.getOrInsertFunction(
        Prefix + "arg_" + ::getTypeName(Arg.getType()),
        FunctionType::get(Arg.getType(), {Arg.getType()}, false));
    IRB.CreateCall(ArgPtrFn, {&Arg});
  }

  FunctionCallee PopFn =
      M.getOrInsertFunction(Prefix + "pop", FunctionType::get(VoidTy, false));
  for (auto &I : instructions(F)) {
    if (!isa<ReturnInst>(I)) {
      continue;
    }
    IRB.SetInsertPoint(&I);
    IRB.SetCurrentDebugLocation(I.getDebugLoc());
    IRB.CreateCall(PopFn, {});
  }
}

bool instrumentEntryPoint(Module &M, Function &EntryPoint, bool UniqName) {
  EntryPoint.setLinkage(GlobalValue::ExternalLinkage);
  createRecordingEntryPoint(EntryPoint);

  return true;
}

bool instrumentModuleForFunction(Module &M, Function &EntryPoint) {
  if (EntryPoint.isDeclaration()) {
    errs() << "Entry point is declaration, used \"" << EntryPoint.getName()
           << "\".\n";
    return false;
  }

  // IGI.pruneModule(EntryPoint);
  instrumentModule(M);
  instrumentEntryPoint(M, EntryPoint, /*UniqName=*/false);
  // instrumentFunctionPtrs(M);

  return true;
}

bool instrumentClEntryPoint(Module &M) {
  Function *EntryPoint = M.getFunction(ClEntryPoint);

  if (!EntryPoint) {
    int No;
    if (to_integer(ClEntryPoint, No)) {
      auto It = M.begin(), End = M.end();

      while (No-- > 0 && It != End) {
        It = std::next(It);
      }

      if (It != End) {
        EntryPoint = &*It;
      }
    }
  }

  if (!EntryPoint) {
    errs() << "No entry point found, used \"" << ClEntryPoint << "\".\n";
    return false;
  }

  return instrumentModuleForFunction(M, *EntryPoint);
}

void Recorder::insertLogCall(Instruction *I, Function *logFunc) {
  IRBuilder<> builder(I);
  Value *addr = nullptr;
  if (auto *loadInst = dyn_cast<LoadInst>(I)) {
    addr = loadInst->getPointerOperand();
  } else if (auto *storeInst = dyn_cast<StoreInst>(I)) {
    addr = storeInst->getPointerOperand();
  }
  if (addr) {
    // Insert call to logMemoryAccess before the memory access
    builder.CreateCall(logFunc, builder.CreateBitCast(
                                    addr, Type::getInt8PtrTy(I->getContext())));
  }
}

PreservedAnalyses Recorder::run(Module &M, ModuleAnalysisManager &MAM) {
  // LLVMContext &Context = M->getContext();
  instrumentClEntryPoint

      return PreservedAnalyses::all();
}

// Register the pass with -mllvm
llvm::PassPluginLibraryInfo getRecorderPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Recorder", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "my-pass") {
                    FPM.addPass(Recorder());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getRecorderPluginInfo();
}
