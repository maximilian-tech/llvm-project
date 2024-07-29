#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sys/resource.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "rt.hpp"

#include "../llvm/include/llvm/Transforms/IPO/InputGenerationTypes.h"

namespace {
int VERBOSE = 0;
int TIMING = 0;

INPUTGEN_TIMER_DEFINE(IGInitialization);
INPUTGEN_TIMER_DEFINE(IGGen);
INPUTGEN_TIMER_DEFINE(IGDump);
} // namespace

extern "C" {
extern VoidPtrTy __inputgen_function_pointers[];
extern uint32_t __inputgen_num_function_pointers;
}

using BranchHint = llvm::inputgen::BranchHint;

static constexpr intptr_t MinObjAllocation = 64;
static constexpr unsigned NullPtrProbability = 75;
static constexpr int CmpPtrRetryProbability = 10;
static constexpr int MaxDeviationFromBranchHint = 10;

template <typename T>
static void dumpBranchHints(BranchHint *BHs, int32_t BHSize) {
  for (int I = 0; I < BHSize; I++) {
    auto &BH = BHs[I];
    std::cerr << "BranchHint ";
#define DUMPBF(FIELD) std::cerr << #FIELD " " << BH.FIELD << " "
    DUMPBF(Kind);
    DUMPBF(Signed);
    DUMPBF(Frequency);
    DUMPBF(Dominator);
#undef DUMPBF
    if constexpr (!std::is_same<__int128, T>::value)
      std::cerr << "Val " << *reinterpret_cast<T *>(BH.Val) << std::endl;
  }
}

template <typename T> static T divFloor(T A, T B) {
  assert(B > 0);
  T Res = A / B;
  T Rem = A % B;
  if (Rem == 0)
    return Res;
  if (Rem < 0) {
    assert(A < 0);
    return Res - 1;
  }
  assert(A > 0);
  return Res;
}

template <typename T> static T divCeil(T A, T B) {
  assert(B > 0);
  T Res = A / B;
  T Rem = A % B;
  if (Rem == 0)
    return Res;
  if (Rem > 0) {
    assert(A > 0);
    return Res + 1;
  }
  assert(A < 0);
  return Res;
}

template <typename T> static T alignStart(T Ptr, intptr_t Alignment) {
  intptr_t IPtr = reinterpret_cast<intptr_t>(Ptr);
  return reinterpret_cast<T>(divFloor(IPtr, Alignment) * Alignment);
}

template <typename T> static T alignEnd(T Ptr, intptr_t Alignment) {
  intptr_t IPtr = reinterpret_cast<intptr_t>(Ptr);
  return reinterpret_cast<T>(divCeil(IPtr, Alignment) * Alignment);
}

static VoidPtrTy advance(VoidPtrTy Ptr, uint64_t Bytes) {
  return reinterpret_cast<uint8_t *>(Ptr) + Bytes;
}

struct ObjectTy {
  const ObjectAddressing &OA;
  ObjectTy(size_t Idx, const ObjectAddressing &OA, VoidPtrTy Output,
           bool KnownSizeObjBundle = false)
      : OA(OA), KnownSizeObjBundle(KnownSizeObjBundle), Idx(Idx) {
    this->Output.Memory = Output;
    this->Output.AllocationSize = OA.MaxObjectSize;
    this->Output.AllocationOffset = OA.getOffsetFromObjBasePtr(nullptr);

    if (KnownSizeObjBundle)
      CurrentStaticObjEnd = OA.getObjBasePtr();
  }
  ~ObjectTy() {}

  struct AlignedMemoryChunk {
    VoidPtrTy Ptr;
    intptr_t InputSize;
    intptr_t InputOffset;
    intptr_t OutputSize;
    intptr_t OutputOffset;
    intptr_t CmpSize;
    intptr_t CmpOffset;
  };

  bool KnownSizeObjBundle;
  VoidPtrTy CurrentStaticObjEnd;

  VoidPtrTy addKnownSizeObject(uintptr_t Size) {
    assert(KnownSizeObjBundle);
    // Make sure zero-sized objects have their own address
    if (Size == 0)
      Size = 1;
    if (Size + CurrentStaticObjEnd > OA.getLowestObjPtr() + OA.MaxObjectSize)
      return nullptr;
    VoidPtrTy ObjPtr = CurrentStaticObjEnd;
    CurrentStaticObjEnd = alignEnd(CurrentStaticObjEnd + Size, ObjAlignment);
    return ObjPtr;
  }

  struct KnownSizeObjInputMem {
    VoidPtrTy Start;
    uintptr_t Size;
  };
  KnownSizeObjInputMem getKnownSizeObjectInputMemory(VoidPtrTy LocalPtr,
                                                     uintptr_t Size) {
    assert(KnownSizeObjBundle);
    KnownSizeObjInputMem Mem;
    Mem.Start = std::min(
        LocalPtr + Size,
        std::max(LocalPtr, OA.getObjBasePtr() + InputLimits.LowestOffset));
    VoidPtrTy End = std::max(
        LocalPtr, std::min(LocalPtr + Size,
                           OA.getObjBasePtr() + InputLimits.HighestOffset));
    Mem.Size = End - Mem.Start;
    assert(Mem.Start <= End);
    return Mem;
  }

  void comparedAt(VoidPtrTy Ptr) {
    intptr_t Offset = OA.getOffsetFromObjBasePtr(Ptr);
    CmpLimits.update(Offset, 1);
  }

  AlignedMemoryChunk getAlignedInputMemory() {
    // If we compare the pointer at some offset we need to make sure the output
    // allocation will contain those locations, otherwise comparisons may differ
    // in input-gen and input-run as we would compare against an offset in a
    // different object
    if (!OutputLimits.isEmpty()) {
      if (!CmpLimits.isEmpty())
        OutputLimits.update(CmpLimits.LowestOffset, CmpLimits.getSize());
      // We no longer need the CmpLimits, reset it
      CmpLimits = Limits();
    }

    VoidPtrTy InputStart =
        InputLimits.LowestOffset + Input.Memory - Input.AllocationOffset;
    VoidPtrTy InputEnd =
        InputLimits.HighestOffset + Input.Memory - Input.AllocationOffset;
    intptr_t OutputStart = alignStart(OutputLimits.LowestOffset, ObjAlignment);
    intptr_t OutputEnd = alignEnd(OutputLimits.HighestOffset, ObjAlignment);
    return {InputStart,
            InputEnd - InputStart,
            InputLimits.LowestOffset,
            OutputEnd - OutputStart,
            OutputStart,
            CmpLimits.getSize(),
            CmpLimits.LowestOffset};
  }

  template <typename T>
  T read(VoidPtrTy Ptr, uint32_t Size, BranchHint *BHs, int32_t BHSize);

  template <typename T> void write(T Val, VoidPtrTy Ptr, uint32_t Size) {
    intptr_t Offset = OA.getOffsetFromObjBasePtr(Ptr);
    assert(Output.isAllocated(Offset, Size));
    Used.ensureAllocation(Offset, Size);
    markUsed(Offset, Size);
    OutputLimits.update(Offset, Size);
  }

