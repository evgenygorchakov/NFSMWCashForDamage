#pragma once

// NFSMWCashForDamage.ini. Our own parser: GetPrivateProfileString does not strip a comment
// at the end of a line ("Source=CostToState ; ..."), which the documented format uses.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cfd {

enum class Source {
    CostToState,   // pursuit total, the number on the HUD
    PlayerDamage,  // only what the player broke or hit
};

const char* SourceName(Source source);

struct Settings {
    bool enabled = true;
    double multiplier = 1.0;
    int pollIntervalMs = 100;
    Source source = Source::CostToState;
    bool log = true;
};

inline constexpr double kMinMultiplier = 0.0;
inline constexpr double kMaxMultiplier = 1000.0;
inline constexpr int kMinPollIntervalMs = 10;
inline constexpr int kMaxPollIntervalMs = 5000;

struct ParsedConfig {
    Settings settings;
    std::vector<std::string> warnings;  // UTF-8, ready for the log
};

// Sections are ignored. ';' and '#' start a comment anywhere on a line. Keys are
// case-insensitive. A UTF-8 BOM is skipped. Bad values keep the default with a warning.
ParsedConfig ParseConfig(std::string_view text);

// Missing file: defaults plus a warning.
ParsedConfig LoadConfig(const std::filesystem::path& path);

}  // namespace cfd
