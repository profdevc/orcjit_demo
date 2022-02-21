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

int main()
{
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  TheJIT = ExitOnErr(OrcJIT::Create());
  InitializeModule();
  auto RT = TheJIT->getMainJITDylib().createResourceTracker();
  auto M1 = createDemoModule((char *)("mul_func.ll"));
  ExitOnErr(TheJIT->addModule(std::move(M1), RT));
  auto M = createDemoModule((char *)("mul_main.ll"));
  ExitOnErr(TheJIT->addModule(std::move(M), RT));

  // Look up the JIT'd function, cast it to a function pointer, then call it.

  auto main_sym = ExitOnErr(TheJIT->lookup("main"));
  int (*main_f)() = (int (*)())main_sym.getAddress();

  int Result = main_f();

  outs() << "main_f() = " << Result << "\n";
  ExitOnErr(RT->remove());
  return 0;
}