  void setFunctionPtrIdx(VoidPtrTy Ptr, uint32_t Size, VoidPtrTy FPtr,
                         uint32_t FIdx) {
    intptr_t Offset = OA.getOffsetFromObjBasePtr(Ptr);
    storeGeneratedValue(FPtr, Offset, Size);
    FPtrs.insert({Offset, FIdx});
  }

  const size_t Idx;
  std::set<intptr_t> Ptrs;
  std::unordered_map<intptr_t, uint32_t> FPtrs;

private:
  struct Memory {
    VoidPtrTy Memory = nullptr;
    intptr_t AllocationSize = 0;
    intptr_t AllocationOffset = 0;
    bool isAllocated(intptr_t Offset, uint32_t Size) {
      intptr_t AllocatedMemoryStartOffset = AllocationOffset;
      intptr_t AllocatedMemoryEndOffset =
          AllocatedMemoryStartOffset + AllocationSize;
      return (AllocatedMemoryStartOffset <= Offset &&
              AllocatedMemoryEndOffset > Offset + Size);
    }

    /// Returns true if it was already allocated
    bool ensureAllocation(intptr_t Offset, uint32_t Size) {
      if (isAllocated(Offset, Size))
        return true;
      reallocateData(Offset, Size);
      return false;
    }

    template <typename T>
    void extendMemory(T *&OldMemory, intptr_t NewAllocationSize,
                      intptr_t NewAllocationOffset) {
      T *NewMemory = reinterpret_cast<T *>(calloc(NewAllocationSize, 1));
      memcpy(advance(NewMemory, AllocationOffset - NewAllocationOffset),
             OldMemory, AllocationSize);
      free(OldMemory);
      OldMemory = NewMemory;
    };

    /// Reallocates the data so as to make the memory at `Offset` with length
    /// `Size` available
    void reallocateData(intptr_t Offset, uint32_t Size) {
      assert(!isAllocated(Offset, Size));

      intptr_t AllocatedMemoryStartOffset = AllocationOffset;
      intptr_t AllocatedMemoryEndOffset =
          AllocatedMemoryStartOffset + AllocationSize;
      intptr_t NewAllocatedMemoryStartOffset = AllocatedMemoryStartOffset;
      intptr_t NewAllocatedMemoryEndOffset = AllocatedMemoryEndOffset;

      intptr_t AccessStartOffset = Offset;
      intptr_t AccessEndOffset = AccessStartOffset + Size;

      if (AccessStartOffset < AllocatedMemoryStartOffset) {
        // Extend the allocation in the negative direction
        NewAllocatedMemoryStartOffset = alignStart(
            std::min(2 * AccessStartOffset, -MinObjAllocation), ObjAlignment);
      }
      if (AccessEndOffset >= AllocatedMemoryEndOffset) {
        // Extend the allocation in the positive direction
        NewAllocatedMemoryEndOffset = alignEnd(
            std::max(2 * AccessEndOffset, MinObjAllocation), ObjAlignment);
      }

      intptr_t NewAllocationOffset = NewAllocatedMemoryStartOffset;
      intptr_t NewAllocationSize =
          NewAllocatedMemoryEndOffset - NewAllocatedMemoryStartOffset;

      INPUTGEN_DEBUG(
          printf("Reallocating data in Object for access at %ld with size %d "
                 "from offset "
                 "%ld, size %ld to offset %ld, size %ld.\n",
                 Offset, Size, AllocationOffset, AllocationSize,
                 NewAllocationOffset, NewAllocationSize));

      extendMemory(Memory, NewAllocationSize, NewAllocationOffset);

      AllocationSize = NewAllocationSize;
      AllocationOffset = NewAllocationOffset;
    }
  };
  Memory Output, Input, Used;

  struct Limits {
    bool Initialized = false;
    intptr_t LowestOffset = 0;
    intptr_t HighestOffset = 0;
    bool isEmpty() { return !Initialized; }
    intptr_t getSize() { return HighestOffset - LowestOffset; }
    void update(intptr_t Offset, uint32_t Size) {
      if (!Initialized) {
        Initialized = true;
        LowestOffset = Offset;
        HighestOffset = Offset + Size;
        return;
      }
      if (LowestOffset > Offset)
        LowestOffset = Offset;
      if (HighestOffset < Offset + Size)
        HighestOffset = Offset + Size;
    }
  };
  Limits InputLimits, OutputLimits, CmpLimits;

  bool allUsed(intptr_t Offset, uint32_t Size) {
    for (unsigned It = 0; It < Size; It++)
      if (!Used.isAllocated(Offset + It, 1) ||
          !Used.Memory[Offset + It - Used.AllocationOffset])
        return false;
    return true;
  }

  void markUsed(intptr_t Offset, uint32_t Size) {
    assert(Used.isAllocated(Offset, Size));

    for (unsigned It = 0; It < Size; It++)
      Used.Memory[Offset + It - Used.AllocationOffset] = 1;
  }

  template <typename T>
  void storeGeneratedValue(T Val, intptr_t Offset, uint32_t Size) {
    assert(Size == sizeof(Val));

    // Only assign the bytes that were uninitialized
    uint8_t Bytes[sizeof(Val)];
    memcpy(Bytes, &Val, sizeof(Val));
    for (unsigned It = 0; It < sizeof(Val); It++) {
      if (!allUsed(Offset + It, 1)) {
        VoidPtrTy OutputLoc =
            Output.Memory - Output.AllocationOffset + Offset + It;
        VoidPtrTy InputLoc =
            Input.Memory - Input.AllocationOffset + Offset + It;
        *OutputLoc = Bytes[It];
        *InputLoc = Bytes[It];
        markUsed(Offset + It, 1);
      }
    }

    InputLimits.update(Offset, Size);
    OutputLimits.update(Offset, Size);
  }
};

struct GenValTy {
  uint8_t Content[MaxPrimitiveTypeSize] = {0};
  static_assert(sizeof(Content) == MaxPrimitiveTypeSize);
  int32_t IsPtr;
};

template <typename T> static GenValTy toGenValTy(T A, int32_t IsPtr) {
  GenValTy U;
  static_assert(sizeof(T) <= sizeof(U.Content));
  memcpy(U.Content, &A, sizeof(A));
  U.IsPtr = IsPtr;
  return U;
}

struct RetryInfoTy {
  uint64_t RollbackLocation;
  RetryInfoTy(uint64_t RollbackLocation) : RollbackLocation(RollbackLocation) {}
  virtual void dump(std::ostream &Stream) const {
    Stream << "RL " << RollbackLocation << " ";
  }
  virtual ~RetryInfoTy() {};
};
struct ObjCmpOffsetTy : RetryInfoTy {
  size_t IdxOriginal, IdxOther;
  intptr_t Offset;
  ObjCmpOffsetTy(uint64_t RollbackLocation, size_t IdxOriginal, size_t IdxOther,
                 intptr_t Offset)
      : RetryInfoTy(RollbackLocation), IdxOriginal(IdxOriginal),
        IdxOther(IdxOther), Offset(Offset) {}
  void dump(std::ostream &Stream) const override {
    RetryInfoTy::dump(Stream);
    Stream << "ObjCmpOffset " << IdxOriginal << " " << IdxOther << " " << Offset
           << std::endl;
  }
};
struct ObjCmpNullTy : RetryInfoTy {
  size_t Idx;
  ObjCmpNullTy(uint64_t RollbackLocation, size_t Idx)
      : RetryInfoTy(RollbackLocation), Idx(Idx) {}
  void dump(std::ostream &Stream) const override {
    RetryInfoTy::dump(Stream);
    Stream << "ObjCmpNull " << Idx << std::endl;
  }
};

