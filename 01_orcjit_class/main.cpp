/*
Ref: https://releases.llvm.org/13.0.0/docs/tutorial/BuildingAJIT2.html
Usage:
clang++ -g main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit support nativecodegen` -O3 -o main.cpp.o
./main.cpp.o -f mul_func.ll -f mul_main.ll
*/
#include "llvm/ADT/APFloat.h"
#include "llvm/ExecutionEngine/JITLink/EHFrameSupport.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
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

static std::unique_ptr<OrcJIT> TheJIT;
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static ExitOnError ExitOnErr;

static void InitializeModule()
{
  // Open a new context and module.
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("orcjit demo", *TheContext);
  TheModule->setDataLayout(TheJIT->getDataLayout());
}

ThreadSafeModule createDemoModule(char *fileName)
{
  auto Context(std::make_unique<LLVMContext>());
  auto Err(std::make_unique<SMDiagnostic>());
  auto M = parseIRFile(fileName, *Err, *Context);

  return ThreadSafeModule(std::move(M), std::move(Context));
}

int opt = 0;

inline void parse_opt(int argc, char **argv, ResourceTrackerSP *RT)
{
  while ((opt = getopt(argc, argv, "f:")) != -1)
  {
    switch (opt)
    {
    case 'f':
    {
      if (access(optarg, 0) == -1)
      {
        printf("Given input file invalid\n");
        exit(-1);
      }
      else
      {
        std::string a = optarg;
        if (a.substr(a.find_last_of('.'), a.length() - a.find_last_of('.')) == ".ll")
        {
          auto M = createDemoModule((char *)(optarg));
          ExitOnErr(TheJIT->addModule(std::move(M), *RT));
          printf("Module %s has been add to JIT\n", optarg);
        }
        else
        {
          printf("%s: file format must be .ll\n", optarg);
          exit(-1);
        }
      }
      break;
    }
    case '?':
      if (optopt == 'f')
        fprintf(stderr, "Option -%c requires an argument.\n", optopt);
      else if (isprint(optopt))
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      else
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      printf("option usage error\n");
      exit(-1);
      break;
    }
  }
}

int main(int argc, char **argv)
{
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  TheJIT = ExitOnErr(OrcJIT::Create());
  InitializeModule();
  auto RT = TheJIT->getMainJITDylib().createResourceTracker();
  parse_opt(argc, argv, &RT);
  // Look up the JIT'd function, cast it to a function pointer, then call it.

  auto main_sym = ExitOnErr(TheJIT->lookup("main"));
  int (*main_f)() = (int (*)())main_sym.getAddress();

  int Result = main_f();

  outs() << "main_f() = " << Result << "\n";
  ExitOnErr(RT->remove());
  return 0;
}