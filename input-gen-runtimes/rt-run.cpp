#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
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

  auto MemSize = readSingleEl<uint64_t>(Input);
  char *Memory = ccast(malloc(MemSize));
  Input.read(ccast(Memory), MemSize);
  printf("MemSize %lu : %p\n", MemSize, Memory);

  auto ArgsMemSize = readSingleEl<uint64_t>(Input);
  char *ArgsMemory = ccast(malloc(ArgsMemSize));
  Input.read(ccast(ArgsMemory), ArgsMemSize);
  printf("Args %lu : %p\n", ArgsMemSize, ArgsMemory);

  auto RemapNum = readSingleEl<uint64_t>(Input);
  for (uint64_t I = 0; I < RemapNum; I++) {
    auto Kind = readSingleEl<uint32_t>(Input);
    auto From = readSingleEl<uint64_t>(Input);
    auto To = readSingleEl<uint64_t>(Input);
    printf("%lu) %i: %lu -> %lu\n", I, Kind, From, To);
    assert(To <= MemSize);
    if (Kind == 0) {
      // Heap
      assert(From < MemSize);
      *(&((void **)Memory)[From]) = (void *)&Memory[To];
    } else if (Kind == 1) {
      // Arguments
      assert(From < ArgsMemSize / sizeof(uintptr_t));
      *(&((void **)ArgsMemory)[From]) = (void *)&Memory[To];
    } else if (Kind == 2) {
      // Globals
      Globals.resize(std::max(Globals.size(), From + 1));
      assert(!Globals[From]);
      Globals[From] = &Memory[To];
      printf("Global stored\n");
    } else {
      exit(2);
    }
  }

  for (unsigned long I = 0; I < ArgsMemSize; I += sizeof(void *))
    printf("[%lu] %i : %f : %li : %lf : %p\n", I, *((int *)&ArgsMemory[I]),
           *((float *)&ArgsMemory[I]), *((long int *)&ArgsMemory[I]),
           *((double *)&ArgsMemory[I]), *((void **)&ArgsMemory[I]));

  printf("Run\n");
  __inputrun_entry(ArgsMemory);

  free(ArgsMemory);
  free(Memory);
}
