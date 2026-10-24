#include "Config.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace cfd {
namespace {

constexpr std::string_view kSpace = " \t\r\n\v\f";

std::string_view Trim(std::string_view s) {
    const size_t first = s.find_first_not_of(kSpace);
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(kSpace) - first + 1);
}

char ToLowerAscii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), ToLowerAscii);
    return out;
}

std::optional<bool> ParseBool(std::string_view v) {
    const std::string s = Lower(v);
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return std::nullopt;
}

std::optional<double> ParseDouble(std::string_view v) {
    std::string s(v);
    std::replace(s.begin(), s.end(), ',', '.');
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    if (begin != end && *begin == '+') ++begin;
    double out = 0.0;
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    if (begin == end || ec != std::errc{} || ptr != end || !std::isfinite(out)) return std::nullopt;
    return out;
}

std::optional<int> ParseInt(std::string_view v) {
    const char* begin = v.data();
    const char* end = v.data() + v.size();
    if (begin != end && *begin == '+') ++begin;
    int out = 0;
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    if (begin == end || ec != std::errc{} || ptr != end) return std::nullopt;
    return out;
}

std::string FormatDouble(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

class Parser {
public:
    explicit Parser(ParsedConfig& out) : out_(out) {}

    void Line(int number, std::string_view raw) {
        number_ = number;
        const size_t comment = raw.find_first_of(";#");
        const std::string_view text = Trim(raw.substr(0, comment));
        if (text.empty() || text.front() == '[') return;

        const size_t eq = text.find('=');
        if (eq == std::string_view::npos) {
            Warn("no '=', line skipped: " + std::string(text));
            return;
        }
        const std::string_view keyText = Trim(text.substr(0, eq));
        const std::string_view value = Trim(text.substr(eq + 1));
        const std::string key = Lower(keyText);
        const std::string shown = std::string(keyText) + "=" + std::string(value);

        if (key == "enabled" || key == "multiplier" || key == "pollintervalms" ||
            key == "source" || key == "log") {
            if (!seen_.insert(key).second) {
                Warn("key " + std::string(keyText) + " is set again, replacing the previous value");
            }
        }

        Settings& s = out_.settings;
        if (key == "enabled") {
            Bool(shown, value, s.enabled);
        } else if (key == "log") {
            Bool(shown, value, s.log);
        } else if (key == "multiplier") {
            const auto v = ParseDouble(value);
            if (!v) {
                Warn(shown + " is not a number, keeping " + FormatDouble(s.multiplier));
            } else if (*v < kMinMultiplier || *v > kMaxMultiplier) {
                s.multiplier = std::clamp(*v, kMinMultiplier, kMaxMultiplier);
                Warn(shown + " is out of range " + FormatDouble(kMinMultiplier) + ".." +
                     FormatDouble(kMaxMultiplier) + ", using " + FormatDouble(s.multiplier));
            } else {
                s.multiplier = *v;
            }
        } else if (key == "pollintervalms") {
            const auto v = ParseInt(value);
            if (!v) {
                Warn(shown + " is not an integer, keeping " + std::to_string(s.pollIntervalMs));
            } else if (*v < kMinPollIntervalMs || *v > kMaxPollIntervalMs) {
                s.pollIntervalMs = std::clamp(*v, kMinPollIntervalMs, kMaxPollIntervalMs);
                Warn(shown + " is out of range " + std::to_string(kMinPollIntervalMs) + ".." +
                     std::to_string(kMaxPollIntervalMs) + ", using " +
                     std::to_string(s.pollIntervalMs));
            } else {
                s.pollIntervalMs = *v;
            }
        } else if (key == "source") {
            const std::string v = Lower(value);
            if (v == "costtostate") {
                s.source = Source::CostToState;
            } else if (v == "playerdamage") {
                s.source = Source::PlayerDamage;
            } else if (v == "bounty") {
                s.source = Source::CostToState;
                Warn(shown + " is not supported yet, using CostToState");
            } else {
                Warn(shown + " is an unknown source (CostToState or PlayerDamage), keeping " +
                     SourceName(s.source));
            }
        } else {
            Warn("unknown key " + std::string(keyText) + ", skipped");
        }
    }

private:
    void Warn(const std::string& message) {
        out_.warnings.push_back("line " + std::to_string(number_) + ": " + message);
    }

    void Bool(const std::string& shown, std::string_view value, bool& target) {
        const auto v = ParseBool(value);
        if (v) {
            target = *v;
        } else {
            Warn(shown + " should be 0 or 1, keeping " + (target ? "1" : "0"));
        }
    }

    ParsedConfig& out_;
    int number_ = 0;
    std::set<std::string> seen_;
};

}  // namespace

const char* SourceName(Source source) {
    return source == Source::PlayerDamage ? "PlayerDamage" : "CostToState";
}

ParsedConfig ParseConfig(std::string_view text) {
    ParsedConfig out;
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);

    Parser parser(out);
    int number = 1;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        parser.Line(number++, text.substr(0, nl));
        if (nl == std::string_view::npos) break;
        text.remove_prefix(nl + 1);
    }
    return out;
}

ParsedConfig LoadConfig(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ParsedConfig out;
        out.warnings.push_back("settings file not found: " + ToUtf8(path) +
                               ", using defaults");
        return out;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return ParseConfig(content.str());
}

}  // namespace cfd
