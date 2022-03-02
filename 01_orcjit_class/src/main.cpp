/*
Ref: https://releases.llvm.org/13.0.0/docs/tutorial/index.html#building-a-jit-in-llvm
Usage:
  build:
    mkdir build
    cd build
    cmake ..
    make
  execute:
    ./OrcJIT file1.cpp file2.cpp dir3/ dir4/ --args args1 args2 --num-threads NumThreads
    (support file and (all .cpp and .ll in ) directionary)
*/
#include "llvm/ExecutionEngine/JITLink/EHFrameSupport.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"

#include "orcjit.h"
#include <unistd.h>
#include <iostream>
#include <dirent.h>
#include <sys/stat.h>
#include <set>

using namespace llvm;
using namespace llvm::orc;

static cl::list<std::string> InputFiles(cl::Positional, cl::OneOrMore,
                                        cl::desc("input files"));

static cl::list<std::string> InputArgv("args", cl::Positional,
                                       cl::desc("<program arguments>..."),
                                       cl::ZeroOrMore, cl::PositionalEatsArgs);

static cl::opt<unsigned> NumThreads("num-threads", cl::Optional,
                                    cl::desc("Number of compile threads"),
                                    cl::init(2));

static std::unique_ptr<OrcJIT> TheJIT;
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static ExitOnError ExitOnErr;

void AddModule(std::string InputFile, char **argv){
  SMDiagnostic Err;
    auto Ctx = std::make_unique<LLVMContext>();
    std::string InputFilell = InputFile;
    if (InputFile.substr(InputFile.find_last_of('.'), InputFile.length() - InputFile.find_last_of('.')) == ".cpp")
    {
      InputFilell = InputFile + ".ll";
      FILE *cmdp = NULL;
      if(!access(InputFilell.c_str(),F_OK)){
        std::string rmcmd = "rm " + InputFilell;
        printf("%s\n",rmcmd.c_str());
        cmdp = popen(rmcmd.c_str(), "r");
        if (!cmdp)
        {
          perror("popen");
          exit(EXIT_FAILURE);
        }
      }
      std::string cmd = "clang++ -S -emit-llvm " + InputFile + " -o " + InputFilell;
      while(!access(InputFilell.c_str(),F_OK));
      cmdp = NULL;
      cmdp = popen(cmd.c_str(), "r");
      if (!cmdp)
      {
        perror("popen");
        exit(EXIT_FAILURE);
      }
      else
        printf("clang++: emit %s to %s\n", InputFile.c_str(), InputFilell.c_str());
      while(access(InputFilell.c_str(),F_OK));
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
      exit(-1);
    }

    ExitOnErr(TheJIT->addModule(ThreadSafeModule(std::move(M), std::move(Ctx))));
    printf("Module %s has been add to JIT\n", InputFilell.c_str());
}

int main(int argc, char **argv)
{
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  TheJIT = ExitOnErr(OrcJIT::Create(NumThreads));
  printf("\n--------   Adding Module  --------\n");
  cl::ParseCommandLineOptions(argc, argv, "OrcJIT");

  //add modules to jit
  for (const auto &InputFile : InputFiles)
  {
    struct stat s;
    if(stat(InputFile.c_str(),&s)==0){
      if(s.st_mode&S_IFDIR){
        DIR *pdir;
        struct dirent* ptr;
        if(!(pdir=opendir(InputFile.c_str()))){
          perror("pdir error");
          exit(-1);
        }
        std::set<std::string> fn;
        while((ptr=readdir(pdir))!=nullptr){
          if((!strncmp(ptr->d_name, ".", 1)) || (!strncmp(ptr->d_name, "..", 2)))
            continue;

          std::string filename=ptr->d_name;
          std::string suffix=filename.substr(filename.find_last_of('.'), filename.length() - filename.find_last_of('.'));
          if(suffix == ".cpp"){
            fn.insert(filename);
            AddModule(InputFile + filename, argv);
          }
        }
        while((ptr=readdir(pdir))!=nullptr){
          if((!strncmp(ptr->d_name, ".", 1)) || (!strncmp(ptr->d_name, "..", 2)))
            continue;

          std::string filename=ptr->d_name;
          std::string suffix=filename.substr(filename.find_last_of('.'), filename.length() - filename.find_last_of('.'));
          if(suffix == ".ll" && !fn.count(filename.substr(0, filename.length() - 3)))
            AddModule(InputFile + filename, argv);
        }
      }else if(s.st_mode&S_IFREG){  //file
        AddModule(InputFile.c_str(), argv);
      }else{
        printf("stat: '%s' is neither a directionary nor a file, what is it???\n",InputFile.c_str());
        exit(-1);
      }
    }
    else{
      printf("%s: Invalid file name/directionary\n",InputFile.c_str());
      exit(-1);
    }
    
  }

  // Look up the JIT'd function, cast it to main function pointer, then call it.
  auto MainSym = ExitOnErr(TheJIT->lookup("main"));
  auto Main =
      jitTargetAddressToFunction<int (*)(int, char *[])>(MainSym.getAddress());

  printf("\n----  Executing Module Main  -----\n");
  if(!runAsMain(Main, InputArgv, StringRef(InputFiles.front())))
  printf("\n---------  End Execution  --------\n\n");
  return 0;
}