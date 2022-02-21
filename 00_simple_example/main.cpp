/*
# Guidance
## Compile mul.cpp
### mul.cpp -> main.ll
clang++ -emit-llvm -S mul.cpp -o mul.ll
## Compile main.cpp
clang++ -g main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit support nativecodegen` -O3 -o main.cpp.o

### Note
For compilation on your pc, maybe you need to change symbol name ("_Z3mulii") in main.cpp according to your emit result
*/

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;
using namespace llvm::orc;

ExitOnError ExitOnErr;

ThreadSafeModule createDemoModule()
{
  auto Context(std::make_unique<LLVMContext>());
  auto Err(std::make_unique<SMDiagnostic>());
  auto M = parseIRFile("mul.ll", *Err, *Context);

  return ThreadSafeModule(std::move(M), std::move(Context));
}

int main(int argc, char *argv[])
{
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  cl::ParseCommandLineOptions(argc, argv, "00 simple example");
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // Create an LLJIT instance.
  // auto J = ExitOnErr(LLLazyJITBuilder().create());
  auto J = ExitOnErr(LLLazyJITBuilder().setNumCompileThreads(2).create());
  auto M = createDemoModule();

  ExitOnErr(J->addIRModule(std::move(M)));

  // Look up the JIT'd function, cast it to a function pointer, then call it.
  auto main_mul_sym = ExitOnErr(J->lookup("_Z3mulii"));
  int (*main_mul)(int, int) = (int (*)(int, int))main_mul_sym.getAddress();

  int Result = main_mul(3, 7);

  outs() << "main_mul(3,7) = " << Result << "\n";
  return 0;
}