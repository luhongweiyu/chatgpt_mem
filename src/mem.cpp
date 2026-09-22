#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "mem.h"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define MEM_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "mem", __VA_ARGS__)
#define MEM_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "mem", __VA_ARGS__)
#else
#define MEM_LOGI(...) do { fprintf(stderr, "[mem] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define MEM_LOGE(...) do { fprintf(stderr, "[mem] "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

namespace {

static bool IsRangePresent(uint64_t address, int32_t pid, size_t size) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/pagemap", pid);

    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    if (size == 0) {
        close(fd);
        return true;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        close(fd);
        return false;
    }

    bool ok = true;
    for (size_t offset = 0; offset < size; offset += static_cast<size_t>(page_size)) {
        const uint64_t page_index = (address + offset) / static_cast<uint64_t>(page_size);
        const off_t file_offset = static_cast<off_t>(page_index * sizeof(uint64_t));

        uint64_t entry = 0;
        const ssize_t n = pread(fd, &entry, sizeof(entry), file_offset);
        if (n != static_cast<ssize_t>(sizeof(entry)) || (entry & (1ULL << 63)) == 0) {
            ok = false;
            break;
        }
    }

    close(fd);
    return ok;
}

static bool ReadMemory(int32_t pid, uint64_t address, void* buffer, size_t size) {
    struct iovec local_iov { buffer, size };
    struct iovec remote_iov { reinterpret_cast<void*>(static_cast<uintptr_t>(address)), size };

    const ssize_t n = static_cast<ssize_t>(syscall(
        SYS_process_vm_readv,
        pid,
        &local_iov,
        1,
        &remote_iov,
        1,
        0));

    if (n == static_cast<ssize_t>(size)) {
        MEM_LOGI("ReadMemory成功: pid=%d, addr=0x%llx, size=%zu",
                 pid, static_cast<unsigned long long>(address), size);
        return true;
    }

    const int err = errno;
    MEM_LOGE("ReadMemory失败: pid=%d, addr=0x%llx, size=%zu, read=%ld, errno=%d (%s)",
             pid,
             static_cast<unsigned long long>(address),
             size,
             static_cast<long>(n),
             err,
             strerror(err));
    return false;
}

static bool WriteMemory(int32_t pid, uint64_t address, const void* buffer, size_t size) {
    struct iovec local_iov { const_cast<void*>(buffer), size };
    struct iovec remote_iov { reinterpret_cast<void*>(static_cast<uintptr_t>(address)), size };

    const ssize_t n = static_cast<ssize_t>(syscall(
        SYS_process_vm_writev,
        pid,
        &local_iov,
        1,
        &remote_iov,
        1,
        0));

    if (n == static_cast<ssize_t>(size)) {
        MEM_LOGI("WriteMemory成功: pid=%d, addr=0x%llx, size=%zu",
                 pid, static_cast<unsigned long long>(address), size);
        return true;
    }

    const int err = errno;
    MEM_LOGE("WriteMemory失败: pid=%d, addr=0x%llx, size=%zu, written=%ld, errno=%d (%s)",
             pid,
             static_cast<unsigned long long>(address),
             size,
             static_cast<long>(n),
             err,
             strerror(err));
    return false;
}

static bool ParsePidName(const dirent* ent, int32_t* pid_out) {
    if (!ent || !pid_out) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const long value = strtol(ent->d_name, &end, 10);
    if (errno != 0 || end == ent->d_name || *end != '\0' || value <= 0 || value > INT32_MAX) {
        return false;
    }

    *pid_out = static_cast<int32_t>(value);
    return true;
}

static bool ReadCmdline(const char* pid_text, char out[256]) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%s/cmdline", pid_text);

    FILE* fp = fopen(path, "r");
    if (!fp) {
        return false;
    }

    memset(out, 0, 256);
    const size_t n = fread(out, 1, 255, fp);
    fclose(fp);
    return n != 0;
}

static int32_t FindPid(const char* exact_name) {
    if (!exact_name) {
        return -1;
    }

    DIR* dir = opendir("/proc");
    if (!dir) {
        return -1;
    }

    int32_t result = -1;
    while (dirent* ent = readdir(dir)) {
        int32_t pid = -1;
        if (!ParsePidName(ent, &pid)) {
            continue;
        }

        char cmdline[256];
        if (!ReadCmdline(ent->d_name, cmdline)) {
            continue;
        }

        if (strcmp(cmdline, exact_name) == 0) {
            result = pid;
            break;
        }
    }

    closedir(dir);
    return result;
}

