

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
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;

cl::OptionCategory InputGenCategory("input-gen Options");

static cl::opt<std::string> OutputDir("output-dir", cl::Required, cl::cat(InputGenCategory));

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("Input file"),
                                          cl::cat(InputGenCategory));

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

  for (auto &F : M->getFunctionList()) {
    if (F.isDeclaration())
      continue;

    llvm::outs() << "Generating input for function " << F.getName() << "...\n";


  }

  return 0;
}
