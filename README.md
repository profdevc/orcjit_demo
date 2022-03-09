# orcjit_demo
There are a series example usages about llvm orcjit 
## Requirement
LLVM version >=13.0.0
## Reference
### [Kaleidoscope and JIT](https://releases.llvm.org/13.0.0/docs/tutorial/index.html#building-a-jit-in-llvm)
### [OrcV2 Doc](https://llvm.org/docs/ORCv2.html)
## Note
If there are syscall symbols not found, please check whether the glibc version support. see also #13