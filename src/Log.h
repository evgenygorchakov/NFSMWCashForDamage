#pragma once

// NFSMWCashForDamage.log: UTF-8 with BOM, rewritten on every start. Each line goes straight
// to the OS with WriteFile, so it survives a game crash. Safe to call from any thread.

#include <filesystem>

namespace logger {

// With `enabled` the file is created now. Without it only Fatal writes, and a log left over
// from an earlier session is deleted so it cannot be mistaken for this one.
void Init(const std::filesystem::path& path, bool enabled);
bool Enabled();

// printf-style, UTF-8.
void Info(const char* format, ...);
// Written even with Log=0.
void Fatal(const char* format, ...);

}  // namespace logger
