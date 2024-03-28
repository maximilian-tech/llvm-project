

#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
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
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

using namespace llvm;

cl::OptionCategory InputGenCategory("input-gen Options");

static cl::opt<std::string> OutputDir("output-dir", cl::Required,
                                      cl::cat(InputGenCategory));

static cl::opt<std::string> InputGenRuntime(
    "input-gen-runtime",
    cl::desc("Input gen runtime to link into the instrumented module."),
    cl::cat(InputGenCategory));

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("Input file"),
                                          cl::cat(InputGenCategory));

static cl::opt<bool> CompileInputGenExecutable("compile-input-gen-executable",
                                               cl::cat(InputGenCategory));

constexpr char ToolName[] = "input-gen";

namespace llvm {
bool inputGenerationInstrumentModuleForFunction(Function &F,
                                                ModuleAnalysisManager &MAM);

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
    if (CompileInputGenExecutable) {
      if (InputGenRuntime.empty())
        fatalError("input-gen: Need to specify input-gen runtime to compile "
                   "executable.");

      ErrorOr<std::string> ClangOrErr =
          findClang(Argv[0], "ignoring-this-for-now");
      if (ClangOrErr) {
        Clang = *ClangOrErr;
      } else {
        errs() << "input-gen: Unable to find clang\n";
        fatalError("input-gen: Unable to generate input.");
      }
    }
  }

  void genForAllFunctions() {
    // TODO Use proper path concat function
    std::string Functions = OutputDir + "/" + "available_functions";
    auto Fs = std::ofstream(Functions);

    // TODO Maybe we can parallelize this loop
    for (auto &F : M.getFunctionList()) {
      if (F.isDeclaration())
        continue;

      Fs << F.getName().str() << std::endl;

      ValueToValueMapTy VMap;
      auto ClonedModule = CloneModule(M, VMap);
      auto &ClonedF = *cast<Function>(VMap[&F]);

      LoopAnalysisManager LAM;
      FunctionAnalysisManager FAM;
      CGSCCAnalysisManager CGAM;
      ModuleAnalysisManager MAM;

      PassBuilder PB;

      PB.registerModuleAnalyses(MAM);
      PB.registerCGSCCAnalyses(CGAM);
      PB.registerFunctionAnalyses(FAM);
      PB.registerLoopAnalyses(LAM);
      PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

      llvm::outs() << "Handling function @" << ClonedF.getName() << "\n";
      llvm::outs() << "Instrumenting...\n";
      if (!inputGenerationInstrumentModuleForFunction(ClonedF, MAM)) {
        llvm::outs() << "Instrumenting failed\n";
        continue;
      }
      llvm::outs() << "Instrumenting succeeded\n";

      llvm::outs() << "Generating input module...\n";

      if (generateInputModule(*ClonedModule, ClonedF)) {
        llvm::outs() << "Generating input module succeeded\n";
      } else {
        llvm::outs() << "Generating input module failed\n";
      }
    }
  }

  bool generateInputModule(Module &M, Function &F) {
    std::string Name = F.getName().str();
    // TODO Use proper path concat function
    std::string InstrumentedModule =
        OutputDir + "/" + "input-gen." + Name + ".instrumented.bc";
    std::string InputGenExecutable =
        OutputDir + "/" + "input-gen." + Name + ".executable";

    int InstrumentedModuleFD;
    std::error_code EC =
        sys::fs::openFileForWrite(InstrumentedModule, InstrumentedModuleFD);
    if (EC)
      // errc::permission_denied happens on Windows when we try to open a file
      // that has been marked for deletion.
      if (!(EC == std::errc::file_exists || EC == std::errc::permission_denied))
        return false;

    if (writeProgramToFile(InstrumentedModuleFD, M)) {
      errs() << ToolName << ": Error emitting bitcode to file '"
             << InstrumentedModule << "'!\n";
      close(InstrumentedModuleFD);
      exit(1);
    }
    close(InstrumentedModuleFD);

    if (CompileInputGenExecutable) {
      outs() << "Compiling " << InstrumentedModule << "\n";
      SmallVector<StringRef, 8> Args = {
          Clang, "-fopenmp",        "-O2", InputGenRuntime, InstrumentedModule,
          "-o",  InputGenExecutable};
      std::string ErrMsg;
      int Res = sys::ExecuteAndWait(
          Args[0], Args, /*Env=*/std::nullopt, /*Redirects=*/{},
          /*SecondsToWait=*/0, /*MemoryLimit=*/0, &ErrMsg);
      if (Res) {
        if (!ErrMsg.empty())
          errs() << "input-gen: gen binary compilation failed: " + ErrMsg +
                        "\n";
        else
          errs() << "input-gen: gen binary compilation failed.\n";
        return false;
      }
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
