#pragma once

#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MemProcessEntry {
    char* name;
    int32_t pid;
    int32_t reserved;
} MemProcessEntry;

typedef struct MemProcessList {
    int32_t count;
    int32_t reserved;
    MemProcessEntry* items;
} MemProcessList;

typedef struct MemMapEntry {
    uint64_t start;
    uint64_t size;
    char* permissions;
    char* path;
} MemMapEntry;

typedef struct MemMapList {
    int32_t count;
    int32_t reserved;
    MemMapEntry* items;
} MemMapList;

/*
 * Recovered 64-bit ABI table returned by get_mem().
 * Function order matches the original arm64-v8a/x86_64 binaries.
 */
typedef struct MemApi {
    /* Checks whether every page in [address, address + size) is present in /proc/<pid>/pagemap. */
    bool (*is_range_present)(uint64_t address, int32_t pid, size_t size);

    /* Cross-process memory access via process_vm_readv/process_vm_writev. */
    bool (*read_memory)(int32_t pid, uint64_t address, void* buffer, size_t size);
    bool (*write_memory)(int32_t pid, uint64_t address, const void* buffer, size_t size);

    /* Process lookup helpers based on /proc/<pid>/cmdline. */
    int32_t (*find_pid)(const char* exact_name);
    MemProcessList* (*find_processes)(const char* pattern);

    /* Parse /proc/<pid>/maps; only entries whose path contains module_filter are returned. */
    MemMapList* (*find_maps)(int32_t pid, const char* module_filter);

    /* Paired destructors for the two heap-owned list types. */
    void (*free_processes)(MemProcessList* list);
    void (*free_maps)(MemMapList* list);

    /* ABI slot recovered from the binary. The original performed remote SO injection. */
    int32_t (*inject_so)(int32_t pid, const char* so_path);
} MemApi;

MemApi* get_mem(void);

#ifdef __cplusplus
}
#endif
