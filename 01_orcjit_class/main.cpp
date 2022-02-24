/*
Ref: https://releases.llvm.org/13.0.0/docs/tutorial/index.html#building-a-jit-in-llvm
Usage:
  Compile
    clang++ -g main.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit support nativecodegen` -O3 -o main.cpp.o
  execute
    ./main.cpp.o -f file1.cpp file2.cpp -t para1 para2
    note:
      1. file order maybe affects whether modules can be added and executed correctly. 
        You'd better put the referenced file first
        There must be at least one file.cpp has main() function after -f
      2. use -p to pass paras to main() function in filex.cpp if it need them. -p is not necessary.
*/
#include "llvm/ExecutionEngine/JITLink/EHFrameSupport.h"
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

std::set<std::string> cmd_para{"-f", "-p"};

int opt = 0;
char **_argv;
char *main_file_name;

inline void parse_opt(int argc, char **argv, int &_argc, ResourceTrackerSP &RT)
{
  while ((opt = getopt(argc, argv, "f:p:")) != -1)
  {
    switch (opt)
    {
    case 'f':
    {
      int currind = optind - 1;
      int file_count = 0;
      while (argv[currind + file_count] && !cmd_para.count(argv[currind + file_count]))
        file_count++;
      if (!file_count)
      {
        printf("No input file. Please give input file by -f\n");
        exit(-1);
      }
      for (int i = 0; i < file_count; i++)
      {
        if (access(argv[currind + i], 0) == -1)
        {
          printf("Given input file: %s is invalid\n", argv[currind + i]);
          exit(-1);
        }
        else
        {
          std::string a = argv[currind + i];
          if (a.substr(a.find_last_of('.'), a.length() - a.find_last_of('.')) == ".cpp")
          {
            std::string al = a.substr(0, a.find_last_of('.')) + ".ll";
            std::string cmd = "clang++ -S -emit-llvm " + a + " -o " + al;
            FILE *cmdp = NULL;
            cmdp = popen(cmd.c_str(), "r");
            if (!cmdp)
            {
              perror("popen");
              exit(EXIT_FAILURE);
            }
            else
              printf("clang++: emit %s to %s\n", a.c_str(), al.c_str());
            auto M = createDemoModule((char *)(al.c_str()));
            ExitOnErr(TheJIT->addModule(std::move(M), RT));
            printf("Module %s has been add to JIT\n", al.c_str());
            if (bool(TheJIT->lookup("main")))
              main_file_name = argv[currind + i];
          }
          else
          {
            printf("%s: file format must be .cpp\n", argv[currind + i]);
            exit(-1);
          }
        }
      }
      break;
    }
    case 'p':
    {
      int currind = optind - 1;
      while (argv[currind + _argc] && !cmd_para.count(argv[currind + _argc]))
        _argc++;
      if (_argc > 0)
      {
        _argv = (char **)malloc((_argc + 1) * sizeof(char *));
        _argv[0] = (char *)malloc(strlen(main_file_name) * sizeof(char));
        strcpy(_argv[0], main_file_name);
        for (int i = 0; i < _argc; i++)
        {
          _argv[i + 1] = (char *)malloc(strlen(argv[currind + i]) * sizeof(char));
          strcpy(_argv[i + 1], argv[currind + i]);
        }
        printf("\nArgs: ");
        for (int i = 1; i <= _argc; i++)
          printf("%s ", _argv[i]);
        printf("has been passed to main() in %s\n", main_file_name);
      }
      else
        printf("No agrs need to be passed to main() in %s\n", main_file_name);
      break;
    }
    case '?':
      if (optopt == 'f' || optopt == 'p')
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
  int _argc = 0;
  printf("\n--------   Adding Module  --------\n");
  parse_opt(argc, argv, _argc, RT);
  // Look up the JIT'd function, cast it to a function pointer, then call it.

  auto main_sym = ExitOnErr(TheJIT->lookup("main"));
  int (*main_f)(int, char **) = (int (*)(int, char **))main_sym.getAddress();

  printf("\n-------- Executing Module --------\n");
  int Result = main_f(_argc, _argv);

  // Return value of function
  // outs() << "main_f() = " << Result << "\n";

  ExitOnErr(RT->remove());

  if (_argc > 0)
  {
    for (int i = 0; i < _argc + 1; i++)
      free((void *)_argv[i]);
    free((void *)_argv);
  }
  printf("\n");
  return 0;
}