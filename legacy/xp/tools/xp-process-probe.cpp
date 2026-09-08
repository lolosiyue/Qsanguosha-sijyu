// Read-only Windows XP SP3 x86 process diagnostics. No Qt or remote execution.
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define PSAPI_VERSION 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

typedef unsigned __int64 U64;

static std::string quote(const wchar_t *text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    std::vector<char> bytes(length > 0 ? length : 1, 0);
    if (length > 0) WideCharToMultiByte(CP_UTF8, 0, text, -1, &bytes[0], length, NULL, NULL);
    std::string result("\"");
    for (size_t i = 0; i + 1 < bytes.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(bytes[i]);
        if (ch == '"' || ch == '\\') { result += '\\'; result += ch; }
        else if (ch < 0x20) {
            char escape[7]; std::sprintf(escape, "\\u%04x", ch); result += escape;
        } else result += ch;
    }
    return result + '"';
}

static std::string utc()
{
    SYSTEMTIME time; GetSystemTime(&time);
    char buffer[40];
    std::sprintf(buffer, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}

static void error(const char *operation, DWORD code, DWORD pid = 0)
{
    std::printf("{\"type\":\"error\",\"utc\":\"%s\",\"pid\":%lu,\"operation\":\"%s\",\"win32_error\":%lu}\n",
        utc().c_str(), pid, operation, code);
    std::fflush(stdout);
}

static std::string imagePath(HANDLE process, HMODULE module, DWORD *failure)
{
    // XP process paths are normally MAX_PATH; retain room for extended paths.
    std::vector<wchar_t> path(32768, 0);
    const DWORD count = GetModuleFileNameExW(process, module, &path[0], DWORD(path.size()));
    if (!count || count >= path.size()) {
        *failure = count ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        return "null";
    }
    *failure = 0;
    return quote(&path[0]);
}

static void modules(HANDLE process, DWORD pid)
{
    std::vector<HMODULE> list(128);
    DWORD needed = 0;
    // The loader may change during enumeration. Bound retries and record failure.
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (!EnumProcessModules(process, &list[0], DWORD(list.size() * sizeof(HMODULE)), &needed)) {
            error("EnumProcessModules", GetLastError(), pid); return;
        }
        if (needed <= list.size() * sizeof(HMODULE)) {
            for (size_t i = 0; i < needed / sizeof(HMODULE); ++i) {
                DWORD failure = 0;
                const std::string path = imagePath(process, list[i], &failure);
                std::printf("{\"type\":\"module\",\"utc\":\"%s\",\"pid\":%lu,\"base\":%I64u,\"path\":%s,\"win32_error\":%lu}\n",
                    utc().c_str(), pid, U64(reinterpret_cast<ULONG_PTR>(list[i])), path.c_str(), failure);
            }
            return;
        }
        list.resize(needed / sizeof(HMODULE) + 32);
    }
    error("EnumProcessModules_unstable", ERROR_RETRY, pid);
}

static void virtualMemory(HANDLE process)
{
    SYSTEM_INFO info; GetSystemInfo(&info);
    const U64 begin = U64(reinterpret_cast<ULONG_PTR>(info.lpMinimumApplicationAddress));
    const U64 end = U64(reinterpret_cast<ULONG_PTR>(info.lpMaximumApplicationAddress)) + 1;
    U64 address = begin, committed = 0, reserved = 0, freeBytes = 0, largestFree = 0;
    DWORD failure = 0;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(ULONG_PTR(address)), &region, sizeof(region))) {
            failure = GetLastError(); break;
        }
        const U64 next = U64(reinterpret_cast<ULONG_PTR>(region.BaseAddress)) + U64(region.RegionSize);
        if (next <= address) { failure = ERROR_INVALID_DATA; break; }
        const U64 size = (next < end ? next : end) - address;
        if (region.State == MEM_COMMIT) committed += size;
        else if (region.State == MEM_RESERVE) reserved += size;
        else if (region.State == MEM_FREE) { freeBytes += size; if (size > largestFree) largestFree = size; }
        address = next;
    }
    std::printf(",\"virtual\":{\"range_begin\":%I64u,\"range_end_exclusive\":%I64u,\"scanned_end_exclusive\":%I64u,\"committed_bytes\":%I64u,\"reserved_bytes\":%I64u,\"free_bytes\":%I64u,\"largest_free_bytes\":%I64u,\"complete\":%s,\"win32_error\":%lu}",
        begin, end, address < end ? address : end, committed, reserved, freeBytes, largestFree,
        failure ? "false" : "true", failure);
}

