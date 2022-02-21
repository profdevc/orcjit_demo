#ifndef ORCJIT_ORCJIT_H
#define ORCJIT_ORCJIT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <memory>

namespace llvm {
namespace orc {

class OrcJIT {
private:
  std::unique_ptr<ExecutionSession> ES;

  DataLayout DL;
  MangleAndInterner Mangle;

  RTDyldObjectLinkingLayer ObjectLayer;
  IRCompileLayer CompileLayer;
  IRTransformLayer OptimizeLayer;

  JITDylib &MainJD;

public:
  OrcJIT(std::unique_ptr<ExecutionSession> ES,
                  JITTargetMachineBuilder JTMB, DataLayout DL)
      : ES(std::move(ES)), DL(std::move(DL)), Mangle(*this->ES, this->DL),
        ObjectLayer(*this->ES,
                    []() { return std::make_unique<SectionMemoryManager>(); }),
        CompileLayer(*this->ES, ObjectLayer,
                     std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
        OptimizeLayer(*this->ES, CompileLayer, optimizeModule),
        MainJD(this->ES->createBareJITDylib("<main>")) {
    MainJD.addGenerator(
        cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
            DL.getGlobalPrefix())));
  }

  Error setupJITDylib(JITDylib &JD);

  ~OrcJIT() {
    if (auto Err = ES->endSession())
      ES->reportError(std::move(Err));
  }

  /*Error setupJITDylib(JITDylib &JD) {

    // Add per-jitdylib standard interposes.
    SymbolMap PerJDInterposes;
    PerJDInterposes[J.mangleAndIntern("__lljit.run_atexits_helper")] =
        JITEvaluatedSymbol(pointerToJITTargetAddress(runAtExitsHelper),
                           JITSymbolFlags());
    cantFail(JD.define(absoluteSymbols(std::move(PerJDInterposes))));

    auto Ctx = std::make_unique<LLVMContext>();
    auto M = std::make_unique<Module>("__standard_lib", *Ctx);
    M->setDataLayout(J.getDataLayout());

    auto *Int64Ty = Type::getInt64Ty(*Ctx);
    auto *DSOHandle = new GlobalVariable(
        *M, Int64Ty, true, GlobalValue::ExternalLinkage,
        ConstantInt::get(Int64Ty, reinterpret_cast<uintptr_t>(&JD)),
        "__dso_handle");
    DSOHandle->setVisibility(GlobalValue::DefaultVisibility);
    DSOHandle->setInitializer(
        ConstantInt::get(Int64Ty, pointerToJITTargetAddress(&JD)));

    auto *GenericIRPlatformSupportTy =
        StructType::create(*Ctx, "lljit.GenericLLJITIRPlatformSupport");

    auto *PlatformInstanceDecl = new GlobalVariable(
        *M, GenericIRPlatformSupportTy, true, GlobalValue::ExternalLinkage,
        nullptr, "__lljit.platform_support_instance");

    auto *VoidTy = Type::getVoidTy(*Ctx);
    addHelperAndWrapper(
        *M, "__lljit_run_atexits", FunctionType::get(VoidTy, {}, false),
        GlobalValue::HiddenVisibility, "__lljit.run_atexits_helper",
        {PlatformInstanceDecl, DSOHandle});

    return J.addIRModule(JD, ThreadSafeModule(std::move(M), std::move(Ctx)));
  }*/


  static Expected<std::unique_ptr<OrcJIT>> Create() {
    auto EPC = SelfExecutorProcessControl::Create();
    if (!EPC)
      return EPC.takeError();

    auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

    JITTargetMachineBuilder JTMB(
        ES->getExecutorProcessControl().getTargetTriple());

    auto DL = JTMB.getDefaultDataLayoutForTarget();
    if (!DL)
      return DL.takeError();

    return std::make_unique<OrcJIT>(std::move(ES), std::move(JTMB),
                                             std::move(*DL));
  }

  const DataLayout &getDataLayout() const { return DL; }

  /*ExecutionSession &getExecutionSession() { return *ES; }*/

  JITDylib &getMainJITDylib() { return MainJD; }

  Error addModule(ThreadSafeModule TSM, ResourceTrackerSP RT = nullptr) {
    if (!RT)
      RT = MainJD.getDefaultResourceTracker();

    return OptimizeLayer.add(RT, std::move(TSM));
  }

  Expected<JITEvaluatedSymbol> lookup(StringRef Name) {
    return ES->lookup({&MainJD}, Mangle(Name.str()));
  }

private:
  static Expected<ThreadSafeModule>
  optimizeModule(ThreadSafeModule TSM, const MaterializationResponsibility &R) {
    TSM.withModuleDo([](Module &M) {
      // Create a function pass manager.
      auto FPM = std::make_unique<legacy::FunctionPassManager>(&M);

      // Add some optimizations.
      FPM->add(createInstructionCombiningPass());
      FPM->add(createReassociatePass());
      FPM->add(createGVNPass());
      FPM->add(createCFGSimplificationPass());
      FPM->doInitialization();

      // Run the optimizations over all functions in the module being added to
      // the JIT.
      for (auto &F : M)
        FPM->run(F);
    });

    return std::move(TSM);
  }
};

} // end namespace orc
} // end namespace llvm

#endif // ORCJIT_ORCJIT_H
