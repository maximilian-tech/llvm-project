#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <omp.h>
#include <random>
#include <type_traits>
#include <vector>

static void *advance(void *Ptr, uint64_t Bytes) {
  return reinterpret_cast<char *>(Ptr) + Bytes;
}

struct ObjectTy {
  ObjectTy(void *Data, uint64_t Size, bool Artifical = true)
      : Size(Size), Data(Data) {}

  uint64_t Size = 0;
  void *Data = nullptr;
  void *begin() { return Data; }
  void *end() { return advance(Data, Size); }
};

typedef uintptr_t ArgTy;
template <typename T> static T fromArgTy(ArgTy U) {
  static_assert(sizeof(T) <= sizeof(U));
  T A;
  memcpy(&A, &U, sizeof(A));
  return A;
}

template <typename T> static ArgTy toArgTy(T A) {
  ArgTy U;
  static_assert(sizeof(T) <= sizeof(U));
  memcpy(&U, &A, sizeof(A));
  return U;
}

static constexpr uint64_t HeapSize = 1UL << 32;

struct HeapTy : ObjectTy {
  HeapTy(HeapTy *LastHeap = nullptr)
      : ObjectTy(malloc(HeapSize), HeapSize), LastHeap(LastHeap) {
    printf("New heap [%p:%p)\n", begin(), end());
  }
  HeapTy *LastHeap = nullptr;

  std::bitset<(HeapSize / 8)> UsedSet;

  bool isUsed(void *Ptr, int64_t Size) {
    uintptr_t Offset = (uintptr_t)Ptr - (uintptr_t)Data;
    uintptr_t Idx = Offset / 8;
    bool Res = true;
    do {
      Res &= UsedSet[Idx++];
      Size -= 8;
    } while (Size > 0);
    return Res;
  }
  void markUsed(void *Ptr, int64_t Size) {
    uintptr_t Offset = (uintptr_t)Ptr - (uintptr_t)Data;
    uintptr_t Idx = Offset / 8;
    do {
      UsedSet.set(Idx++);
      Size -= 8;
    } while (Size > 0);
  }

  template <typename T> T read(void *Ptr, void *Base);

  template <typename T>
  void write(T *Ptr, T Val, uint32_t Size, bool DueToRead = false) {
    if (begin() <= Ptr && advance(Ptr, Size) < end()) {
      markUsed(Ptr, Size);
      memcpy(Ptr, &Val, Size);
      if constexpr (std::is_pointer<T>::value)
        if (DueToRead)
          PtrMap[Ptr] = Val;
    } else if (LastHeap) {
      LastHeap->write(Ptr, Val, Size, DueToRead);
    } else {
      //      printf("Out of bound write at %p\n", (void *)Ptr);
      // exit(1);
    }
  }

  std::map<void *, void *> PtrMap;
};

struct InputGenRTTy {
  InputGenRTTy(const char *ExecPath, const char *OutputDir, int Seed)
      : Seed(Seed), OutputDir(OutputDir), ExecPath(ExecPath),
        Heap(new HeapTy()) {
    Gen.seed(Seed);
  }
  ~InputGenRTTy() { report(); }

  int Seed;
  std::string OutputDir;
  std::filesystem::path ExecPath;
  std::mt19937 Gen;
  std::uniform_int_distribution<> Rand;
  std::vector<char> Conds;

  int rand() { return Rand(Gen); }

  void *getNewObj(uint64_t Size, bool Artifical) {
    printf("Get new obj %lu\n", Size);
    void *Loc;
    if (LastObj && advance(LastObj->end(), Size) < Heap->end()) {
      Loc = LastObj->end();
    } else {
      if (LastObj)
        Heap = new HeapTy(Heap);
      Loc = Heap->begin();
    }
    LastObj = new ObjectTy(Loc, Size, Artifical);
    ObjMap[Loc] = LastObj;
    return Loc;
  }

  template <typename T> T getNewValue(int Max = 1000) {
    NumNewValues++;
    return rand() % Max;
  }
  template <> void *getNewValue<void *>(int Max) {
    NumNewValues++;
    memset(Storage, 0, 64);
    if (rand() % 1000) {
      *reinterpret_cast<void **>(Storage) = getNewObj(1024 * 1024, true);
    }
    void *V = *((void **)(Storage));
    return V;
  }

  uint64_t NumNewValues = 0;
  char Storage[64];

  std::vector<ArgTy> Args;

  ObjectTy *LastObj = 0;
  std::map<void *, ObjectTy *> ObjMap;

  HeapTy *Heap;

  void report() {
    if (OutputDir == "-") {
      // TODO cross platform
      std::ofstream Null("/dev/null");
      report(stdout, Null);
    } else {
      auto FileName = ExecPath.filename().string();
      std::string ReportOutName(OutputDir + "/" + FileName + ".report." +
                                std::to_string(Seed) + ".c");
      std::string InputOutName(OutputDir + "/" + FileName + ".code." +
                               std::to_string(Seed) + ".c");
      std::ofstream InputOutStream(InputOutName,
                                   std::ios::out | std::ios::binary);
      FILE *ReportOutFD = fopen(ReportOutName.c_str(), "w");
      if (!ReportOutFD) {
        fprintf(stderr, "Could not open %s\n", ReportOutName.c_str());
        return;
      }
      report(ReportOutFD, InputOutStream);
      fclose(ReportOutFD);
    }
  }