static void threads(DWORD pid)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    DWORD count = 0, failure = 0;
    if (snapshot == INVALID_HANDLE_VALUE) failure = GetLastError();
    else {
        THREADENTRY32 entry; entry.dwSize = sizeof(entry);
        BOOL found = Thread32First(snapshot, &entry);
        while (found) {
            if (entry.th32OwnerProcessID == pid) ++count;
            entry.dwSize = sizeof(entry);
            found = Thread32Next(snapshot, &entry);
        }
        if (GetLastError() != ERROR_NO_MORE_FILES) failure = GetLastError();
        CloseHandle(snapshot);
    }
    if (failure) std::printf(",\"threads\":null,\"threads_win32_error\":%lu", failure);
    else std::printf(",\"threads\":%lu", count);
}

static void sample(HANDLE process, DWORD pid, unsigned index, U64 created)
{
    DWORD failure = 0;
    const std::string path = imagePath(process, NULL, &failure);
    std::printf("{\"type\":\"sample\",\"sample\":%u,\"pid\":%lu,\"process_created_filetime\":%I64u,\"utc\":\"%s\",\"image\":%s,\"image_win32_error\":%lu",
        index, pid, created, utc().c_str(), path.c_str(), failure);
    PROCESS_MEMORY_COUNTERS_EX memory = {}; memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
        std::printf(",\"memory\":{\"private_bytes\":%I64u,\"working_set_bytes\":%I64u,\"peak_working_set_bytes\":%I64u,\"pagefile_bytes\":%I64u,\"peak_pagefile_bytes\":%I64u}",
            U64(memory.PrivateUsage), U64(memory.WorkingSetSize), U64(memory.PeakWorkingSetSize), U64(memory.PagefileUsage), U64(memory.PeakPagefileUsage));
    else std::printf(",\"memory\":null,\"memory_win32_error\":%lu", GetLastError());
    virtualMemory(process);
    DWORD handles = 0;
    if (GetProcessHandleCount(process, &handles)) std::printf(",\"handles\":%lu", handles);
    else std::printf(",\"handles\":null,\"handles_win32_error\":%lu", GetLastError());
    threads(pid);
    PERFORMANCE_INFORMATION performance = {}; performance.cb = sizeof(performance);
    if (GetPerformanceInfo(&performance, sizeof(performance)))
        std::printf(",\"system_commit\":{\"total_bytes\":%I64u,\"limit_bytes\":%I64u,\"peak_bytes\":%I64u,\"page_size_bytes\":%I64u}",
            U64(performance.CommitTotal) * performance.PageSize, U64(performance.CommitLimit) * performance.PageSize,
            U64(performance.CommitPeak) * performance.PageSize, U64(performance.PageSize));
    else std::printf(",\"system_commit\":null,\"system_commit_win32_error\":%lu", GetLastError());
    std::printf(",\"finished_utc\":\"%s\"}\n", utc().c_str());
    std::fflush(stdout);
}

