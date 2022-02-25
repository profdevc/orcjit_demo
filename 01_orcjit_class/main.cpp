/*
Ref: https://releases.llvm.org/13.0.0/docs/tutorial/index.html#building-a-jit-in-llvm
Usage:
  Compile
    clang++ -g main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit support nativecodegen` -O3 -o main.cpp.o
  execute
    ./main.cpp.o file1.cpp file2.cpp --args para1 para2
*/
#include "llvm/ExecutionEngine/JITLink/EHFrameSupport.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"

#include "orcjit.h"
#include <unistd.h>

using namespace llvm;
using namespace llvm::orc;

static cl::list<std::string> InputFiles(cl::Positional, cl::OneOrMore,
                                        cl::desc("input files"));

static cl::list<std::string> InputArgv("args", cl::Positional,
                                       cl::desc("<program arguments>..."),
                                       cl::ZeroOrMore, cl::PositionalEatsArgs);

static std::unique_ptr<OrcJIT> TheJIT;
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static ExitOnError ExitOnErr;

int main(int argc, char **argv)
{
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  TheJIT = ExitOnErr(OrcJIT::Create());
  printf("\n--------   Adding Module  --------\n");
  cl::ParseCommandLineOptions(argc, argv, "OrcJIT");

  //add modules to jit
  for (const auto &InputFile : InputFiles)
  {
    SMDiagnostic Err;
    auto Ctx = std::make_unique<LLVMContext>();
    std::string InputFilell = InputFile;
    if (InputFile.substr(InputFile.find_last_of('.'), InputFile.length() - InputFile.find_last_of('.')) == ".cpp")
    {
      InputFilell = InputFile.substr(0, InputFile.find_last_of('.')) + ".ll";
      std::string cmd = "clang++ -S -emit-llvm " + InputFile + " -o " + InputFilell;
      FILE *cmdp = NULL;
      cmdp = popen(cmd.c_str(), "r");
      if (!cmdp)
      {
        perror("popen");
        exit(EXIT_FAILURE);
      }
      else
        printf("clang++: emit %s to %s\n", InputFile.c_str(), InputFilell.c_str());
    }
    else if ((InputFile).substr(InputFile.find_last_of('.'), InputFile.length() - InputFile.find_last_of('.')) != ".ll")
    {

      printf("%s: file format must be .cpp\n", InputFile.c_str());
      exit(-1);
    }

    auto M = parseIRFile(InputFilell, Err, *Ctx);
    if (!M)
    {
      Err.print(argv[0], errs());
      return 1;
    }

    ExitOnErr(TheJIT->addModule(ThreadSafeModule(std::move(M), std::move(Ctx))));
    printf("Module %s has been add to JIT\n", InputFilell.c_str());
  }

  // Look up the JIT'd function, cast it to main function pointer, then call it.
  auto MainSym = ExitOnErr(TheJIT->lookup("main"));
  auto Main =
      jitTargetAddressToFunction<int (*)(int, char *[])>(MainSym.getAddress());

  printf("\n-------- Executing Module --------\n");
  return runAsMain(Main, InputArgv, StringRef(InputFiles.front()));
}