struct InputGenConfTy {
  bool EnablePtrCmpRetry;
  bool EnableBranchHints;
  InputGenConfTy() {
    EnablePtrCmpRetry = !getenv("INPUT_GEN_DISABLE_PTR_CMP_RETRY");
    EnableBranchHints = !getenv("INPUT_GEN_DISABLE_BRANCH_HINTS");
  }
};

struct InputGenRTTy {
  InputGenRTTy(const char *ExecPath, const char *OutputDir,
               const char *FuncIdent, VoidPtrTy StackPtr, int Seed,
               InputGenConfTy InputGenConf,
               std::vector<std::unique_ptr<RetryInfoTy>> &RetryInfos,
               std::function<void(std::unique_ptr<RetryInfoTy>)> *RetryCallback)
      : InputGenConf(InputGenConf), RetryCallback(RetryCallback),
        RetryInfos(RetryInfos), StackPtr(StackPtr), Seed(Seed),
        FuncIdent(FuncIdent), OutputDir(OutputDir), ExecPath(ExecPath) {
    Gen.seed(Seed);
    if (this->FuncIdent != "") {
      this->FuncIdent += ".";
    }

    const struct rlimit Rlimit = {RLIM_INFINITY, RLIM_INFINITY};
    int Err = setrlimit(RLIMIT_AS, &Rlimit);
    if (Err)
      INPUTGEN_DEBUG(printf("Could not set bigger limit on malloc: %s\n",
                            strerror(errno)));

    uintptr_t Size = (uintptr_t)16 /*G*/ * 1024 /*M*/ * 1024 /*K*/ * 1024;
    static_assert(sizeof(Size) >= 8);
    do {
      Size = Size / 2;
      OA.setSize(Size);
    } while (!OutputMem.allocate(Size, OA.MaxObjectSize));
    INPUTGEN_DEBUG(printf("Max obj size: 0x%lx, max obj num: %lu\n",
                          OA.MaxObjectSize, OA.MaxObjectNum));

    OutputObjIdxOffset = OA.globalPtrToObjIdx(OutputMem.AlignedMemory);
    DefaultIntDistrib = std::uniform_int_distribution<>(0, 32);
    DefaultFloatDistrib = std::uniform_real_distribution<>(0, 10);

    UnusedRetryInfo = RetryInfos.begin();

    INPUTGEN_DEBUG({
      std::cerr << "Got " << RetryInfos.size() << " retry infos." << std::endl;
      for (auto const &Info : RetryInfos)
        Info->dump(std::cerr);
    });
  }
  ~InputGenRTTy() {}

  InputGenConfTy InputGenConf;

  std::function<void(std::unique_ptr<RetryInfoTy>)> *RetryCallback;
  std::vector<std::unique_ptr<RetryInfoTy>> &RetryInfos;
  std::vector<std::unique_ptr<RetryInfoTy>>::iterator UnusedRetryInfo;

  VoidPtrTy StackPtr;
  intptr_t OutputObjIdxOffset;
  int32_t Seed, SeedStub;
  std::string FuncIdent;
  std::string OutputDir;
  std::filesystem::path ExecPath;
  std::mt19937 Gen;
  std::uniform_int_distribution<> Rand;
  std::uniform_real_distribution<> DefaultFloatDistrib;
  std::uniform_int_distribution<> DefaultIntDistrib;
  struct AlignedAllocation {
    VoidPtrTy Memory = nullptr;
    uintptr_t Size = 0;
    uintptr_t Alignment = 0;
    VoidPtrTy AlignedMemory = nullptr;
    uintptr_t AlignedSize = 0;
    bool allocate(uintptr_t S, uintptr_t A) {
      if (Memory)
        free(Memory);
      Size = S + A;
      Memory = (VoidPtrTy)malloc(Size);
      if (Memory) {
        Alignment = A;
        AlignedSize = S;
        AlignedMemory = alignEnd(Memory, A);
        INPUTGEN_DEBUG(printf("Allocated 0x%lx (0x%lx) bytes of 0x%lx-aligned "
                              "memory at start %p.\n",
                              AlignedSize, Size, Alignment,
                              (void *)AlignedMemory));
      } else {
        INPUTGEN_DEBUG(
            printf("Unable to allocate memory with size 0x%lx\n", Size));
      }
      return Memory;
    }
    ~AlignedAllocation() { free(Memory); }
  };
  AlignedAllocation OutputMem;
  ObjectAddressing OA;

  struct GlobalTy {
    VoidPtrTy Ptr;
    size_t ObjIdx;
    uintptr_t Size;
  };
  std::vector<GlobalTy> Globals;
  std::vector<intptr_t> FunctionPtrs;

  uint64_t NumNewValues = 0;

  std::vector<GenValTy> GenVals;
  uint32_t NumArgs = 0;

  // Storage for dynamic objects
  std::vector<std::unique_ptr<ObjectTy>> Objects;
  std::vector<size_t> GlobalBundleObjects;

  int rand() { return Rand(Gen); }

  struct NewObj {
    size_t Idx;
    VoidPtrTy Ptr;
  };
  static constexpr size_t NullPtrIdx = -1;
  static constexpr uint64_t UnknownSize = -1;
  NewObj getNewPtr(uint64_t Size) {
    size_t Idx = Objects.size();
    if (UnusedRetryInfo != RetryInfos.end()) {
      RetryInfoTy *ObjCmp = UnusedRetryInfo->get();
      if (ObjCmpOffsetTy *ObjCmpOffset =
              dynamic_cast<ObjCmpOffsetTy *>(ObjCmp)) {
        if (ObjCmpOffset->IdxOther == Idx) {
          // An offset of this object will be compared to ObjCmp.IdxOriginal at
          // offset ObjCmp.Offset. Make sure that comparison will succeed
          VoidPtrTy Ptr = OA.localPtrToGlobalPtr(ObjCmpOffset->IdxOriginal +
                                                     OutputObjIdxOffset,
                                                 OA.getObjBasePtr()) +
                          ObjCmpOffset->Offset;
          INPUTGEN_DEBUG(printf("Pointer to existing obj #%lu at %p\n",
                                ObjCmpOffset->IdxOriginal, (void *)Ptr));
          UnusedRetryInfo++;
          return {ObjCmpOffset->IdxOriginal, Ptr};
        }
      } else if (ObjCmpNullTy *ObjCmpNull =
                     dynamic_cast<ObjCmpNullTy *>(ObjCmp)) {
        if (ObjCmpNull->Idx == Idx) {
          INPUTGEN_DEBUG(printf("Pointer to null instead of object #%lu\n",
                                ObjCmpNull->Idx));
          UnusedRetryInfo++;
          return {NullPtrIdx, nullptr};
        }
      } else {
        INPUTGEN_DEBUG(std::cerr << "Invalid type" << std::endl);
        abort();
      }
    }
    Objects.push_back(std::make_unique<ObjectTy>(
        Idx, OA, OutputMem.AlignedMemory + Idx * OA.MaxObjectSize));
    VoidPtrTy OutputPtr =
        OA.localPtrToGlobalPtr(Idx + OutputObjIdxOffset, OA.getObjBasePtr());
    INPUTGEN_DEBUG(
        printf("New Obj #%lu at output ptr %p\n", Idx, (void *)OutputPtr));
    return {Idx, OutputPtr};
  }