static int observe(const wchar_t *name)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { error("ProcessSnapshot", GetLastError()); return 1; }
    PROCESSENTRY32W entry; entry.dwSize = sizeof(entry);
    BOOL found = Process32FirstW(snapshot, &entry);
    unsigned matches = 0;
    while (found) {
        if (_wcsicmp(entry.szExeFile, name) == 0) {
            ++matches;
            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
            DWORD failure = process ? 0 : GetLastError();
            const std::string path = process ? imagePath(process, NULL, &failure) : "null";
            std::printf("{\"type\":\"process\",\"utc\":\"%s\",\"pid\":%lu,\"name\":%s,\"image\":%s,\"win32_error\":%lu}\n",
                utc().c_str(), entry.th32ProcessID, quote(entry.szExeFile).c_str(), path.c_str(), failure);
            if (process) CloseHandle(process);
        }
        entry.dwSize = sizeof(entry); found = Process32NextW(snapshot, &entry);
    }
    const DWORD failure = GetLastError();
    CloseHandle(snapshot);
    if (failure != ERROR_NO_MORE_FILES) { error("ProcessEnumeration", failure); return 1; }
    std::printf("{\"type\":\"observe_complete\",\"matches\":%u}\n", matches);
    return 0;
}

static bool number(const wchar_t *text, U64 maximum, U64 *value)
{
    if (!*text) return false;
    U64 result = 0;
    for (; *text; ++text) {
        if (*text < L'0' || *text > L'9') return false;
        const unsigned digit = *text - L'0';
        if (result > (maximum - digit) / 10) return false;
        result = result * 10 + digit;
    }
    if (!result || result > maximum) return false;
    *value = result; return true;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc == 2 && std::wcscmp(argv[1], L"--help") == 0) {
        std::puts("xp-process-probe --pid N [--samples N] [--interval-ms N]\n"
                  "xp-process-probe observe --name executable.exe\n"
                  "Read-only JSONL. Defaults: one sample, 1000 ms interval.\n"
                  "Limits: 3600 samples, interval 10..60000 ms, scheduled span <= 1 hour.");
        return 0;
    }
    if (argc == 4 && std::wcscmp(argv[1], L"observe") == 0 && std::wcscmp(argv[2], L"--name") == 0 && *argv[3])
        return observe(argv[3]);
    U64 pid = 0, samples = 1, interval = 1000;
    unsigned seen = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) { error("arguments", ERROR_INVALID_PARAMETER); return 2; }
        unsigned bit = 0; U64 *destination = NULL, maximum = 0;
        if (std::wcscmp(argv[i], L"--pid") == 0) { bit = 1; destination = &pid; maximum = 0xffffffffULL; }
        else if (std::wcscmp(argv[i], L"--samples") == 0) { bit = 2; destination = &samples; maximum = 3600; }
        else if (std::wcscmp(argv[i], L"--interval-ms") == 0) { bit = 4; destination = &interval; maximum = 60000; }
        if (!bit || (seen & bit) || !number(argv[i + 1], maximum, destination)) {
            error("arguments", ERROR_INVALID_PARAMETER); return 2;
        }
        seen |= bit;
    }
    if (!pid || interval < 10 || (samples - 1) * interval > 3600000) {
        error("arguments", ERROR_INVALID_PARAMETER); return 2;
    }
    // Retain one process object for the whole observation; never reopen by PID.
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process) { error("OpenProcess", GetLastError(), DWORD(pid)); return 1; }
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        error("GetProcessTimes", GetLastError(), DWORD(pid)); CloseHandle(process); return 1;
    }
    const U64 creation = (U64(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    int result = 0;
    for (unsigned i = 0; i < samples; ++i) {
        const DWORD wait = WaitForSingleObject(process, i ? DWORD(interval) : 0);
        if (wait == WAIT_OBJECT_0) {
            std::printf("{\"type\":\"process_exited\",\"pid\":%lu,\"utc\":\"%s\"}\n", DWORD(pid), utc().c_str()); break;
        }
        if (wait != WAIT_TIMEOUT) { error("WaitForSingleObject", GetLastError(), DWORD(pid)); result = 1; break; }
        if (!i) modules(process, DWORD(pid));
        sample(process, DWORD(pid), i + 1, creation);
    }
    CloseHandle(process);
    return result;
}
