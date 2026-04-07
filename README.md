# Hydrolox
A compiled, statically typed language designed to surpass C and C++ in raw speed when 
it comes to scientific and graphical computing (Graphical since it can talk to the GPU directly)

--- 

### Hydrolox Example
```hydrolox
func i32 start() {
  x: i32 = 5;
  return x;
}
```

---

## Current Features
- i32 and bool types
- Arrays with indexing
- For/while loops with ++/-- increments
- Full boolean and comparison operators
- LLVM O2 optimization pipeline

## Planned Features
- Fat Pointers (Strings)
- More complex data types
- Modules
- Easy parallelization
- idk, other stuff

## Dependencies
- LLVM
- elpc (compiler library)
- CMake

## Build
cmake -B build -DELPC_ENABLE_LLVM=ON
cmake --build build
./build/hydrolox test/main.hlox