  NewObj getNewGlobal(uint64_t Size) {
    assert(Size != UnknownSize);
    for (size_t GlobalBundleIdx : GlobalBundleObjects) {
      auto &Obj = Objects[GlobalBundleIdx];
      if (VoidPtrTy LocalPtr = Obj->addKnownSizeObject(Size))
        return {GlobalBundleIdx,
                OA.localPtrToGlobalPtr(GlobalBundleIdx + OutputObjIdxOffset,
                                       LocalPtr)};
    }
    size_t Idx = Objects.size();
    Objects.push_back(std::make_unique<ObjectTy>(
        Idx, OA, OutputMem.AlignedMemory + Idx * OA.MaxObjectSize,
        /*KnownSizeObjBundle=*/true));
    VoidPtrTy LocalPtr = Objects.back()->addKnownSizeObject(Size);
    GlobalBundleObjects.push_back(Idx);
    return {Idx, OA.localPtrToGlobalPtr(Idx + OutputObjIdxOffset, LocalPtr)};
  }

  size_t getObjIdx(VoidPtrTy GlobalPtr, bool AllowNull = false) {
    assert(AllowNull || GlobalPtr);
    if (GlobalPtr == nullptr)
      return NullPtrIdx;
    size_t Idx = OA.globalPtrToObjIdx(GlobalPtr) - OutputObjIdxOffset;
    return Idx;
  }

  // Returns nullptr if it is not an object managed by us - a stack pointer or
  // memory allocated by malloc
  ObjectTy *globalPtrToObj(VoidPtrTy GlobalPtr, bool AllowNull = false) {
    size_t Idx = getObjIdx(GlobalPtr, AllowNull);
    bool IsExistingObj = Idx >= 0 && Idx < Objects.size();
    [[maybe_unused]] bool IsOutsideObjMemory = Idx > OA.MaxObjectNum || Idx < 0;
    assert(IsExistingObj || IsOutsideObjMemory);
    if (IsExistingObj) {
      INPUTGEN_DEBUG(std::cerr << "Access: " << (void *)GlobalPtr << " Obj #"
                               << Idx << std::endl);
      return Objects[Idx].get();
    }
    INPUTGEN_DEBUG(std::cerr << "Access to memory not handled by us: "
                             << (void *)GlobalPtr << std::endl);
    return nullptr;
  }

  void cmpPtr(VoidPtrTy A, VoidPtrTy B, int32_t Predicate) {
    ObjectTy *ObjA = globalPtrToObj(A, /*AllowNull=*/true);
    if (ObjA)
      ObjA->comparedAt(OA.globalPtrToLocalPtr(A));
    ObjectTy *ObjB = globalPtrToObj(B, /*AllowNull=*/true);
    if (ObjB)
      ObjB->comparedAt(OA.globalPtrToLocalPtr(B));

    if (!InputGenConf.EnablePtrCmpRetry)
      return;

    // Do not move this down. We want to always consume a rand() here
    bool ShouldCallback = !(rand() % CmpPtrRetryProbability);

    if (A == nullptr && B == nullptr)
      return;

    if (!RetryCallback)
      return;

    size_t IdxA = getObjIdx(A, /*AllowNull=*/true);
    size_t IdxB = getObjIdx(B, /*AllowNull=*/true);
    INPUTGEN_DEBUG(std::cerr << "CmpPtr " << (void *)A << " (#" << IdxA << ") "
                             << (void *)B << " (#" << IdxB << ") "
                             << std::endl);

    bool IsGlobalA =
        std::find(GlobalBundleObjects.begin(), GlobalBundleObjects.end(),
                  IdxA) != GlobalBundleObjects.end();
    bool IsGlobalB =
        std::find(GlobalBundleObjects.begin(), GlobalBundleObjects.end(),
                  IdxB) != GlobalBundleObjects.end();
    if (IsGlobalA && IsGlobalB) {
      INPUTGEN_DEBUG(std::cerr << "Globals cannot alias, ignoring."
                               << std::endl);
      return;
    }
    if ((IsGlobalA && B == nullptr) || (IsGlobalB && A == nullptr)) {
      INPUTGEN_DEBUG(std::cerr << "Globals cannot be null, ignoring."
                               << std::endl);
      return;
    }
    if ((IdxA != NullPtrIdx && !ObjA) || (IdxB != NullPtrIdx && !ObjB)) {
      INPUTGEN_DEBUG(
          std::cerr
          << "Object is not managed by us, can't retry to make it better"
          << std::endl);
      return;
    }

    if (IdxA != IdxB && ShouldCallback) {
      if (IdxB == NullPtrIdx) {
        (*RetryCallback)(std::make_unique<ObjCmpNullTy>(IdxA, IdxA));
      } else if (IdxA == NullPtrIdx) {
        (*RetryCallback)(std::make_unique<ObjCmpNullTy>(IdxB, IdxB));
      } else {
        if (IdxA > IdxB)
          std::swap(IdxA, IdxB);
        INPUTGEN_DEBUG(std::cerr
                       << "Compared different objects, will retry input gen. "
                       << IdxA << " " << IdxB << std::endl);

        (*RetryCallback)(std::make_unique<ObjCmpOffsetTy>(
            IdxB, IdxA, IdxB,
            OA.globalPtrToLocalPtr(A) - OA.globalPtrToLocalPtr(B)));
      }
    }
  }

  template <typename T> T getNewArg(BranchHint *BHs, int32_t BHSize) {
    T V = getNewValue<T>(BHs, BHSize);
    GenVals.push_back(toGenValTy(V, std::is_pointer<T>::value));
    NumArgs++;
    return V;
  }

  template <typename T> T getNewStub(BranchHint *BHs, int32_t BHSize) {
    T V = getNewValue<T>(BHs, BHSize);
    GenVals.push_back(toGenValTy(V, std::is_pointer<T>::value));
    return V;
  }

  template <typename T> T getDefaultNewValue() {
    if constexpr (std::is_floating_point<T>::value) {
      return DefaultFloatDistrib(Gen);
    } else if constexpr (std::is_integral<T>::value) {
      return DefaultIntDistrib(Gen);
    } else if constexpr (std::is_same<T, __int128>::value) {
      return DefaultIntDistrib(Gen);
    } else {
      static_assert(false);
    }
  }

