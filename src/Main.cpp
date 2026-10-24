// NFSMWCashForDamage: every time Cost to State grows in a career pursuit, the same amount
// (times Multiplier) is added to the player's career cash.
//
// MVP: a background thread polls the game every PollIntervalMs. Nothing in speed.exe is
// patched; we only read memory and add to the cash field.

#include "Addresses.h"
#include "Config.h"
#include "Game.h"
#include "Log.h"
#include "Tracker.h"

#include <windows.h>

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int kVerifyAttempts = 50;  // 100 ms apart: other plugins may still be patching code
constexpr int kStatusLineLimit = 200;
constexpr int32_t kUnknown = INT32_MIN;
constexpr int32_t kNoRaceStatus = -1;

HMODULE g_module = nullptr;

std::string Format(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string out;
    if (length > 0) {
        out.resize(static_cast<size_t>(length) + 1);
        std::vsnprintf(out.data(), out.size(), format, args);
        out.resize(static_cast<size_t>(length));
    }
    va_end(args);
    return out;
}

std::string Utf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

fs::path ModulePath(HMODULE module) {
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(module, buf.data(), static_cast<DWORD>(buf.size()));
    return fs::path(std::wstring(buf.data(), n));
}

class Plugin {
public:
    explicit Plugin(const cfd::Settings& settings)
        : settings_(settings),
          tracker_(settings.source == cfd::Source::CostToState
                       ? cfd::Tracker::NewObject::CountFromZero
                       : cfd::Tracker::NewObject::TakeAsBaseline),
          payout_(settings.multiplier) {}

    void Tick() {
        int32_t flow = 0;
        if (!game::ReadFlowState(flow)) flow = kUnknown;
        if (flow != flow_) {
            Status("GameFlowState: %s -> %s", FlowText(flow_).c_str(), FlowText(flow).c_str());
            flow_ = flow;
        }
        if (flow != addr::kFlowRacing) {
            if (pursuit_.pointer != 0) EndPursuit("interrupted: left the race");
            // Unloading the world frees every pursuit; an address seen again is a new one.
            tracker_.ObjectGone();
            status_ = kUnknown;  // report the player again after loading
            return;
        }

        int32_t context = 0;
        const int32_t contextKey = game::ReadRaceContext(context) ? context : kNoRaceStatus;
        if (contextKey != context_) {
            if (contextKey == addr::kContextCareer) {
                Status("Race context: career");
            } else if (contextKey == kNoRaceStatus) {
                Status("No GRaceStatus: waiting");
            } else {
                Status("Race context %d: not career, no cash is paid", contextKey);
            }
            context_ = contextKey;
        }
        if (contextKey != addr::kContextCareer) {
            status_ = kUnknown;
            return;
        }

        game::Player player;
        const game::PlayerStatus status = game::ReadPlayer(player);
        const bool changed = static_cast<int32_t>(status) != status_;
        if (changed) {
            Status("Player lookup: %s", game::PlayerStatusName(status));
            status_ = static_cast<int32_t>(status);
        }

        std::string increase;
        if (status == game::PlayerStatus::Found) increase = Sample(player, changed);
        if (payout_.Pending() > 0) Pay(increase.empty() ? "deferred payout" : increase);
    }

private:
    struct PursuitInfo {
        uintptr_t pointer = 0;
        int64_t total = 0;
        bool damageKnown = false;
        int32_t damageAtStart = 0;
        int32_t damage = 0;
        int64_t paid = 0;
        bool readFailureLogged = false;
    };

    static std::string FlowText(int32_t flow) {
        if (flow == kUnknown) return "unknown";
        return Format("%d %s", flow, game::FlowStateName(flow));
    }

    template <typename... Args>
    void Status(const char* format, Args... args) {
        if (statusLines_ > kStatusLineLimit) return;
        if (statusLines_++ == kStatusLineLimit) {
            logger::Info("More than %d status changes, no longer logging them", kStatusLineLimit);
            return;
        }
        logger::Info(format, args...);
    }