  template <typename T> const char *ccast(T *Ptr) {
    return reinterpret_cast<const char *>(Ptr);
  }
  template <typename T> T writeSingleEl(std::ofstream &Output, T El) {
    Output.write(ccast(&El), sizeof(El));
    return El;
  }

  void report(FILE *ReportOut, std::ofstream &InputOut) {
    fprintf(ReportOut, "Args (%zu total)\n", Args.size());
    for (size_t I = 0; I < Args.size(); ++I)
      fprintf(ReportOut, "Arg %zu: %p\n", I, (void *)Args[I]);
    fprintf(ReportOut, "\nNum new values: %lu\n", NumNewValues);
    fprintf(ReportOut, "\nHeap PtrMap: %lu\n", Heap->PtrMap.size());
    fprintf(ReportOut, "\nObjects (%zu total)\n", ObjMap.size());

    uintptr_t Min = -1, Max = 0;
    for (auto &It : ObjMap) {
      auto *ObjLIt = It.second->begin();
      auto *ObjRIt = It.second->end();
      while (ObjRIt != ObjLIt) {
        if (Heap->isUsed(ObjLIt, 1))
          break;
        ObjLIt = advance(ObjLIt, 1);
      }
      while (ObjRIt != ObjLIt) {
        if (Heap->isUsed(advance(ObjRIt, -1), 1))
          break;
        ObjRIt = advance(ObjRIt, -1);
      }
      if (ObjLIt == ObjRIt)
        continue;
      fprintf(ReportOut, "Obj [%p : %p] %lu)\n", ObjLIt, ObjRIt,
              ((char *)ObjRIt - (char *)ObjLIt));
      Min = std::min(Min, uintptr_t(ObjLIt));
      Max = std::max(Max, uintptr_t(ObjRIt));
    }
    fprintf(ReportOut, "Heap %p : Min %p :: Max %p\n", Heap->begin(),
            (void *)Min, (void *)Max);
    fprintf(ReportOut, "%lu\n", (char *)Min - (char *)Heap->begin());

    std::map<uint64_t, void *> Remap;
    std::map<void *, uint64_t> Repos;
    uintptr_t Idx = 0;
    uint64_t TotalSize = 0;
    InputOut.seekp(sizeof(Idx));
    for (auto &It : ObjMap) {
      auto *ObjLIt = It.second->begin();
      auto *ObjRIt = It.second->end();
      while (ObjRIt != ObjLIt) {
        if (Heap->isUsed(ObjLIt, 1))
          break;
        ObjLIt = advance(ObjLIt, 1);
      }
      while (ObjRIt != ObjLIt) {
        if (Heap->isUsed(advance(ObjRIt, -1), 1))
          break;
        ObjRIt = advance(ObjRIt, -1);
      }
      // if (ObjLIt == ObjRIt)
      //   continue;
      uint64_t Size =
          reinterpret_cast<char *>(ObjRIt) - reinterpret_cast<char *>(ObjLIt);
      printf("Size %lu\n", Size);
      TotalSize += Size;
      //      writeSingleEl(InputOut, Size);
      InputOut.write(reinterpret_cast<const char *>(ObjLIt), Size);

      Repos[It.second->begin()] = Idx;
      auto PtrEnd = Heap->PtrMap.end();
      while (ObjRIt != ObjLIt) {
        auto PtrIt = Heap->PtrMap.find(ObjLIt);
        if (PtrIt != PtrEnd) {
          Remap[Idx] = PtrIt->second;
        }
        ObjLIt = advance(ObjLIt, 1);
        ++Idx;
      }
    }
    printf("TotalSize %lu\n", TotalSize);
    auto AfterMemory = InputOut.tellp();
    InputOut.seekp(0);
    writeSingleEl(InputOut, TotalSize);
    InputOut.seekp(AfterMemory);

    uint64_t ArgsSize = Args.size() * sizeof(Args[0]);
    writeSingleEl(InputOut, ArgsSize);
    InputOut.write(ccast(Args.data()), ArgsSize);
    auto AfterArgs = InputOut.tellp();

    uint64_t RemapNum = 0;
    // We will write the RemapNum here later
    InputOut.seekp(sizeof(RemapNum), std::ios_base::cur);

    for (auto &It : Remap) {
      if (Repos.count(It.second)) {
        uint32_t MemRemap = 0;
        writeSingleEl(InputOut, /* TODO enum */ MemRemap);
        writeSingleEl(InputOut, It.first);
        writeSingleEl(InputOut, Repos[It.second]);
        RemapNum++;
      }
    }
    for (uint64_t ArgNo = 0; ArgNo < Args.size(); ++ArgNo) {
      auto It = Repos.find((void *)Args[ArgNo]);
      if (It == Repos.end())
        continue;
      uint32_t ArgRemap = 1;
      writeSingleEl(InputOut, /* TODO enum */ ArgRemap);
      writeSingleEl(InputOut, ArgNo);
      writeSingleEl(InputOut, It->second);
      //      *Arg = Repos[It.second];
      RemapNum++;
    }
    // auto AfterRemap = InputOut.tellp();
    InputOut.seekp(AfterArgs);
    writeSingleEl(InputOut, RemapNum);
    // InputOut.seekp(AfterRemap);
  }
};