  enum EndKindTy { OPEN, CLOSED };
  template <typename T> struct Interval {
    std::optional<T> getExactValue() {
      if (BeginKind == CLOSED && EndKind == CLOSED && Begin == End)
        return Begin;
      return std::nullopt;
    }
    EndKindTy BeginKind, EndKind;
    T Begin, End;
    static std::optional<Interval<T>> intersect(Interval<T> A, Interval<T> B) {
      Interval<T> C;
      if (A.Begin < B.Begin) {
        C.Begin = B.Begin;
        C.BeginKind = B.BeginKind;
      } else if (A.Begin == B.Begin) {
        C.Begin = B.Begin;
        C.BeginKind = std::min(A.BeginKind, B.BeginKind);
      } else {
        C.Begin = A.Begin;
        C.BeginKind = A.BeginKind;
      }

      if (A.End > B.End) {
        C.End = B.End;
        C.EndKind = B.EndKind;
      } else if (A.End == B.End) {
        C.End = B.End;
        C.EndKind = std::max(A.EndKind, B.EndKind);
      } else {
        C.End = A.End;
        C.EndKind = A.EndKind;
      }

      if (C.Begin > C.End)
        return std::nullopt;
      if (C.Begin == C.End && std::min(C.BeginKind, C.EndKind) == OPEN)
        return std::nullopt;

      return C;
    }
  };

  template <typename T> struct Set {
    std::vector<Interval<T>> Intervals;
    Set(std::vector<Interval<T>> I) : Intervals(I) {}
    static Set<T> intersect(Set<T> A, Set<T> B) {
      // Dumbest algorithm ever but it works
      Set<T> C({});
      for (size_t I = 0; I < A.Intervals.size(); I++) {
        for (size_t J = 0; J < B.Intervals.size(); J++) {
          auto Intersection =
              Interval<T>::intersect(A.Intervals[I], B.Intervals[I]);
          if (Intersection.has_value())
            C.Intervals.push_back(*Intersection);
        }
      }
      return C;
    }
    static Set<T> all() {
      return Set<T>({{CLOSED, CLOSED, std::numeric_limits<T>::min(),
                      std::numeric_limits<T>::max()}});
    }
  };

  // TODO Not sure what happens here when these values are unsigned...
  // TODO For now we assume every BH is with the same signedness (unsigned or
  // signed) as it would be very annoying otherwise but we can handle that using
  // intervals
  template <typename T> Set<T> getSetForBH(BranchHint BH) {
    T Val = *reinterpret_cast<T *>(BH.Val);
    switch (BH.Kind) {
    default:
      assert(false && "Invalid branch hint kind found");
      [[fallthrough]];
    case BranchHint::EQ:
      return Set<T>({{CLOSED, CLOSED, Val, Val}});
    case BranchHint::NE:
      return Set<T>({{CLOSED, OPEN, std::numeric_limits<T>::min(), Val},
                     {OPEN, CLOSED, Val, std::numeric_limits<T>::max()}});
    case BranchHint::LT:
      return Set<T>({{CLOSED, OPEN, std::numeric_limits<T>::min(), Val}});
    case BranchHint::LE:
      return Set<T>({{CLOSED, CLOSED, std::numeric_limits<T>::min(), Val}});
    case BranchHint::GT:
      return Set<T>({{OPEN, CLOSED, Val, std::numeric_limits<T>::max()}});
    case BranchHint::GE:
      return Set<T>({{CLOSED, CLOSED, Val, std::numeric_limits<T>::max()}});
    }
  }

  template <typename T> T getNewValue(BranchHint *BHs, int32_t BHSize) {
    if constexpr (std::is_same<T, bool>::value) {
      return getNewValueImpl<char>(BHs, BHSize);
    } else {
      return getNewValueImpl<T>(BHs, BHSize);
    }
  }

  template <typename T> T getNewValueImpl(BranchHint *BHs, int32_t BHSize) {
    static_assert(!std::is_pointer<T>::value);
    NumNewValues++;
    INPUTGEN_DEBUG(dumpBranchHints<T>(BHs, BHSize));
    if (InputGenConf.EnableBranchHints && BHSize > 0 && BHs[0].Frequency == 0) {
      BranchHint BH = BHs[0];
      auto ValueSet = Set<T>::all();
      do {
        ValueSet = Set<T>::intersect(ValueSet, getSetForBH<T>(BH));
        if (BH.Dominator == -1)
          break;
        assert(BH.Dominator < BHSize && BH.Dominator >= 0);
        BH = BHs[BH.Dominator];
      } while (true);

      if (ValueSet.Intervals.empty()) {
        INPUTGEN_DEBUG(std::cerr << "Got contradicting combination of Branch "
                                    "Hints, will just use the first one");
        ValueSet = getSetForBH<T>(BHs[0]);
      }

      // Picking an interval at random doesnt seem that uniform but w/e
      Interval<T> Interval =
          ValueSet.Intervals[rand() % ValueSet.Intervals.size()];
      T Begin =
          Interval.BeginKind == OPEN ? Interval.Begin + 1 : Interval.Begin;
      T End = Interval.EndKind == OPEN ? Interval.End - 1 : Interval.End;

      // Cap the values so that we dont get something huge
      if constexpr (std::is_unsigned<T>::value) {
        if (End - Begin > MaxDeviationFromBranchHint)
          End = Begin + MaxDeviationFromBranchHint;
      } else {
        if (Begin > 0) {
          if (End - Begin > MaxDeviationFromBranchHint)
            End = Begin + MaxDeviationFromBranchHint;
        } else if (End < 0) {
          if (End - Begin > MaxDeviationFromBranchHint)
            Begin = End - MaxDeviationFromBranchHint;
        } else {
          if (End - Begin > MaxDeviationFromBranchHint) {
            if (End > MaxDeviationFromBranchHint)
              End = MaxDeviationFromBranchHint;
            if (Begin < -MaxDeviationFromBranchHint)
              Begin = -MaxDeviationFromBranchHint;
          }
        }
      }

      T GenVal;

      if (Interval.getExactValue().has_value())
        GenVal = *Interval.getExactValue();
      else if constexpr (std::is_floating_point<T>::value) {
        auto Distrib = std::uniform_real_distribution<T>(Begin, End);
        GenVal = Distrib(Gen);
      } else if constexpr (std::is_integral<T>::value) {
        auto Distrib = std::uniform_int_distribution<T>(
            Interval.BeginKind == OPEN ? Begin + 1 : Begin,
            Interval.EndKind == OPEN ? End - 1 : End);
        GenVal = Distrib(Gen);
      } else if constexpr (std::is_same<T, __int128>::value) {
        auto Distrib = std::uniform_int_distribution<long long>(
            Interval.BeginKind == OPEN ? Begin + 1 : Begin,
            Interval.EndKind == OPEN ? End - 1 : End);
        GenVal = Distrib(Gen);
      } else {
        static_assert(false);
      }
      if constexpr (!std::is_same<T, __int128>::value)
        INPUTGEN_DEBUG(std::cerr << "Used branch hints to generate val "
                                 << GenVal << std::endl);
      return GenVal;
    }

    return getDefaultNewValue<T>();
  }

  template <>
  VoidPtrTy getNewValue<VoidPtrTy>(BranchHint *BHs, int32_t BHSize) {
    NumNewValues++;
    // We let the ptr cmp retry handle null pointers if it is enabled
    if (InputGenConf.EnablePtrCmpRetry || (rand() % NullPtrProbability)) {
      auto Obj = getNewPtr(UnknownSize);
      INPUTGEN_DEBUG(printf("New ptr: Obj #%lu at output ptr %p\n", Obj.Idx,
                            (void *)Obj.Ptr));
      return Obj.Ptr;
    }
    INPUTGEN_DEBUG(printf("New Obj = nullptr\n"));
    return nullptr;
  }