    // Returns a description of this poll's increase when it produced dollars to pay.
    std::string Sample(const game::Player& player, bool justFound) {
        if (justFound || player.perpetrator != loggedPerpetrator_) {
            loggedPerpetrator_ = player.perpetrator;
            LogPlayer(player);
        }

        int32_t damage = 0;
        const bool damageOk = game::ReadPlayerDamage(player, damage);
        uint32_t total = 0;
        const bool totalOk = player.pursuit != 0 && game::ReadCostToState(player, total);
        TrackPursuit(player, damageOk, damage, totalOk, total);
        if (player.pursuit != 0 && !totalOk && !pursuit_.readFailureLogged) {
            pursuit_.readFailureLogged = true;
            Status("Pursuit 0x%08X: could not read CostToState, skipping this poll",
                   static_cast<unsigned>(player.pursuit));
        }

        cfd::Tracker::Result r;
        if (settings_.source == cfd::Source::CostToState) {
            if (player.pursuit == 0) {
                tracker_.ObjectGone();
            } else if (totalOk) {
                r = tracker_.Sample(player.pursuit, total);
            }
        } else if (damageOk) {
            r = tracker_.Sample(player.perpetrator, damage);
        }

        const char* source = cfd::SourceName(settings_.source);
        if (r.event == cfd::Tracker::Event::Decrease) {
            logger::Info("%s dropped %lld -> %lld: new baseline, nothing paid", source, r.before,
                         r.after);
        } else if (r.event == cfd::Tracker::Event::Implausible) {
            logger::Info("%s %lld -> %lld: jump larger than %lld, looks like a bad read; "
                         "new baseline, nothing paid",
                         source, r.before, r.after, cfd::Tracker::kMaxDelta);
        }
        if (r.delta <= 0) return {};

        const std::string change =
            Format("%s %lld -> %lld (×%g)", source, r.before, r.after, settings_.multiplier);
        if (settings_.multiplier <= 0.0) {
            logger::Info("%s: multiplier is 0, nothing paid", change.c_str());
            return {};
        }
        payout_.Accrue(r.delta);
        if (payout_.Pending() == 0) {
            logger::Info("%s: less than $1, accumulating (carry %.3f)", change.c_str(),
                         payout_.Carry());
            return {};
        }
        return change;
    }

    void Pay(const std::string& what) {
        const int32_t chunk = payout_.NextChunk();
        int32_t before = 0;
        int32_t after = 0;
        if (!game::AddCash(chunk, before, after)) {
            Status("%s: could not write cash, $%lld deferred to the next poll",
                   what.c_str(), payout_.Pending());
            return;
        }
        const int64_t added = static_cast<int64_t>(after) - before;
        payout_.Paid(chunk);
        pursuit_.paid += added;
        logger::Info("+$%lld: %s | cash %d -> %d", added, what.c_str(), before, after);
        if (added < chunk) {
            logger::Info("Cash reached the maximum %d, $%lld more was not paid", INT_MAX,
                         chunk - added + payout_.Pending());
            payout_.Discard();
        }
    }

    void TrackPursuit(const game::Player& player, bool damageOk, int32_t damage, bool totalOk,
                      uint32_t total) {
        if (player.pursuit != pursuit_.pointer) {
            if (pursuit_.pointer != 0) EndPursuit("ended");
            if (player.pursuit != 0) {
                pursuit_.pointer = player.pursuit;
                pursuit_.damageKnown = damageOk;
                pursuit_.damageAtStart = damage;
                logger::Info("Pursuit started (AIPursuit 0x%08X)",
                             static_cast<unsigned>(player.pursuit));
            }
        }
        if (pursuit_.pointer == 0) return;
        if (totalOk) pursuit_.total = total;
        if (damageOk) {
            if (!pursuit_.damageKnown) pursuit_.damageAtStart = damage;
            pursuit_.damageKnown = true;
            pursuit_.damage = damage;
        }
    }

    void EndPursuit(const char* how) {
        logger::Info("Pursuit %s: total %lld, player damage %d, paid $%lld", how,
                     pursuit_.total, pursuit_.damage - pursuit_.damageAtStart, pursuit_.paid);
        pursuit_ = {};
    }

