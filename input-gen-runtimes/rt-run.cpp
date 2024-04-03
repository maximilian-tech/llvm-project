#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <vector>

template <typename T> static char *ccast(T *Ptr) {
  return reinterpret_cast<char *>(Ptr);
}

template <typename T> static T readSingleEl(std::ifstream &Input) {
  T El;
  Input.read(ccast(&El), sizeof(El));
  return El;
}

static std::vector<char *> Globals;
static size_t GlobalsIt = 0;

extern "C" {
void __inputgen_global(int32_t NumGlobals, void *Global, void **ReplGlobal,
                       int32_t GlobalSize) {
  assert(Globals.size() > GlobalsIt);
  memcpy(Global, Globals[GlobalsIt], GlobalSize);
}

void __inputrun_entry(char *);
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  char *InputName = argv[1];
  printf("Replay %s\n", InputName);

  std::ifstream Input(InputName, std::ios::in | std::ios::binary);

  auto Seed = readSingleEl<uint32_t>(Input);
  std::mt19937 Gen;
  std::uniform_int_distribution<> Rand;
  Gen.seed(Seed);

  auto MemSize = readSingleEl<uint64_t>(Input);
  char *Memory = ccast(malloc(MemSize));
  printf("MemSize %lu : %p\n", MemSize, Memory);

  std::map<uint64_t, char *> ObjMap;
  auto NumObjects = readSingleEl<uint32_t>(Input);
  printf("NO %u\n", NumObjects);
  for (uint32_t I = 0; I < NumObjects; I++) {
    auto ObjIdx = readSingleEl<uint32_t>(Input);
    auto Offset = readSingleEl<uint64_t>(Input);
    printf("O %u -> %lu\n", ObjIdx, Offset);

    assert(Offset <= MemSize);
    ObjMap[ObjIdx] = Memory + Offset;
  }

  auto NumGlobals = readSingleEl<uint32_t>(Input);
  for (uint32_t I = 0; I < NumGlobals; I++) {
    auto ObjIdx = readSingleEl<uint32_t>(Input);
    printf("G %u -> %lu\n", I, ObjIdx);
    assert(ObjMap.count(ObjIdx));
    Globals.push_back(ObjMap[ObjIdx]);
  }

  auto NumVals = readSingleEl<uint32_t>(Input);
  for (uint32_t I = 0; I < NumVals; ++I) {
    uint32_t ObjIdx = readSingleEl<uint32_t>(Input);
    ptrdiff_t Offset = readSingleEl<ptrdiff_t>(Input);
    uint32_t ContentOrIdxKind = readSingleEl<int32_t>(Input);
    uintptr_t ContentOrIdx = readSingleEl<uintptr_t>(Input);
    uint32_t Size = readSingleEl<uint32_t>(Input);
    printf("ObjIdx %u\n", ObjIdx);
    assert(ObjMap.count(ObjIdx));
    char *Tgt = ObjMap[ObjIdx] + Offset;
    char *Src;
    if (ContentOrIdxKind == /* Idx */ 0) {
      assert(ObjMap.count(ContentOrIdx));
      ContentOrIdx = (uintptr_t)ObjMap[ContentOrIdx];
    } else if (ContentOrIdxKind == /* Content */ 1) {
    } else {
      printf("Error\n");
      exit(4);
    }
    Src = ccast(&ContentOrIdx);
    memcpy(Tgt, Src, Size);
  }

  auto NumArgs = readSingleEl<uint32_t>(Input);
  char *ArgsMemory = ccast(malloc(NumArgs * sizeof(uintptr_t)));
  printf("Args %lu : %p\n", NumArgs, ArgsMemory);
  for (uint32_t I = 0; I < NumArgs; ++I) {
    auto Content = readSingleEl<uintptr_t>(Input);
    auto ObjIdx = readSingleEl<int32_t>(Input);
    if (ObjIdx != -1) {
      assert(ObjMap.count(ObjIdx));
      Content = (uintptr_t)ObjMap[ObjIdx];
    }
    memcpy(ArgsMemory + I * sizeof(void *), &Content, sizeof(void *));
  }

  printf("Run\n");
  __inputrun_entry(ArgsMemory);

  free(ArgsMemory);
  free(Memory);
}
