cmake_minimum_required(VERSION 3.16)
project(hydrolox VERSION 0.1.0 LANGUAGES CXX C)

# ----------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(elpc
    GIT_REPOSITORY https://github.com/TheMasterofWorlds99/elpc.git
    GIT_TAG        main
)

FetchContent_MakeAvailable(elpc)

# ----------------------------------------------------------------
# LLVM — required for Hydrolox
# ----------------------------------------------------------------

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

target_link_libraries(elpc INTERFACE LLVM)
target_compile_definitions(elpc INTERFACE ELPC_ENABLE_LLVM)

# ----------------------------------------------------------------
# Compiler executable
# ----------------------------------------------------------------

add_executable(hydrolox src/main.cpp src/types.cpp)

target_include_directories(hydrolox
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(hydrolox
    PRIVATE
        ${LLVM_DEFINITIONS}
        ELPC_ENABLE_LLVM
)

target_compile_features(hydrolox PRIVATE cxx_std_23)

target_link_libraries(hydrolox
    PRIVATE
        elpc::elpc
        ${LLVM_LIBS}
)
