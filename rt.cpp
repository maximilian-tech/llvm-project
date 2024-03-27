#include <bitset>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
      printf("Out of bound write at %p\n", (void *)Ptr);
      exit(1);
    }
  }

  std::map<void *, void *> PtrMap;
};

struct InputGenRTTy {
  InputGenRTTy(const char *OutputDir, int Seed)
      : Seed(Seed), OutputDir(OutputDir), Heap(new HeapTy()) {
    Gen.seed(Seed);
  }
  ~InputGenRTTy() { report(); }

  int Seed;
  std::string OutputDir;
  std::mt19937 Gen;
  std::uniform_int_distribution<> Rand;
  std::vector<char> Conds;

  int rand() { return Rand(Gen); }

  void *getNewObj(uint64_t Size, bool Artifical) {
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
    if (rand() % 12) {
      *reinterpret_cast<void **>(Storage) = getNewObj(1024 * 1024, true);
    }
    void *V = *((void **)(Storage));
    return V;
  }

  uint64_t NumNewValues = 0;
  char Storage[64];

  std::vector<uintptr_t> Args;

  ObjectTy *LastObj = 0;
  std::map<void *, ObjectTy *> ObjMap;

  HeapTy *Heap;

  void report() {
    if (OutputDir == "-") {
      report(stdout, stderr);
    } else {
      std::string ReportOut(OutputDir + "/" + "report." + std::to_string(Seed) + ".c");
      std::string CodeOut(OutputDir + "/" + "code." + std::to_string(Seed) + ".c");
      FILE *ReportOutFD = fopen(ReportOut.c_str(), "w");
      if (!ReportOutFD) {
        fprintf(stderr, "Could not open %s\n", ReportOut.c_str());
        return;
      }
      FILE *CodeOutFD = fopen(CodeOut.c_str(), "w");;
      if (!CodeOutFD) {
        fprintf(stderr, "Could not open %s\n", CodeOut.c_str());
        fclose(ReportOutFD);
        return;
      }
      report(ReportOutFD, CodeOutFD);
      fclose(ReportOutFD);
      fclose(CodeOutFD);
    }
  }

  void report(FILE *ReportOut, FILE *CodeOut) {

    fprintf(ReportOut, "Args (%zu total)\n", Args.size());
    for (size_t I = 0; I < Args.size(); ++I)
      fprintf(ReportOut, "Arg %zu: %lu\n", I, Args[I]);
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
    fprintf(ReportOut, "Heap %p : Min %p :: Max %p\n", Heap->begin(), (void *)Min,
           (void *)Max);
    fprintf(ReportOut, "%lu\n", (char *)Min - (char *)Heap->begin());

    fprintf(CodeOut, "#include <stdint.h>\n");
    fprintf(CodeOut, "  char Memory[] = {");
    //    fprintf(ReportOut, "  uintptr_t MemoryDiff = (uintptr_t)Memory -
    //    (uintptr_t)%p;\n", Heap->begin());
    std::map<int, void *> Remap;
    std::map<void *, int> Repos;
    uintptr_t Idx = 0;
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
      Repos[It.second->begin()] = Idx;
      auto PtrEnd = Heap->PtrMap.end();
      while (ObjRIt != ObjLIt) {
        auto PtrIt = Heap->PtrMap.find(ObjLIt);
        if (PtrIt != PtrEnd) {
          Remap[Idx] = PtrIt->second;
        }
        if (Idx)
          fprintf(CodeOut, ",%i", (int)*(char *)ObjLIt);
        else
          fprintf(CodeOut, "%i", (int)*(char *)ObjLIt);
        ObjLIt = advance(ObjLIt, sizeof(char));
        ++Idx;
      }
    }
    fprintf(CodeOut, "};\n\n");
    fprintf(CodeOut, "char Conds[] = {");
    Idx = 0;
    char C = 0;
    fprintf(ReportOut, "Conds: %zu\n", Conds.size());
    while (Idx < Conds.size()) {
      auto Rem = Idx % 8;
      fprintf(ReportOut, " %i : %i : %i\n", C, Conds[Idx], Conds[Idx] << Rem);
      C |= (Conds[Idx] << Rem);
      fprintf(ReportOut, "=%i : %i : %i\n", C, Conds[Idx], Conds[Idx] << Rem);
      Idx++;
      if ((Idx % 8) == 0) {
        if (Idx > 8)
          fprintf(CodeOut, ",");
        fprintf(CodeOut, "%i", (int)C);
        C = 0;
      }
    }
    if ((Idx % 8) != 0) {
      if (Idx > 8)
        fprintf(CodeOut, ",");
      fprintf(CodeOut, "%i", (int)C);
    }
    fprintf(CodeOut, "};\n\n");
    fprintf(CodeOut, "struct LinkedList;\n\n");
    fprintf(CodeOut, "extern \"C\" void foo(LinkedList*);\n\n");
    fprintf(CodeOut, "int main() {\n");
    for (auto &It : Remap) {
      if (Repos.count(It.second))
        fprintf(CodeOut, "  *((void**)&Memory[%i]) = (void*)&Memory[%i];\n",
                It.first, Repos[It.second]);
    }
    fprintf(CodeOut, "  foo((LinkedList*)(Memory + %lu));\n",
            (char *)Args[0] - (char *)Min);
    fprintf(CodeOut, "}\n");
  }
};