  template <>
  FunctionPtrTy getNewValue<FunctionPtrTy>(BranchHint *BHs, int32_t BHSize) {
    NumNewValues++;
    return nullptr;
  }

  template <typename T> void write(VoidPtrTy Ptr, T Val, uint32_t Size) {
    assert(Ptr);
    ObjectTy *Obj = globalPtrToObj(Ptr);
    if (Obj)
      Obj->write<T>(Val, OA.globalPtrToLocalPtr(Ptr), Size);
  }

  template <typename T>
  T read(VoidPtrTy Ptr, VoidPtrTy Base, uint32_t Size, BranchHint *BHs,
         int32_t BHSize) {
    assert(Ptr);
    ObjectTy *Obj = globalPtrToObj(Ptr);
    if (Obj)
      return Obj->read<T>(OA.globalPtrToLocalPtr(Ptr), Size, BHs, BHSize);
    return *reinterpret_cast<T *>(Ptr);
  }

  void registerGlobal(VoidPtrTy, VoidPtrTy *ReplGlobal, int32_t GlobalSize) {
    auto Global = getNewGlobal(GlobalSize);
    Globals.push_back({Global.Ptr, Global.Idx, (uintptr_t)GlobalSize});
    *ReplGlobal = Global.Ptr;
    INPUTGEN_DEBUG(printf("Global %p replaced with Obj %zu @ %p\n",
                          (void *)ReplGlobal, Global.Idx, (void *)Global.Ptr));
  }

  void registerFunctionPtrAccess(VoidPtrTy Ptr, uint32_t Size,
                                 VoidPtrTy *PotentialFPs, uint64_t N) {
    assert(Ptr);
    ObjectTy *Obj = globalPtrToObj(Ptr);
    assert(Obj && "FP Object should just have been created.");
    VoidPtrTy FP = PotentialFPs[rand() % N];
    *reinterpret_cast<VoidPtrTy *>(Ptr) = FP;

    VoidPtrTy *GlobalIt = std::find(
        __inputgen_function_pointers,
        __inputgen_function_pointers + __inputgen_num_function_pointers, FP);
    assert(GlobalIt != __inputgen_function_pointers +
                           __inputgen_num_function_pointers &&
           "Function not found in list!");

    Obj->setFunctionPtrIdx(OA.globalPtrToLocalPtr(Ptr), Size, FP,
                           GlobalIt - __inputgen_function_pointers);
  }

  intptr_t registerFunctionPtrIdx(size_t N) {
    auto Offset = rand() % N;
    FunctionPtrs.push_back(Offset);
    return Offset;
  }

  void report() {
    if (OutputDir == "-") {
      // TODO cross platform
      std::ofstream Null("/dev/null");
      report(Null);
    } else {
      auto FileName = ExecPath.filename().string();
      std::string ReportOutName(OutputDir + "/" + FileName + ".report." +
                                FuncIdent + std::to_string(Seed) + ".txt");
      std::string InputOutName(OutputDir + "/" + FileName + ".input." +
                               FuncIdent + std::to_string(Seed) + ".bin");
      std::ofstream InputOutStream(InputOutName,
                                   std::ios::out | std::ios::binary);
      report(InputOutStream);
    }
  }

  void report(std::ofstream &InputOut) {
    INPUTGEN_DEBUG({
      printf("Args (%u total)\n", NumArgs);
      for (size_t I = 0; I < NumArgs; ++I)
        printf("Arg %zu: %p\n", I, (void *)GenVals[I].Content);
      printf("Num new values: %lu\n", NumNewValues);
      printf("Objects (%zu total)\n", Objects.size());
    });

    writeV<uintptr_t>(InputOut, OA.Size);
    writeV<uintptr_t>(InputOut, OutputObjIdxOffset);
    writeV<uint32_t>(InputOut, SeedStub);

    auto BeforeTotalSize = InputOut.tellp();
    uint64_t TotalSize = 0;
    writeV(InputOut, TotalSize);

    uint32_t NumObjects = Objects.size();
    writeV(InputOut, NumObjects);
    INPUTGEN_DEBUG(printf("Num Obj %u\n", NumObjects));

    std::vector<ObjectTy::AlignedMemoryChunk> MemoryChunks;
    uintptr_t I = 0;
    for (auto &Obj : Objects) {
      auto MemoryChunk = Obj->getAlignedInputMemory();
      INPUTGEN_DEBUG(printf(
          "Obj #%zu aligned memory chunk at %p, input size %lu "
          "offset %ld, output size %lu offset %ld, cmp size %lu offset %ld\n",
          Obj->Idx, (void *)MemoryChunk.Ptr, MemoryChunk.InputSize,
          MemoryChunk.InputOffset, MemoryChunk.OutputSize,
          MemoryChunk.OutputOffset, MemoryChunk.CmpSize,
          MemoryChunk.CmpOffset));
      writeV<intptr_t>(InputOut, I);
      writeV<intptr_t>(InputOut, MemoryChunk.InputSize);
      writeV<intptr_t>(InputOut, MemoryChunk.InputOffset);
      writeV<intptr_t>(InputOut, MemoryChunk.OutputSize);
      writeV<intptr_t>(InputOut, MemoryChunk.OutputOffset);
      writeV<intptr_t>(InputOut, MemoryChunk.CmpSize);
      writeV<intptr_t>(InputOut, MemoryChunk.CmpOffset);
      InputOut.write(reinterpret_cast<char *>(MemoryChunk.Ptr),
                     MemoryChunk.InputSize);
      TotalSize += MemoryChunk.OutputSize;
      MemoryChunks.push_back(MemoryChunk);

      assert(Obj->Idx == I);
      I++;
    }

    INPUTGEN_DEBUG(printf("TotalSize %lu\n", TotalSize));
    auto BeforeNumGlobals = InputOut.tellp();
    InputOut.seekp(BeforeTotalSize);
    writeV(InputOut, TotalSize);
    InputOut.seekp(BeforeNumGlobals);

    uint32_t NumGlobals = Globals.size();
    writeV(InputOut, NumGlobals);
    INPUTGEN_DEBUG(printf("Num Glob %u\n", NumGlobals));

    for (uint32_t I = 0; I < NumGlobals; ++I) {
      auto InputMem = Objects[Globals[I].ObjIdx]->getKnownSizeObjectInputMemory(
          OA.globalPtrToLocalPtr(Globals[I].Ptr), Globals[I].Size);
      VoidPtrTy InputStart = OA.localPtrToGlobalPtr(
          Globals[I].ObjIdx + OutputObjIdxOffset, InputMem.Start);
      writeV<VoidPtrTy>(InputOut, Globals[I].Ptr);
      writeV<VoidPtrTy>(InputOut, InputStart);
      writeV<uintptr_t>(InputOut, InputMem.Size);
      INPUTGEN_DEBUG(printf("Glob %u %p in Obj #%zu input start %p size %zu\n",
                            I, (void *)Globals[I].Ptr, Globals[I].ObjIdx,
                            (void *)InputStart, InputMem.Size));
    }

    I = 0;
    for (auto &Obj : Objects) {
      writeV<intptr_t>(InputOut, Obj->Idx);
      writeV<uintptr_t>(InputOut, Obj->Ptrs.size());
      INPUTGEN_DEBUG(printf("O #%ld NP %ld\n", Obj->Idx, Obj->Ptrs.size()));
      for (auto Ptr : Obj->Ptrs) {
        writeV<intptr_t>(InputOut, Ptr);
        INPUTGEN_DEBUG(printf("P at %ld : %p\n", Ptr,
                              *reinterpret_cast<void **>(
                                  MemoryChunks[Obj->Idx].Ptr +
                                  MemoryChunks[Obj->Idx].InputOffset + Ptr)));
      }

      writeV<uintptr_t>(InputOut, Obj->FPtrs.size());
      INPUTGEN_DEBUG(printf("O #%ld NFP %ld\n", Obj->Idx, Obj->FPtrs.size()));
      for (auto Ptr : Obj->FPtrs) {
        writeV<intptr_t>(InputOut, Ptr.first);
        writeV<uint32_t>(InputOut, Ptr.second);
        INPUTGEN_DEBUG(printf("FP at %ld : %u\n", Ptr.first, Ptr.second));
      }

      assert(Obj->Idx == I);
      I++;
    }

    uint32_t NumGenVals = GenVals.size();
    INPUTGEN_DEBUG(printf("Num GenVals %u\n", NumGenVals));
    INPUTGEN_DEBUG(printf("Num Args %u\n", NumArgs));
    writeV<uint32_t>(InputOut, NumGenVals);
    writeV<uint32_t>(InputOut, NumArgs);
    I = 0;
    for (auto &GenVal : GenVals) {
      INPUTGEN_DEBUG(printf("GenVal #%ld isPtr %d\n", I, GenVal.IsPtr));
      INPUTGEN_DEBUG(printf("Content "));
      for (unsigned J = 0; J < sizeof(GenVal.Content); J++)
        INPUTGEN_DEBUG(printf("%d ", (int)GenVal.Content[J]));
      INPUTGEN_DEBUG(printf("\n"));
      static_assert(sizeof(GenVal.Content) == MaxPrimitiveTypeSize);
      InputOut.write(ccast(GenVal.Content), MaxPrimitiveTypeSize);
      writeV<int32_t>(InputOut, GenVal.IsPtr);
    }

    uint32_t NumGenFunctionPtrs = FunctionPtrs.size();
    writeV<uint32_t>(InputOut, NumGenFunctionPtrs);
    for (intptr_t FPOffset : FunctionPtrs) {
      writeV<intptr_t>(InputOut, FPOffset);
    }
  }
};

