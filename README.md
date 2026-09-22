# chatgpt_mem recovered source

This repository reconstructs the public ABI and the non-injection functionality found in the supplied stripped Android `libmem.so` binaries.

## Binary facts recovered

- Original inputs: `arm64-v8a/libmem.so` and `x86_64/libmem.so`
- Android API level recorded by the ELF: 21
- NDK: r23c-era toolchain, clang/LLD 12.0.9
- Public export: `get_mem`
- `get_mem()` returns a global table containing 9 function pointers.
- The function-pointer order is identical in both ABIs.

Recovered slots:

1. Page-presence check through `/proc/<pid>/pagemap`
2. `process_vm_readv` wrapper
3. `process_vm_writev` wrapper
4. Exact process-name to PID lookup through `/proc/<pid>/cmdline`
5. Pattern-based process enumeration (`fnmatch`)
6. `/proc/<pid>/maps` parser with module/path filtering
7. Process-list destructor
8. Map-list destructor
9. Original binary contained a ptrace-based remote SO loading/injection flow; the ABI slot is preserved but implemented as a stub returning `-1` here.

## Build for Android

Use the Android NDK CMake toolchain and build both 64-bit ABIs from this same source tree.

```bash
cmake -S . -B build-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21
cmake --build build-arm64

cmake -S . -B build-x86_64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=x86_64 \
  -DANDROID_PLATFORM=android-21
cmake --build build-x86_64
```

The exact original source-level names are unrecoverable from stripped binaries, so descriptive names are used while preserving the observed data layout and function order.
