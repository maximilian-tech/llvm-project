#include <cstdint>
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

extern "C" void __inputrun_run(char *);

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  char *InputName = argv[1];

  std::ifstream Input(InputName, std::ios::in | std::ios::binary);

  auto MemSize = readSingleEl<uint64_t>(Input);
  char *Memory = ccast(malloc(MemSize));
  Input.read(ccast(Memory), MemSize);

  // Relocations
  while (true) {
    auto RelocationLeft = readSingleEl<bool>(Input);
    if (!RelocationLeft)
      break;

    auto From = readSingleEl<uint64_t>(Input);
    auto To = readSingleEl<uint64_t>(Input);
    Input.read(ccast(&From), sizeof(From));
    Input.read(ccast(&To), sizeof(To));

    *((void **)&Memory[From]) = (void *)&Memory[To];
  }

  auto ArgsMemSize = readSingleEl<uint64_t>(Input);
  char *ArgsMemory = ccast(malloc(ArgsMemSize));
  Input.read(ccast(ArgsMemory), ArgsMemSize);

  __inputrun_run(ArgsMemory);

  free(ArgsMemory);
  free(Memory);
}