void *DynLibHandle = nullptr;
static InputGenRTTy *InputGenRT;
static InputGenRTTy &getInputGenRT() { return *InputGenRT; }

template <typename T>
T ObjectTy::read(VoidPtrTy Ptr, uint32_t Size, BranchHint *BHs,
                 int32_t BHSize)
  {

  intptr_t Offset = OA.getOffsetFromObjBasePtr(Ptr);

  assert(Output.isAllocated(Offset, Size));

  Used.ensureAllocation(Offset, Size);
  Input.ensureAllocation(Offset, Size);

  T *OutputLoc = reinterpret_cast<T *>(
      advance(Output.Memory, -Output.AllocationOffset + Offset));

  if (allUsed(Offset, Size))
    return *OutputLoc;

  if constexpr (std::is_same<T, FunctionPtrTy>::value)
    return nullptr;

  T Val = getInputGenRT().getNewValue<T>(BHs, BHSize);
  storeGeneratedValue(Val, Offset, Size);

  if constexpr (std::is_pointer<T>::value)
    Ptrs.insert(Offset);

  return *OutputLoc;
}

extern "C" {
void __inputgen_version_mismatch_check_v1() {}

void __inputgen_init() {
  // getInputGenRT().init();
}
void __inputgen_deinit() {
  // getInputGenRT().init();
}

void __inputgen_global(int32_t NumGlobals, VoidPtrTy Global,
                       VoidPtrTy *ReplGlobal, int32_t GlobalSize) {
  getInputGenRT().registerGlobal(Global, ReplGlobal, GlobalSize);
}

VoidPtrTy __inputgen_select_fp(VoidPtrTy *PotentialFPs, uint64_t N) {
  return PotentialFPs[getInputGenRT().registerFunctionPtrIdx(N)];
}

void __inputgen_access_fp(VoidPtrTy Ptr, int32_t Size, VoidPtrTy Base,
                          VoidPtrTy *PotentialFPs, uint64_t N) {
  if (!getInputGenRT().read<FunctionPtrTy>(Ptr, Base, Size, /* BHs */ nullptr,
                                           0)) {
    getInputGenRT().registerFunctionPtrAccess(Ptr, Size, PotentialFPs, N);
    return;
  }
  // return an error if the function ptr is not in the potential callee list.
  if (std::find(PotentialFPs, PotentialFPs + N,
                *reinterpret_cast<VoidPtrTy *>(Ptr)) == PotentialFPs + N) {
    std::cerr << "Loaded Value is not a valid function pointer." << std::endl;
    exit(13);
  }
}

// TODO: need to support overlapping Tgt and Src here
VoidPtrTy __inputgen_memmove(VoidPtrTy Tgt, VoidPtrTy Src, uint64_t N) {
  VoidPtrTy SrcIt = Src;
  VoidPtrTy TgtIt = Tgt;
  for (uintptr_t I = 0; I < N; ++I, ++SrcIt, ++TgtIt) {
    auto V = getInputGenRT().read<char>(SrcIt, Src, sizeof(char), nullptr, 0);
    getInputGenRT().write<char>(TgtIt, V, sizeof(char));
  }
  return TgtIt;
}
VoidPtrTy __inputgen_memcpy(VoidPtrTy Tgt, VoidPtrTy Src, uint64_t N) {
  return __inputgen_memmove(Tgt, Src, N);
}

VoidPtrTy __inputgen_memset(VoidPtrTy Tgt, char C, uint64_t N) {
  VoidPtrTy TgtIt = Tgt;
  for (uintptr_t I = 0; I < N; ++I, ++TgtIt) {
    getInputGenRT().write<char>(TgtIt, C, sizeof(char));
  }
  return TgtIt;
}

#define RW(TY, NAME)                                                           \
  TY __inputgen_get_##NAME(BranchHint *BHs, int32_t BHSize) {                  \
    return getInputGenRT().getNewStub<TY>(BHs, BHSize);                        \
  }                                                                            \
  void __inputgen_access_##NAME(VoidPtrTy Ptr, int64_t Val, int32_t Size,      \
                                VoidPtrTy Base, int32_t Kind, BranchHint *BHs, \
                                int32_t BHSize) {                              \
    switch (Kind) {                                                            \
    case 0:                                                                    \
      getInputGenRT().read<TY>(Ptr, Base, Size, BHs, BHSize);                  \
      return;                                                                  \
    case 1:                                                                    \
      TY TyVal;                                                                \
      /* We need to reinterpret_cast fp types because they are just bitcast    \
         to the int64_t type in LLVM. */                                       \
      if constexpr (std::is_same<TY, float>::value) {                          \
        int32_t Trunc = (int32_t)Val;                                          \
        TyVal = *reinterpret_cast<TY *>(&Trunc);                               \
      } else if constexpr (std::is_same<TY, double>::value) {                  \
        TyVal = *reinterpret_cast<TY *>(&Val);                                 \
      } else {                                                                 \
        TyVal = (TY)Val;                                                       \
      }                                                                        \
      getInputGenRT().write<TY>(Ptr, TyVal, Size);                             \
      return;                                                                  \
    default:                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

#define RWREF(TY, NAME)                                                        \
  TY __inputgen_get_##NAME(BranchHint *BHs, int32_t BHSize) {                  \
    return getInputGenRT().getNewStub<TY>(BHs, BHSize);                        \
  }                                                                            \
  void __inputgen_access_##NAME(VoidPtrTy Ptr, int64_t Val, int32_t Size,      \
                                VoidPtrTy Base, int32_t Kind, BranchHint *BHs, \
                                int32_t BHSize) {                              \
    static_assert(sizeof(TY) > 8);                                             \
    TY TyVal;                                                                  \
    switch (Kind) {                                                            \
    case 0:                                                                    \
      getInputGenRT().read<TY>(Ptr, Base, Size, BHs, BHSize);                  \
      return;                                                                  \
    case 1:                                                                    \
      TyVal = *(TY *)Val;                                                      \
      getInputGenRT().write<TY>(Ptr, TyVal, Size);                             \
      return;                                                                  \
    default:                                                                   \
      abort();                                                                 \
    }                                                                          \
  }