static MemProcessList* FindProcesses(const char* pattern) {
    if (!pattern) {
        return nullptr;
    }

    DIR* dir = opendir("/proc");
    if (!dir) {
        return nullptr;
    }

    MemProcessEntry* items = nullptr;
    size_t count = 0;
    size_t capacity = 0;

    while (dirent* ent = readdir(dir)) {
        int32_t pid = -1;
        if (!ParsePidName(ent, &pid)) {
            continue;
        }

        char cmdline[256];
        if (!ReadCmdline(ent->d_name, cmdline)) {
            continue;
        }

        if (fnmatch(pattern, cmdline, 0) != 0) {
            continue;
        }

        if (count >= capacity) {
            const size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
            void* p = realloc(items, new_capacity * sizeof(MemProcessEntry));
            if (!p) {
                break;
            }
            items = static_cast<MemProcessEntry*>(p);
            capacity = new_capacity;
        }

        items[count].name = strdup(cmdline);
        items[count].pid = pid;
        items[count].reserved = 0;
        ++count;
    }

    closedir(dir);

    MemProcessList* result = static_cast<MemProcessList*>(malloc(sizeof(MemProcessList)));
    if (!result) {
        if (items) {
            for (size_t i = 0; i < count; ++i) {
                free(items[i].name);
            }
            free(items);
        }
        return nullptr;
    }

    result->count = static_cast<int32_t>(count);
    result->reserved = 0;
    result->items = items;
    return result;
}

static void FreeProcesses(MemProcessList* list) {
    if (!list) {
        return;
    }

    if (list->items) {
        for (int32_t i = 0; i < list->count; ++i) {
            free(list->items[i].name);
        }
        free(list->items);
    }
    free(list);
}

static MemMapList* FindMaps(int32_t pid, const char* module_filter) {
    if (!module_filter) {
        return nullptr;
    }

    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE* fp = fopen(maps_path, "r");
    if (!fp) {
        return nullptr;
    }

    MemMapEntry* items = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    char line[512];

    while (fgets(line, sizeof(line), fp)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char permissions[5] = {};
        char path[256] = {};

        const int parsed = sscanf(
            line,
            "%llx-%llx %4s %*s %*s %*s %255[^\n]",
            &start,
            &end,
            permissions,
            path);

        if (parsed < 3) {
            continue;
        }

        if (strstr(path, module_filter) == nullptr) {
            continue;
        }

        if (count >= capacity) {
            const size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
            void* p = realloc(items, new_capacity * sizeof(MemMapEntry));
            if (!p) {
                break;
            }
            items = static_cast<MemMapEntry*>(p);
            capacity = new_capacity;
        }

        MemMapEntry& entry = items[count++];
        entry.start = static_cast<uint64_t>(start);
        entry.size = static_cast<uint64_t>(end - start);
        entry.permissions = strdup(permissions);
        entry.path = strdup(path);
    }

    fclose(fp);

    MemMapList* result = static_cast<MemMapList*>(malloc(sizeof(MemMapList)));
    if (!result) {
        if (items) {
            for (size_t i = 0; i < count; ++i) {
                free(items[i].permissions);
                free(items[i].path);
            }
            free(items);
        }
        return nullptr;
    }

    result->count = static_cast<int32_t>(count);
    result->reserved = 0;
    result->items = items;
    return result;
}

static void FreeMaps(MemMapList* list) {
    if (!list) {
        return;
    }

    if (list->items) {
        for (int32_t i = 0; i < list->count; ++i) {
            free(list->items[i].permissions);
            free(list->items[i].path);
        }
        free(list->items);
    }
    free(list);
}

static int32_t InjectSoStub(int32_t pid, const char* so_path) {
    (void)pid;
    (void)so_path;
    MEM_LOGE("inject_so ABI slot is intentionally not reconstructed");
    return -1;
}

static MemApi g_mem_api = {
    &IsRangePresent,
    &ReadMemory,
    &WriteMemory,
    &FindPid,
    &FindProcesses,
    &FindMaps,
    &FreeProcesses,
    &FreeMaps,
    &InjectSoStub,
};

} // namespace

extern "C" __attribute__((visibility("default"))) MemApi* get_mem(void) {
    return &g_mem_api;
}