    void LogPlayer(const game::Player& player) {
        int32_t damage = 0;
        const bool damageOk = game::ReadPlayerDamage(player, damage);
        uintptr_t cashAddress = 0;
        int32_t cash = 0;
        const bool cashOk = game::ReadCashAddress(cashAddress) && game::ReadCash(cash);
        Status("Player: IPlayer 0x%08X, ISimable 0x%08X, IVehicleAI 0x%08X, IPerpetrator 0x%08X, "
               "pursuit 0x%08X, player damage %s; cash %s (address 0x%08X)",
               static_cast<unsigned>(player.iplayer), static_cast<unsigned>(player.simable),
               static_cast<unsigned>(player.vehicleAI), static_cast<unsigned>(player.perpetrator),
               static_cast<unsigned>(player.pursuit),
               damageOk ? std::to_string(damage).c_str() : "unreadable",
               cashOk ? Format("$%d", cash).c_str() : "unreadable",
               static_cast<unsigned>(cashAddress));
    }

    cfd::Settings settings_;
    cfd::Tracker tracker_;
    cfd::Payout payout_;
    int32_t flow_ = kUnknown;
    int32_t context_ = kUnknown;
    int32_t status_ = kUnknown;
    uintptr_t loggedPerpetrator_ = 0;
    PursuitInfo pursuit_;
    int statusLines_ = 0;
};

void Run() {
    const fs::path asi = ModulePath(g_module);
    const fs::path dir = asi.parent_path();
    const cfd::ParsedConfig config = cfd::LoadConfig(dir / L"NFSMWCashForDamage.ini");
    const cfd::Settings& s = config.settings;

    logger::Init(dir / L"NFSMWCashForDamage.log", s.log);
    logger::Info("NFSMWCashForDamage %s (built %s)", CFD_VERSION, __DATE__);
    logger::Info("exe: %s", Utf8(ModulePath(nullptr)).c_str());
    logger::Info("plugin: %s", Utf8(asi).c_str());
    for (const std::string& warning : config.warnings) {
        logger::Info("INI, %s", warning.c_str());
    }
    logger::Info("Settings: Enabled=%d Multiplier=%g PollIntervalMs=%d Source=%s Log=%d",
                 s.enabled ? 1 : 0, s.multiplier, s.pollIntervalMs, cfd::SourceName(s.source),
                 s.log ? 1 : 0);
    if (!s.enabled) {
        logger::Info("Enabled=0: plugin is off");
        return;
    }

    std::string failure;
    game::BuildCheck check = game::BuildCheck::CodeDiffers;
    for (int attempt = 0; attempt < kVerifyAttempts; ++attempt) {
        check = game::VerifyBuild(failure);
        if (check != game::BuildCheck::CodeDiffers) break;
        Sleep(100);
    }
    if (check == game::BuildCheck::NotV13) {
        logger::Fatal("this is not speed.exe v1.3, the plugin does nothing: %s", failure.c_str());
        return;
    }
    if (check == game::BuildCheck::CodeDiffers) {
        logger::Fatal("speed.exe v1.3 code in memory is not what was expected (another plugin "
                      "may have patched it), the plugin does nothing. Differences: %s",
                      failure.c_str());
        return;
    }
    logger::Info("speed.exe v1.3 confirmed, polling started");

    Plugin plugin(s);
    for (;;) {
        plugin.Tick();
        Sleep(static_cast<DWORD>(s.pollIntervalMs));
    }
}

DWORD WINAPI ThreadMain(LPVOID) {
    // An exception escaping this thread would terminate the game.
    try {
        Run();
    } catch (const std::exception& e) {
        logger::Fatal("exception: %s, plugin stopped", e.what());
    } catch (...) {
        logger::Fatal("unknown exception, plugin stopped");
    }
    return 0;
}

}  // namespace

// DisableThreadLibraryCalls is not used: the static CRT needs thread notifications.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        if (HANDLE thread = CreateThread(nullptr, 0, ThreadMain, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
