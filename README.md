# Hydrolox

A compiled, statically typed language designed to surpass C and C++ in raw speed when
it comes to scientific and graphical computing (Graphical since it can talk to the GPU directly)

---

### Hydrolox Example

```hydrolox
func i32 start() {
  x: i32 = 5;
  y: i32 = 12;
  z: i32 = x + y;
  return z;
}
```

---

## Current Features

- I32, U8, U16, String/Str and Bool types
- Arrays with indexing
- Vector Types
- For/while loops with ++/-- increments
- Full boolean and comparison operators
- LLVM O2 optimization pipeline
- Somewhat functional CLI
- Extern function capabilities

## Planned Features

- Adding more CLI functionality
- More complex data types
- Modules
- Easy parallelization
- idk, other stuff :P

## Dependencies

- LLVM
- elpc (compiler library)
- CMake

## Build

cmake -B build -DELPC_ENABLE_LLVM=ON
cmake --build build
./build/hydrolox test/main.hlox