RW(bool, i1)
RW(char, i8)
RW(short, i16)
RW(int32_t, i32)
RW(int64_t, i64)
RW(float, float)
RW(double, double)
RW(VoidPtrTy, ptr)
RWREF(__int128, i128)
RWREF(long double, x86_fp80)
#undef RW

#define ARG(TY, NAME)                                                          \
  TY __inputgen_arg_##NAME(BranchHint *BHs, int32_t BHSize) {                  \
    return getInputGenRT().getNewArg<TY>(BHs, BHSize);                         \
  }

ARG(bool, i1)
ARG(char, i8)
ARG(short, i16)
ARG(int32_t, i32)
ARG(int64_t, i64)
ARG(float, float)
ARG(double, double)
ARG(VoidPtrTy, ptr)
ARG(__int128, i128)
ARG(long double, x86_fp80)
#undef ARG

void __inputgen_use(VoidPtrTy Ptr, uint32_t Size) { useValue(Ptr, Size); }

void __inputgen_cmp_ptr(VoidPtrTy A, VoidPtrTy B, int32_t Predicate) {
  getInputGenRT().cmpPtr(A, B, Predicate);
}
void __inputgen_unreachable(int32_t No, const char *Name) {
  printf("Reached unreachable %i due to '%s'\n", No, Name ? Name : "n/a");
  exit(UnreachableExitStatus);
}
void __inputgen_override_free(void *P) {}
}

std::vector<std::unique_ptr<RetryInfoTy>> RetryInfos;
static void addNewRetryInfo(std::unique_ptr<RetryInfoTy> &&NewRetryInfo) {
  auto FirstToInvalidate = std::find_if(
      RetryInfos.begin(), RetryInfos.end(),
      [&](std::unique_ptr<RetryInfoTy> &ObjCmp) {
        return ObjCmp->RollbackLocation > NewRetryInfo->RollbackLocation;
      });
  RetryInfos.erase(FirstToInvalidate, RetryInfos.end());
  RetryInfos.emplace_back(std::move(NewRetryInfo));
}

int main(int argc, char **argv) {
  VERBOSE = (bool)getenv("VERBOSE");
  TIMING = (bool)getenv("TIMING");

  uint8_t Tmp;
  VoidPtrTy StackPtr = &Tmp;
  INPUTGEN_DEBUG(std::cerr << "Stack pointer: " << (void *)StackPtr
                           << std::endl);

  if (argc != 7 && argc != 4) {
    std::cerr << "Wrong usage." << std::endl;
    return 1;
  }

  const char *OutputDir = argv[1];
  int Start = std::stoi(argv[2]);
  int End = std::stoi(argv[3]);
  std::string FuncName = ("__inputgen_entry");
  std::string FuncIdent = "";
  if (argc == 7) {
    std::string Type = argv[4];
    FuncName += "___inputgen_renamed_";
    if (Type == "--name") {
      FuncIdent += argv[6];
      FuncName += argv[5];
    } else if (Type == "--file") {
      FuncIdent += argv[6];
      FuncName += getFunctionNameFromFile(argv[5], FuncIdent);
    } else {
      std::cerr << "Invalid arg type, must be --name or --file" << std::endl;
      abort();
    }
  }

  int Size = End - Start;
  if (Size <= 0)
    return 1;

  std::cout << "Will generate " << Size << " inputs for function " << FuncName
            << " " << FuncIdent << std::endl;

  INPUTGEN_TIMER_START(IGInitialization);

  DynLibHandle = dlopen(NULL, RTLD_NOW);
  if (!DynLibHandle) {
    std::cout << "Could not dyn load binary" << std::endl;
    std::cout << dlerror() << std::endl;
    return 11;
  }
  typedef void (*EntryFnType)(int, char **);
  EntryFnType EntryFn = (EntryFnType)dlsym(DynLibHandle, FuncName.c_str());

  if (!EntryFn) {
    std::cout << "Function " << FuncName << " not found in binary."
              << std::endl;
    return 12;
  }

  int I = Start;
  if (Start + 1 != End)
    return 1;

  InputGenConfTy InputGenConf;

  std::function<void()> RunInputGen;
  std::function<void(std::unique_ptr<RetryInfoTy>)> CmpInfoCallback;

  std::atexit([]() {
    INPUTGEN_TIMER_END(IGGen);

    if (InputGenRT) {
      INPUTGEN_TIMER_START(IGDump);
      InputGenRT->report();
      INPUTGEN_TIMER_END(IGDump);
      delete InputGenRT;
      InputGenRT = nullptr;
    }
    if (DynLibHandle)
      dlclose(DynLibHandle);
  });

  RunInputGen = [&]() {
    InputGenRT =
        new InputGenRTTy(argv[0], OutputDir, FuncIdent.c_str(), StackPtr, I,
                         InputGenConf, RetryInfos, &CmpInfoCallback);
    INPUTGEN_TIMER_END(IGInitialization);
    INPUTGEN_TIMER_START(IGGen);
    EntryFn(argc, argv);
    exit(0);
  };

  CmpInfoCallback = [&](std::unique_ptr<RetryInfoTy> &&ObjCmp) {
    addNewRetryInfo(std::move(ObjCmp));

    INPUTGEN_DEBUG(std::cerr << "Retrying..." << std::endl);

    delete InputGenRT;
    InputGenRT = nullptr;
    RunInputGen();
  };

  RunInputGen();

  return 0;
}
