# Ref: https://stackoverflow.com/questions/52931852/how-to-convert-llvm-clang-command-line-to-cmake-config
# Ref: https://github.com/LoranceChen/simple-lang/blob/master/CMakeLists.txt
cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 14)
SET(CMAKE_C_COMPILER /usr/local/bin/clang)
SET(CMAKE_CXX_COMPILER /usr/local/bin/clang++)

# if (NOT LLVM_ENABLE_RTTI)
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
# endif()

PROJECT(OrcJIT)

find_package(LLVM REQUIRED CONFIG)


message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "CMAKE_ROOT ${CMAKE_ROOT}")
message(STATUS "CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}")
message(STATUS "LLVM_FOUND ${LLVM_FOUND}")
message(STATUS "LLVM_DIR ${LLVM_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM_DEFINITIONS: ${LLVM_DEFINITIONS}")

message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++11 support
# for your compiler.

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if (NOT LLVM_ENABLE_RTTI)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# Now build our tools
add_executable(${PROJECT_NAME} src/main.cpp src/orcjit.h)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
    set(Arch_asmparser X86AsmParser)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(Arch_asmparser AArch64AsmParser)
else()
    # https://cmake.org/cmake/help/latest/variable/CMAKE_HOST_SYSTEM_PROCESSOR.html
    message("Unsupport architecture.You can modify CMakeLists Line 46 according to your architecture") 
endif()

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(
    llvm_libs
    Core
    nativecodegen
    OrcJIT
    Support
    ${Arch_asmparser}
)

# Link against LLVM libraries
target_link_libraries(${PROJECT_NAME} pthread ${llvm_libs})