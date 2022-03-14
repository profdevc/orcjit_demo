/*
Ref: https://releases.llvm.org/13.0.0/docs/tutorial/index.html#building-a-jit-in-llvm
Usage:
  build:
    mkdir build
    cd build
    cmake ..
    make
  execute:
    ./OrcJIT file1.cpp file2.cpp dir3/ dir4/ --cl-emit-flags clang++_emit-llvm_flags1 --args args1 args2 --num-threads NumThreads
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
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>

using namespace llvm;
using namespace llvm::orc;

static cl::list<std::string> InputFiles(cl::Positional, cl::OneOrMore,
                                        cl::desc("input files"));

static cl::list<std::string> InputArgv("args", cl::Positional,
                                       cl::desc("<program arguments>..."),
                                       cl::ZeroOrMore, cl::PositionalEatsArgs);

static cl::list<std::string> ClEmitFlags("cl-emit-flags", cl::Positional,
                                       cl::desc("<clang emit llvm flags>..."),
                                       cl::ZeroOrMore, cl::PositionalEatsArgs);

static cl::opt<unsigned> NumThreads("num-threads", cl::Optional,
                                    cl::desc("Number of compile threads"),
                                    cl::init(2));

static std::unique_ptr<OrcJIT> TheJIT;
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static ExitOnError ExitOnErr;

std::string Progn = "";

pthread_mutex_t mutex_x=PTHREAD_MUTEX_INITIALIZER;

std::vector<std::string> InputFileArr;
std::set<std::string> fn;

void* isleep(void* fn){
  while(1){
    sleep(1);
    write(STDOUT_FILENO,".",sizeof("."));
  }
}

void* AddModule(void* fn){
  std::string InputFile = (char*)fn;
  SMDiagnostic Err;
  auto Ctx = std::make_unique<LLVMContext>();
  std::string InputFilell = InputFile;
  if (InputFile.substr(InputFile.find_last_of('.'), InputFile.length() - InputFile.find_last_of('.')) == ".cpp")
  {
    InputFilell += ".ll";
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
    while(!access(InputFilell.c_str(),F_OK));
    std::string cmd = "clang++ -S ";
    for (const auto& ClEmitFlag : ClEmitFlags)
      cmd += ClEmitFlag + " ";
    cmd += "-emit-llvm " + InputFile + " -O3 -o " + InputFilell;
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
  else if (InputFile.substr(InputFile.find_last_of('.'), InputFile.length() - InputFile.find_last_of('.')) != ".ll")
  {
    printf("%s: file format must be .cpp\n", InputFile.c_str());
    exit(-1);
  }

  auto M = parseIRFile(InputFilell, Err, *Ctx);
  if (!M)
  {
    Err.print(Progn.c_str(), errs());
    exit(-1);
  }

  ExitOnErr(TheJIT->addModule(ThreadSafeModule(std::move(M), std::move(Ctx))));
  printf("Module %s has been add to JIT\n", InputFilell.c_str());
  return nullptr;
}

std::string fileForInput(std::string filename, std::string InputFile=""){
  std::string ffname=filename.substr(0, filename.find_first_of('.'));
  std::string suffix=filename.substr(filename.find_first_of('.'), filename.length() - filename.find_first_of('.'));
  std::string filenamell = ffname + ".cpp.ll";
  std::string filenamecpp=ffname+".cpp";
  if(suffix == ".cpp" && !fn.count(ffname)){
    fn.insert(ffname);
    if(!access((InputFile+filenamell).c_str(),R_OK)){
      FILE * fp, *fpll;
      int fd,fdll;
      struct stat buf, bufll;
      fp=fopen((InputFile+filename).c_str(),"r");
      fpll=fopen((InputFile+filenamell).c_str(),"r");
      fd=fileno(fp);
      fdll=fileno(fpll);
      fstat(fd, &buf);
      fstat(fdll, &bufll);
      if(bufll.st_mtime>buf.st_mtime)
        return InputFile + filenamell;
      else
        return InputFile+filename;
    }
    else
      return InputFile+filename;
  }
  else if(suffix == ".cpp.ll" && !fn.count(ffname)){
    fn.insert(ffname);
    if(!access((InputFile+filenamecpp).c_str(),R_OK)){
      FILE * fp, *fpcpp;
      int fd,fdcpp;
      struct stat buf, bufcpp;
      fp=fopen((InputFile+filename).c_str(),"r");
      fpcpp=fopen((InputFile+filenamecpp).c_str(),"r");
      fd=fileno(fp);
      fdcpp=fileno(fpcpp);
      fstat(fd, &buf);
      fstat(fdcpp, &bufcpp);
      if(bufcpp.st_mtime>buf.st_mtime)
        return InputFile+filenamecpp;
      else
        return InputFile+filename;
    }
    else
      return InputFile+filename;
  }
  else
    return "";
}

int main(int argc, char **argv)
{
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  TheJIT = ExitOnErr(OrcJIT::Create(NumThreads));
  printf("\n--------   Adding Module  --------\n");
  cl::ParseCommandLineOptions(argc, argv, "OrcJIT");

  Progn = argv[0];

  //add modules to jit
  for (const auto &InputFile : InputFiles)
  {
    struct stat s;
    if(stat(InputFile.c_str(),&s)==0){ //dir
      if(s.st_mode&S_IFDIR){
        DIR *pdir;
        struct dirent* ptr;
        if(!(pdir=opendir(InputFile.c_str()))){
          perror("pdir error");
          exit(-1);
        }
        while((ptr=readdir(pdir))!=nullptr){
          if((!strncmp(ptr->d_name, ".", 1)) || (!strncmp(ptr->d_name, "..", 2)))
            continue;

          std::string filename=ptr->d_name;
          std::string suffix=filename.substr(filename.find_first_of('.'), filename.length() - filename.find_first_of('.'));
          if(suffix == ".cpp" || suffix == ".cpp.ll"){
            std::string ret = fileForInput(filename, InputFile);
            if(!ret.empty())
              InputFileArr.push_back(ret);
          }
        }
      }else if(s.st_mode&S_IFREG){  //file
        std::string path=InputFile.substr(0, InputFile.find_last_of('/'));
        std::string filename=InputFile.substr(InputFile.find_last_of('/'), InputFile.length() - InputFile.find_last_of('/'));
        std::string suffix=filename.substr(filename.find_first_of('.'), filename.length() - filename.find_first_of('.'));
        if(suffix == ".cpp" || suffix == ".cpp.ll"){
          std::string ret = fileForInput(filename, path);
          if(!ret.empty())
            InputFileArr.push_back(ret);
        }
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

  auto pthread_p = std::make_unique<pthread_t[]>(InputFileArr.size());
  for (int i = 0; i < InputFileArr.size();i++){
    int f = pthread_create(&pthread_p[i], nullptr, AddModule,(void*)(InputFileArr[i].c_str()));
    if (f) {
      printf("pthread create error with pthread_create return %d\n",f);
      exit(-1);
    }
  }
  for (int i = 0; i < InputFileArr.size();i++)
    pthread_join(pthread_p[i], nullptr);
  printf("All threads have been done\n");

  // Look up the JIT'd function, cast it to main function pointer, then call it.
  pthread_t ifMain;
  int ifMainRet=pthread_create(&ifMain,nullptr,isleep,nullptr);
  if (ifMainRet) {
    printf("pthread create error with pthread_create return %d\n",ifMainRet);
    exit(-1);
  }
  printf("\n-- Finding symbols & compiling ---\n");
  auto MainSym = ExitOnErr(TheJIT->lookup("main"));
  auto Main =
      jitTargetAddressToFunction<int (*)(int, char *[])>(MainSym.getAddress());
  if(pthread_cancel(ifMain)){
    printf("pthread cancel error\n");
    exit(-1);
  }

  printf("\n----  Executing Module Main  -----\n");
  if(!runAsMain(Main, InputArgv, StringRef(InputFiles.front())))
  printf("\n---------  End Execution  --------\n\n");
  return 0;
}