#include "Config.h"

#include "Check.h"

#include <string>

using cfd::ParseConfig;
using cfd::Source;

namespace cfd {
std::ostream& operator<<(std::ostream& os, Source s) {
    return os << SourceName(s);
}
}  // namespace cfd

namespace {

bool HasWarning(const cfd::ParsedConfig& c, const std::string& part) {
    for (const std::string& w : c.warnings) {
        if (w.find(part) != std::string::npos) return true;
    }
    return false;
}

void Defaults() {
    const auto c = ParseConfig("");
    CHECK(c.warnings.empty());
    CHECK_EQ(c.settings.enabled, true);
    CHECK_EQ(c.settings.multiplier, 1.0);
    CHECK_EQ(c.settings.pollIntervalMs, 100);
    CHECK_EQ(c.settings.source, Source::CostToState);
    CHECK_EQ(c.settings.log, true);
}

void TaskExample() {
    // Exactly the format from the task, including the comment after a value.
    const auto c = ParseConfig(
        "Enabled=1\n"
        "Multiplier=1.0\n"
        "PollIntervalMs=100\n"
        "Source=CostToState   ; на будущее: возможность переключить на Bounty\n"
        "Log=1\n");
    CHECK(c.warnings.empty());
    CHECK_EQ(c.settings.source, Source::CostToState);
    CHECK_EQ(c.settings.multiplier, 1.0);
}

void BomCrlfSectionsCase() {
    const auto c = ParseConfig(
        "\xEF\xBB\xBF[General]\r\n"
        "  ENABLED = 0   # off\r\n"
        "multiplier=2,5\r\n"
        "\r\n"
        "; comment line\r\n"
        "source = playerdamage\r\n"
        "PollIntervalMs=+250\r\n"
        "LOG=off");
    CHECK(c.warnings.empty());
    CHECK_EQ(c.settings.enabled, false);
    CHECK_EQ(c.settings.multiplier, 2.5);
    CHECK_EQ(c.settings.source, Source::PlayerDamage);
    CHECK_EQ(c.settings.pollIntervalMs, 250);
    CHECK_EQ(c.settings.log, false);
}

void OutOfRangeIsClamped() {
    auto c = ParseConfig("Multiplier=5000\nPollIntervalMs=1\n");
    CHECK_EQ(c.settings.multiplier, 1000.0);
    CHECK_EQ(c.settings.pollIntervalMs, 10);
    CHECK_EQ(c.warnings.size(), size_t{2});
    CHECK(HasWarning(c, "строка 1: Multiplier=5000 вне диапазона 0..1000, взято 1000"));
    CHECK(HasWarning(c, "строка 2:"));

    c = ParseConfig("Multiplier=-1\nPollIntervalMs=99999\n");
    CHECK_EQ(c.settings.multiplier, 0.0);
    CHECK_EQ(c.settings.pollIntervalMs, 5000);
    CHECK_EQ(c.warnings.size(), size_t{2});

    c = ParseConfig("Multiplier=0\nMultiplier=1000\nPollIntervalMs=5000");
    CHECK_EQ(c.settings.multiplier, 1000.0);
    CHECK_EQ(c.settings.pollIntervalMs, 5000);
    CHECK(HasWarning(c, "задан повторно"));
}

void InvalidValuesKeepDefaults() {
    const char* badMultipliers[] = {"abc", "1.5x", "nan", "inf", "", "1..0", "--1"};
    for (const char* bad : badMultipliers) {
        const auto c = ParseConfig(std::string("Multiplier=") + bad);
        CHECK_EQ(c.settings.multiplier, 1.0);
        CHECK(HasWarning(c, "не число"));
    }

    auto c = ParseConfig("PollIntervalMs=1.5\nEnabled=maybe\nLog=2\n");
    CHECK_EQ(c.settings.pollIntervalMs, 100);
    CHECK_EQ(c.settings.enabled, true);
    CHECK_EQ(c.settings.log, true);
    CHECK_EQ(c.warnings.size(), size_t{3});

    c = ParseConfig("Source=Foo");
    CHECK_EQ(c.settings.source, Source::CostToState);
    CHECK(HasWarning(c, "неизвестный источник"));

    c = ParseConfig("Source=PlayerDamage\nSource=Foo");
    CHECK_EQ(c.settings.source, Source::PlayerDamage);
}

void BountyFallsBack() {
    const auto c = ParseConfig("Source=Bounty ; позже");
    CHECK_EQ(c.settings.source, Source::CostToState);
    CHECK(HasWarning(c, "пока не поддерживается"));
}

void UnknownAndBrokenLines() {
    const auto c = ParseConfig("Foo=1\njust text\n=5\n");
    CHECK_EQ(c.warnings.size(), size_t{3});
    CHECK(HasWarning(c, "строка 1: неизвестный ключ Foo"));
    CHECK(HasWarning(c, "строка 2: нет «=»"));
}

void MissingFile() {
    const auto c = cfd::LoadConfig(std::filesystem::path(CFD_SOURCE_DIR) / "no-such-file.ini");
    CHECK_EQ(c.warnings.size(), size_t{1});
    CHECK(HasWarning(c, "не найден"));
    CHECK_EQ(c.settings.multiplier, 1.0);
}

void ShippedIniIsClean() {
    const auto c = cfd::LoadConfig(std::filesystem::path(CFD_SOURCE_DIR) / "NFSMWCashForDamage.ini");
    for (const std::string& w : c.warnings) std::printf("unexpected warning: %s\n", w.c_str());
    CHECK(c.warnings.empty());
    CHECK_EQ(c.settings.enabled, true);
    CHECK_EQ(c.settings.multiplier, 1.0);
    CHECK_EQ(c.settings.pollIntervalMs, 100);
    CHECK_EQ(c.settings.source, Source::CostToState);
    CHECK_EQ(c.settings.log, true);
}

}  // namespace

int main() {
    Defaults();
    TaskExample();
    BomCrlfSectionsCase();
    OutOfRangeIsClamped();
    InvalidValuesKeepDefaults();
    BountyFallsBack();
    UnknownAndBrokenLines();
    MissingFile();
    ShippedIniIsClean();
    return Finish("ConfigTest");
}