static InputGenRTTy *InputGenRT;
#pragma omp threadprivate(InputGenRT)

static InputGenRTTy &getInputGenRT() { return *InputGenRT; }

template <typename T> T HeapTy::read(void *Ptr, void *Base) {
  if (begin() > Ptr || advance(Ptr, sizeof(T)) >= end()) {
    if (LastHeap)
      return LastHeap->read<T>(Ptr, Base);
    //    printf("Out of bound read at %p < %p:%p < %p\n", begin(), Ptr,
    //           advance(Ptr, sizeof(T)), end());
    return *reinterpret_cast<T *>(Ptr);
  }
  if (!isUsed(Ptr, sizeof(T))) {
    write((T *)Ptr, getInputGenRT().getNewValue<T>(), sizeof(T), true);
    assert(isUsed(Ptr, sizeof(T)));
  }
  assert(begin() <= Ptr && advance(Ptr, sizeof(T)) < end());
  return *reinterpret_cast<T *>(Ptr);
}

extern "C" {
void __inputgen_version_mismatch_check_v1() {}

void __inputgen_init() {
  // getInputGenRT().init();
}
void __inputgen_deinit() {
  // getInputGenRT().init();
}

#define RW(TY, NAME)                                                           \
  void __inputgen_read_##NAME(void *Ptr, int64_t Val, int32_t Size,            \
                              void *Base) {                                    \
    if (Size == sizeof(void *)) {                                              \
      void *V = getInputGenRT().Heap->read<void *>(Ptr, Base);                 \
    } else {                                                                   \
      int32_t V = getInputGenRT().Heap->read<int32_t>(Ptr, Base);              \
    }                                                                          \
  }                                                                            \
  void __inputgen_write_##NAME(void *Ptr, int64_t Val, int32_t Size,           \
                               void *Base) {                                   \
    getInputGenRT().Heap->write<TY>((TY *)Ptr, (TY)Val, Size);                 \
  }                                                                            \
  void __record_read_##NAME(void *Ptr, int64_t Val, int32_t Size,              \
                            void *Base) {                                      \
    if (Size == sizeof(void *)) {                                              \
      void *V = getInputGenRT().Heap->read<void *>(Ptr, Base);                 \
      printf("Read %p[:%i] (%p): %p\n", Ptr, Size, Base, V);                   \
    } else {                                                                   \
      int32_t V = getInputGenRT().Heap->read<int32_t>(Ptr, Base);              \
      printf("Read %p[:%i] (%p): %i\n", Ptr, Size, Base, V);                   \
    }                                                                          \
  }                                                                            \
  void __record_write_##NAME(void *Ptr, int64_t Val, int32_t Size,             \
                             void *Base) {                                     \
    getInputGenRT().Heap->write<TY>((TY *)Ptr, (TY)Val, Size);                 \
  }

RW(bool, i1)
RW(char, i8)
RW(short, i16)
RW(int32_t, i32)
RW(int64_t, i64)
RW(float, float)
RW(double, double)
RW(void *, ptr)
#undef RW

#define ARG(TY, NAME)                                                          \
  TY __inputgen_arg_##NAME(TY Arg) {                                           \
    getInputGenRT().Args.push_back(                                            \
        toArgTy<TY>(getInputGenRT().getNewValue<TY>()));                       \
    printf("arg %p\n", (void *)getInputGenRT().Args.back());                   \
    return fromArgTy<TY>(getInputGenRT().Args.back());                         \
  }

ARG(bool, i1)
ARG(char, i8)
ARG(short, i16)
ARG(int32_t, i32)
ARG(int64_t, i64)
ARG(float, float)
ARG(double, double)
ARG(void *, ptr)
#undef ARG

void __inputgen_entry(int, char **);
}

int main(int argc, char **argv) {
  const char *OutputDir = "-";
  int Start = 0;
  int End = 1;

  if (argc == 4) {
    OutputDir = argv[1];
    Start = std::stoi(argv[2]);
    End = std::stoi(argv[3]);
  } else if (argc != 1) {
    std::cerr << "Wrong usage." << std::endl;
    return 1;
  }

  int Size = End - Start;
  if (Size <= 0)
    return 1;

  std::cout << "Will generate " << Size << " inputs." << std::endl;

#pragma omp parallel for schedule(dynamic, 5)
  for (int I = Start; I < End; I++) {
    InputGenRTTy LocalInputGenRT(argv[0], OutputDir, I);
    InputGenRT = &LocalInputGenRT;
    __inputgen_entry(argc, argv);
  }

  return 0;
}
