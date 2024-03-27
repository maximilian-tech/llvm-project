

#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;

cl::OptionCategory InputGenCategory("input-gen Options");

static cl::opt<std::string> OutputDir("output-dir", cl::Required,
                                      cl::cat(InputGenCategory));

static cl::opt<std::string> InputGenDriver("input-gen-driver", cl::Required,
                                           cl::cat(InputGenCategory));

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("Input file"),
                                          cl::cat(InputGenCategory));

namespace llvm {
cl::opt<bool> SaveTemps("save-temps", cl::init(false),
                        cl::desc("Save temporary files"));
}

constexpr char ToolName[] = "input-gen";

namespace llvm {
bool inputGenerationInstrumentModuleForFunction(Function &F);
} // namespace llvm

[[noreturn]] static void fatalError(const Twine &Message) {
  errs() << Message << "\n";
  exit(1);
}

std::string createTempFile(const Twine &Prefix, StringRef Suffix) {
  std::error_code EC;
  SmallString<128> FileName;
  if ((EC = sys::fs::createTemporaryFile(Prefix, Suffix, FileName)))
    fatalError("Unable to create temp file: " + EC.message());
  return static_cast<std::string>(FileName);
}

ErrorOr<std::string> findClang(const char *Argv0, StringRef Triple) {
  // This just needs to be some symbol in the binary.
  void *P = (void *)(intptr_t)findClang;
  std::string MainExecPath = llvm::sys::fs::getMainExecutable(Argv0, P);
  if (MainExecPath.empty())
    MainExecPath = Argv0;

  ErrorOr<std::string> Path = std::error_code();
  std::string TargetClang = (Triple + "-clang++").str();
  std::string VersionedClang = ("clang++-" + Twine(LLVM_VERSION_MAJOR)).str();
  for (const auto *Name :
       {TargetClang.c_str(), VersionedClang.c_str(), "clang++"}) {
    for (const StringRef Parent : {llvm::sys::path::parent_path(MainExecPath),
                                   llvm::sys::path::parent_path(Argv0)}) {
      // Look for various versions of "clang" first in the MainExecPath parent
      // directory and then in the argv[0] parent directory.
      // On Windows (but not Unix) argv[0] is overwritten with the eqiuvalent
      // of MainExecPath by InitLLVM.
      Path = sys::findProgramByName(Name, Parent);
      if (Path)
        return Path;
    }
  }

  // If no parent directory known, or not found there, look everywhere in PATH
  for (const auto *Name : {"clang", "clang-cl"}) {
    Path = sys::findProgramByName(Name);
    if (Path)
      return Path;
  }
  return Path;
}

struct DiscardTemp {
  sys::fs::TempFile &File;
  ~DiscardTemp();
};

DiscardTemp::~DiscardTemp() {
  if (SaveTemps) {
    if (Error E = File.keep())
      errs() << "Failed to keep temp file " << toString(std::move(E)) << '\n';
    return;
  }
  if (Error E = File.discard())
    errs() << "Failed to delete temp file " << toString(std::move(E)) << '\n';
}

static bool writeProgramToFile(int FD, const Module &M) {
  raw_fd_ostream OS(FD, /*shouldClose*/ false);
  WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder*/ false);
  OS.flush();
  if (!OS.has_error())
    return false;
  OS.clear_error();
  return true;
}

class InputGenOrchestration {
public:
  Module &M;
  std::string Clang;
  InputGenOrchestration(Module &M) : M(M){};
  void init(int Argc, char **Argv) {
    ErrorOr<std::string> ClangOrErr =
        findClang(Argv[0], "ignoring-this-for-now");
    if (ClangOrErr) {
      Clang = *ClangOrErr;
    } else {
      errs() << "input-gen: Unable to find clang\n";
      fatalError("input-gen: Unable to generate input.");
    }
  }

  void genForAllFunctions() {
    // maybe we can parallelize this loop
    for (auto &F : M.getFunctionList()) {
      if (F.isDeclaration())
        continue;

      ValueToValueMapTy VMap;
      auto ClonedModule = CloneModule(M, VMap);
      auto &ClonedF = *cast<Function>(VMap[&F]);

      llvm::outs() << "Handling function @" << ClonedF.getName() << "\n";
      llvm::outs() << "Instrumenting...\n";
      if (!inputGenerationInstrumentModuleForFunction(ClonedF)) {
        llvm::outs() << "Instrumenting failed\n";
        continue;
      }
      llvm::outs() << "Instrumenting succeeded\n";

      llvm::outs() << "Generating input...\n";

      if (generateInput(*ClonedModule)) {
        llvm::outs() << "Generating input succeeded\n";
      } else {
        llvm::outs() << "Generating input failed\n";
      }
    }
  }
  bool generateInput(Module &M, Function &F) {
    std::string Prefix = "inputgen-" + M.getName().str() + "-" + F.getName().str();
    std::string OutExecutable = createTempFile(Prefix, "run");
    // Use proper path concat function
    std::string OutInputGenerated = OutputDir + "/" + Prefix + ".c";

    auto Temp = sys::fs::TempFile::create(Prefix + "-instrumented-%%%%%%%.bc");
    if (!Temp) {
      errs() << ToolName
             << ": Error making unique filename: " << toString(Temp.takeError())
             << "\n";
      exit(1);
    }
    DiscardTemp Discard{*Temp};
    if (writeProgramToFile(Temp->FD, M)) {
      errs() << ToolName << ": Error emitting bitcode to file '"
             << Temp->TmpName << "'!\n";
      exit(1);
    }

    // TODO discard the executable too

    outs() << "Compiling " << Temp->TmpName << "\n";
    SmallVector<StringRef, 8> Args = {Clang,         "-O2", InputGenDriver,
                                      Temp->TmpName, "-o",  OutExecutable};
    std::string ErrMsg;
    int Res = sys::ExecuteAndWait(
        Args[0], Args, /*Env=*/std::nullopt, /*Redirects=*/{},
        /*SecondsToWait=*/0, /*MemoryLimit=*/0, &ErrMsg);
    if (Res) {
      if (!ErrMsg.empty())
        errs() << "input-gen: gen binary compilation failed: " + ErrMsg + "\n";
      else
        errs() << "input-gen: gen binary compilation failed.\n";
      return false;
    }

    outs() << "Executing " << OutExecutable << "\n";
    Res = sys::ExecuteAndWait(OutExecutable, {OutExecutable},
                              /*Env=*/std::nullopt, /*Redirects=*/{},
                              /*SecondsToWait=*/0, /*MemoryLimit=*/0, &ErrMsg);
    if (Res) {
      if (!ErrMsg.empty())
        errs() << "input-gen: gen binary execution failed: " + ErrMsg + "\n";
      else
        errs() << "input-gen: gen binary execution failed.\n";
      return false;
    }

    return true;
  }
};

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(InputGenCategory);
  cl::ParseCommandLineOptions(argc, argv, "Input gen");

  ExitOnError ExitOnErr("input-gen: ");
  LLVMContext Context;

  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Diag, Context);
  if (!M) {
    Diag.print("input-gen", errs());
    return 1;
  }

  InputGenOrchestration IGO(*M);

  IGO.init(argc, argv);

  IGO.genForAllFunctions();

  return 0;
}
