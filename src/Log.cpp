#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace logger {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
std::filesystem::path g_path;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_enabled = false;
bool g_triedOpen = false;

void WriteRaw(const std::string& bytes) {
    DWORD written = 0;
    WriteFile(g_file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}

// Caller holds the lock.
bool EnsureOpen() {
    if (g_file != INVALID_HANDLE_VALUE) return true;
    if (g_triedOpen || g_path.empty()) return false;
    g_triedOpen = true;
    g_file = CreateFileW(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) return false;
    WriteRaw("\xEF\xBB\xBF");
    return true;
}

void Write(const char* prefix, const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length < 0) return;

    SYSTEMTIME now;
    GetLocalTime(&now);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "[%02u:%02u:%02u.%03u] ", now.wHour, now.wMinute,
                  now.wSecond, now.wMilliseconds);

    std::string line = stamp;
    line += prefix;
    const size_t start = line.size();
    line.resize(start + static_cast<size_t>(length) + 1);
    std::vsnprintf(line.data() + start, static_cast<size_t>(length) + 1, format, args);
    line.resize(start + static_cast<size_t>(length));
    line += "\r\n";

    AcquireSRWLockExclusive(&g_lock);
    if (EnsureOpen()) WriteRaw(line);
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace

void Init(const std::filesystem::path& path, bool enabled) {
    AcquireSRWLockExclusive(&g_lock);
    g_path = path;
    g_enabled = enabled;
    if (enabled) {
        EnsureOpen();
    } else {
        DeleteFileW(path.c_str());
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool Enabled() {
    return g_enabled;
}

void Info(const char* format, ...) {
    if (!g_enabled) return;
    va_list args;
    va_start(args, format);
    Write("", format, args);
    va_end(args);
}

void Fatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    Write("ОШИБКА: ", format, args);
    va_end(args);
}

}  // namespace logger
