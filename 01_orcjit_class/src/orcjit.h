/* Ref: https://github.com/llvm/llvm-project/blob/main/llvm/examples/Kaleidoscope/BuildingAJIT/ */

#ifndef ORCJIT_ORCJIT_H
#define ORCJIT_ORCJIT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/EPCIndirectionUtils.h"
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
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ThreadPool.h"

#include <list>
#include <string>

#include <memory>

namespace llvm
{
    ExitOnError ExitOnErre;
    namespace orc
    {
        class OrcJIT
        {
        public:
            OrcJIT(std::unique_ptr<ExecutionSession> ES,
                   std::unique_ptr<EPCIndirectionUtils> EPCIU,
                   JITTargetMachineBuilder JTMB, DataLayout DL,std::unique_ptr<DynamicLibrarySearchGenerator> ProcessSymbolsGenerator)
                : ES(std::move(ES)), EPCIU(std::move(EPCIU)), DL(std::move(DL)),
                  Mangle(*this->ES, this->DL),
                  ObjectLayer(*this->ES,
                              []()
                              { return std::make_unique<SectionMemoryManager>(); }),
                  CompileLayer(*this->ES, ObjectLayer,
                               std::make_unique<ConcurrentIRCompiler>(std::move(JTMB))),
                  MainJD(this->ES->createBareJITDylib("<main>")),
                  OptimizeLayer(*this->ES, CompileLayer, optimizeModule),
                  CODLayer(*this->ES, OptimizeLayer,
                           this->EPCIU->getLazyCallThroughManager(),
                           [this]
                           { return this->EPCIU->createIndirectStubsManager(); }){
                               MainJD.addGenerator(std::move(ProcessSymbolsGenerator));
                               this->ES->setDispatchTask(
                                    [this](std::unique_ptr<Task> T) {
                                    CompileThreads.async(
                                        [UnownedT = T.release()]() {
                                            std::unique_ptr<Task> T(UnownedT);
                                            T->run();
                                        });
                                    });
                                    LocalCXXRuntimeOverrides CXXRuntimeoverrides;
                                    ExitOnErre(CXXRuntimeoverrides.enable(MainJD, Mangle));
                           }

            ~OrcJIT()
            {
                if (auto Err = ES->endSession())
                    ES->reportError(std::move(Err));
                if (auto Err = EPCIU->cleanup())
                    ES->reportError(std::move(Err));
                    CompileThreads.wait();
            }

            static Expected<std::unique_ptr<OrcJIT>> Create(unsigned NumThreads)
            {
                auto EPC = SelfExecutorProcessControl::Create();
                if (!EPC)
                    return EPC.takeError();

                auto ES = std::make_unique<ExecutionSession>(std::move(*EPC));

                auto CompileThreads={llvm::hardware_concurrency(NumThreads)};

                auto EPCIU = EPCIndirectionUtils::Create(ES->getExecutorProcessControl());
                if (!EPCIU)
                    return EPCIU.takeError();

                (*EPCIU)->createLazyCallThroughManager(
                    *ES, pointerToJITTargetAddress(&handleLazyCallThroughError));

                if (auto Err = setUpInProcessLCTMReentryViaEPCIU(**EPCIU))
                    return std::move(Err);

                JITTargetMachineBuilder JTMB(
                    ES->getExecutorProcessControl().getTargetTriple());

                auto DL = JTMB.getDefaultDataLayoutForTarget();
                if (!DL)
                    return DL.takeError();

                auto ProcessSymbolsSearchGenerator =
                    DynamicLibrarySearchGenerator::GetForCurrentProcess(
                        DL->getGlobalPrefix());
                if (!ProcessSymbolsSearchGenerator)
                return ProcessSymbolsSearchGenerator.takeError();

                return std::make_unique<OrcJIT>(std::move(ES), std::move(*EPCIU),
                                                std::move(JTMB), std::move(*DL),std::move(*ProcessSymbolsSearchGenerator));
            }

            const DataLayout &getDataLayout() const { return DL; }

            JITDylib &getMainJITDylib() { return MainJD; }

            Error addModule(ThreadSafeModule TSM, ResourceTrackerSP RT = nullptr)
            {
                if (!RT)
                    RT = MainJD.getDefaultResourceTracker();

                return OptimizeLayer.add(RT, std::move(TSM));
            }

            Expected<JITEvaluatedSymbol> lookup(StringRef Name)
            {
                return ES->lookup({&MainJD}, Mangle(Name.str()));
            }

        private:
            std::unique_ptr<ExecutionSession> ES;
            std::unique_ptr<EPCIndirectionUtils> EPCIU;

            ThreadPool CompileThreads;

            DataLayout DL;
            MangleAndInterner Mangle;

            RTDyldObjectLinkingLayer ObjectLayer;
            IRCompileLayer CompileLayer;
            IRTransformLayer OptimizeLayer;
            CompileOnDemandLayer CODLayer;

            JITDylib &MainJD;

            static void handleLazyCallThroughError()
            {
                errs() << "LazyCallThrough error: Could not find function body";
                exit(1);
            }


            static Expected<ThreadSafeModule>
            optimizeModule(ThreadSafeModule TSM, const MaterializationResponsibility &R)
            {
                TSM.withModuleDo([](Module &M)
                    {
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