static InputGenRTTy *InputGenRT;
#pragma omp threadprivate(InputGenRT)

static InputGenRTTy &getInputGenRT() { return *InputGenRT; }

template <typename T> T HeapTy::read(void *Ptr, void *Base) {
  if (!isUsed(Ptr, sizeof(T))) {
    write((T *)Ptr, getInputGenRT().getNewValue<T>(), sizeof(T), true);
    assert(isUsed(Ptr, sizeof(T)));
  }
  if (begin() <= Ptr && advance(Ptr, sizeof(T)) < end()) {
    return *reinterpret_cast<T *>(Ptr);
  }
  if (LastHeap)
    return LastHeap->read<T>(Ptr, Base);
  printf("Out of bound read at %p < %p:%p < %p\n", begin(), Ptr,
         advance(Ptr, sizeof(T)), end());
  exit(1);
}

extern "C" {
void __inputgen_version_mismatch_check_v1() {}

void __inputgen_init() {
  // getInputGenRT().init();
}

#define READ(TY)                                                               \
  void __inputgen_read_##TY(void *Ptr, int32_t Size, void *Base) {             \
    if (Size == sizeof(void *)) {                                              \
      void *V = getInputGenRT().Heap->read<void *>(Ptr, Base);                 \
      printf("Read %p[:%i] (%p): %p\n", Ptr, Size, Base, V);                   \
    } else {                                                                   \
      int32_t V = getInputGenRT().Heap->read<int32_t>(Ptr, Base);              \
      printf("Read %p[:%i] (%p): %i\n", Ptr, Size, Base, V);                   \
    }                                                                          \
  }

READ(i1)
READ(i8)
READ(i16)
READ(i32)
READ(i64)
READ(float)
READ(double)
READ(ptr)
#undef READ

#define ARG(TY, NAME)                                                          \
  TY __inputgen_arg_##NAME() {                                                 \
    getInputGenRT().Args.push_back(                                            \
        uintptr_t(getInputGenRT().getNewValue<TY>()));                         \
    return (TY)getInputGenRT().Args.back();                                    \
  }

ARG(bool, i1)
ARG(char, i8)
ARG(short, 16)
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
    InputGenRTTy LocalInputGenRT(OutputDir, I);
    InputGenRT = &LocalInputGenRT;
    printf(".");
    __inputgen_entry(argc, argv);
  }

  return 0;
}
