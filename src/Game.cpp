#include "Game.h"

#include "Addresses.h"
#include "Memory.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace game {
namespace {

struct Signature {
    uintptr_t address;
    const uint8_t* bytes;
    size_t size;
    const char* what;
};

// Leading bytes of the functions we depend on, taken from speed.exe v1.3.
constexpr uint8_t kAddCostToState[] = {0xA1, 0x00, 0xE0, 0x91, 0x00, 0x85, 0xC0, 0x57,
                                       0x8B, 0xF9, 0x74, 0x5F, 0x8B, 0x87, 0xF4, 0xF8,
                                       0xFF, 0xFF, 0x8D, 0x8F, 0xF4, 0xF8, 0xFF, 0xFF};
constexpr uint8_t kCareerContextCheck[] = {0x8B, 0x15, 0x00, 0xE0, 0x91, 0x00, 0x83,
                                           0xBA, 0x64, 0x19, 0x00, 0x00, 0x02};
constexpr uint8_t kGetCostToState[] = {0x8B, 0x41, 0x20, 0xC3};
constexpr uint8_t kCalcTotalCostToState[] = {0x8B, 0x81, 0x3C, 0x01, 0x00, 0x00,
                                             0x8B, 0x91, 0x50, 0x01, 0x00, 0x00};
constexpr uint8_t kGetPursuit[] = {0x8B, 0x41, 0x70, 0xC3};
constexpr uint8_t kVehicleAIHandle[] = {0xB8, 0x00, 0x37, 0x40, 0x00, 0xC3};
constexpr uint8_t kPerpetratorHandle[] = {0xB8, 0xE0, 0x37, 0x40, 0x00, 0xC3};
constexpr uint8_t kIListFind[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0x71, 0x08, 0x8B, 0x49, 0x04};
constexpr uint8_t kGetPlayerPursuit[] = {0xA1, 0x84, 0xD8, 0x92, 0x00, 0x85, 0xC0, 0x56,
                                         0x74, 0x14, 0xA1, 0x7C, 0xD8, 0x92, 0x00};
constexpr uint8_t kGetSimableThunk[] = {0x83, 0xE9, 0x20, 0xE9, 0x78, 0xFF, 0xFF, 0xFF};
constexpr uint8_t kGetSimable[] = {0x8B, 0x41, 0x18, 0xC3};
constexpr uint8_t kCareerSettingsPath[] = {0xA1, 0x90, 0xCF, 0x91, 0x00, 0x8B, 0x48, 0x10,
                                           0x56, 0x81, 0xC1, 0xA8, 0x00, 0x00, 0x00};
constexpr uint8_t kSpendCash[] = {0x8B, 0x41, 0x0C, 0x8B, 0x54, 0x24, 0x04};
constexpr uint8_t kFlowStateCheck[] = {0x83, 0x3D, 0x90, 0x5E, 0x92, 0x00, 0x06};

#define SIG(address, bytes, what) {address, bytes, sizeof(bytes), what}
constexpr Signature kSignatures[] = {
    SIG(0x00409410, kAddCostToState, "AIPerpVehicle::AddCostToState"),
    SIG(0x00409454, kCareerContextCheck, "AddCostToState: context == career"),
    SIG(0x00409050, kGetCostToState, "AIPerpVehicle::GetCostToState"),
    SIG(0x0040AA00, kCalcTotalCostToState, "AIPursuit::CalcTotalCostToState"),
    SIG(0x00431D70, kGetPursuit, "AIVehicle::GetPursuit"),
    SIG(0x00403700, kVehicleAIHandle, "IVehicleAI::_IHandle"),
    SIG(0x004037E0, kPerpetratorHandle, "IPerpetrator::_IHandle"),
    SIG(0x005D59F0, kIListFind, "UTL::COM::Object::_IList::Find"),
    SIG(0x005EB9E0, kGetPlayerPursuit, "GManager::GetPlayerPursuitInterfaces"),
    SIG(0x006F8FB0, kGetSimableThunk, "LocalPlayer::GetSimable (thunk)"),
    SIG(0x006F8F30, kGetSimable, "LocalPlayer::GetSimable"),
    SIG(0x007BAC19, kCareerSettingsPath, "CarCustomizeManager::Checkout: CareerSettings"),
    SIG(0x0056D7A0, kSpendCash, "CareerSettings::SpendCash"),
    SIG(0x0046E159, kFlowStateCheck, "GameFlowState == RACING"),
};
#undef SIG

struct VtableSlot {
    uintptr_t vtable;
    uintptr_t offset;
    uint32_t target;
    const char* what;
};

constexpr VtableSlot kVtableSlots[] = {
    {addr::kVtHumanPerpetrator, 0x1C, 0x00409050, "IPerpetrator::GetCostToState"},
    {addr::kVtHumanVehicleAI, 0xA0, 0x00431D70, "IVehicleAI::GetPursuit"},
    {addr::kVtAIPursuit, 0xE0, 0x0040AA00, "IPursuit::CalcTotalCostToState"},
    {addr::kVtLocalPlayerIPlayer, 0x04, 0x006F8FB0, "IPlayer::GetSimable"},
};

std::string Hex(const uint8_t* bytes, size_t size) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < size; ++i) {
        std::snprintf(buf, sizeof(buf), i ? " %02X" : "%02X", bytes[i]);
        out += buf;
    }
    return out;
}

void AddFailure(std::string& failure, const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (!failure.empty()) failure += "; ";
    failure += buf;
}

bool HasVtable(uintptr_t object, uintptr_t vtable) {
    uint32_t actual = 0;
    return mem::ReadU32(object, actual) && actual == vtable;
}

}  // namespace

BuildCheck VerifyBuild(std::string& failure) {
    failure.clear();

    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base != addr::kImageBase) {
        AddFailure(failure, "база exe 0x%08X, ожидалась 0x%08X", static_cast<unsigned>(base),
                   static_cast<unsigned>(addr::kImageBase));
        return BuildCheck::NotV13;
    }

    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!mem::Read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !mem::Read(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) {
        AddFailure(failure, "не читается PE-заголовок exe");
        return BuildCheck::NotV13;
    }
    if (nt.FileHeader.TimeDateStamp != addr::kPeTimestamp) {
        AddFailure(failure, "PE timestamp 0x%08X, ожидался 0x%08X (v1.3)",
                   static_cast<unsigned>(nt.FileHeader.TimeDateStamp),
                   static_cast<unsigned>(addr::kPeTimestamp));
        return BuildCheck::NotV13;
    }

    for (const Signature& sig : kSignatures) {
        uint8_t actual[32] = {};
        if (sig.size > sizeof(actual) || !mem::Read(sig.address, actual, sig.size)) {
            AddFailure(failure, "0x%08X (%s): не читается", static_cast<unsigned>(sig.address),
                       sig.what);
        } else if (std::memcmp(actual, sig.bytes, sig.size) != 0) {
            AddFailure(failure, "0x%08X (%s): ожидалось %s, в памяти %s",
                       static_cast<unsigned>(sig.address), sig.what,
                       Hex(sig.bytes, sig.size).c_str(), Hex(actual, sig.size).c_str());
        }
    }

    for (const VtableSlot& slot : kVtableSlots) {
        uint32_t actual = 0;
        const uintptr_t at = slot.vtable + slot.offset;
        if (!mem::ReadU32(at, actual)) {
            AddFailure(failure, "vtable 0x%08X+0x%02X (%s): не читается",
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(slot.offset),
                       slot.what);
        } else if (actual != slot.target) {
            AddFailure(failure, "vtable 0x%08X+0x%02X (%s): ожидалось 0x%08X, в памяти 0x%08X",
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(slot.offset),
                       slot.what, static_cast<unsigned>(slot.target),
                       static_cast<unsigned>(actual));
        }
    }

    return failure.empty() ? BuildCheck::Ok : BuildCheck::CodeDiffers;
}

bool ReadFlowState(int32_t& state) {
    return mem::Read(addr::kGameFlowState, &state, sizeof(state));
}

const char* FlowStateName(int32_t state) {
    // GameFlowState from the decompilation (Misc/GameFlow.hpp).
    switch (state) {
        case 0: return "NONE";
        case 1: return "LOADING_FRONTEND";
        case 2: return "UNLOADING_FRONTEND";
        case 3: return "IN_FRONTEND (меню)";
        case 4: return "LOADING_REGION";
        case 5: return "LOADING_TRACK";
        case 6: return "RACING (в игре)";
        case 7: return "UNLOADING_TRACK";
        case 8: return "UNLOADING_REGION";
        case 9: return "EXIT_DEMO_DISC";
        default: return "?";
    }
}

bool ReadRaceContext(int32_t& context) {
    uintptr_t raceStatus = 0;
    return mem::ReadPointer(addr::kRaceStatus, raceStatus) &&
           mem::Read(raceStatus + addr::kRaceContextOffset, &context, sizeof(context));
}

const char* PlayerStatusName(PlayerStatus status) {
    switch (status) {
        case PlayerStatus::Found: return "игрок найден";
        case PlayerStatus::NoLocalPlayer: return "нет локального игрока";
        case PlayerStatus::BadPlayer: return "IPlayer не LocalPlayer (vtable)";
        case PlayerStatus::BadSimable: return "не читается ISimable игрока";
        case PlayerStatus::BadInterfaceList: return "не читается список интерфейсов";
        case PlayerStatus::NoVehicleAI: return "нет IVehicleAI";
        case PlayerStatus::NoPerpetrator: return "нет IPerpetrator";
        case PlayerStatus::UnexpectedVtable: return "IVehicleAI/IPerpetrator не AIVehicleHuman (vtable)";
        case PlayerStatus::BadLayout: return "IPerpetrator - IVehicleAI != 0x70C";
        case PlayerStatus::BadPursuit: return "указатель погони не AIPursuit (vtable)";
    }
    return "?";
}

PlayerStatus ReadPlayer(Player& out) {
    out = {};

    uint32_t count = 0;
    uintptr_t first = 0;
    if (!mem::ReadU32(addr::kLocalPlayerCount, count) || count == 0 ||
        !mem::ReadPointer(addr::kLocalPlayerArray, first) || !mem::ReadPointer(first, out.iplayer)) {
        return PlayerStatus::NoLocalPlayer;
    }
    if (!HasVtable(out.iplayer, addr::kVtLocalPlayerIPlayer)) return PlayerStatus::BadPlayer;

    if (!mem::ReadPointer(out.iplayer + addr::kPlayerSimableOffset, out.simable) ||
        !mem::ReadPointer(out.simable + addr::kSimableComOffset, out.com)) {
        return PlayerStatus::BadSimable;
    }

    uintptr_t begin = 0;
    uintptr_t end = 0;
    if (!mem::ReadPointer(out.com + addr::kListBeginOffset, begin) ||
        !mem::ReadPointer(out.com + addr::kListEndOffset, end) || end <= begin ||
        (end - begin) % 8 != 0 || (end - begin) / 8 > addr::kMaxInterfaces) {
        return PlayerStatus::BadInterfaceList;
    }
    uint32_t pairs[addr::kMaxInterfaces * 2];
    const size_t pairCount = (end - begin) / 8;
    if (!mem::Read(begin, pairs, pairCount * 8)) return PlayerStatus::BadInterfaceList;
    for (size_t i = 0; i < pairCount; ++i) {
        const uint32_t handle = pairs[2 * i];
        const uint32_t iface = pairs[2 * i + 1];
        if (handle == addr::kHandleVehicleAI) out.vehicleAI = iface;
        if (handle == addr::kHandlePerpetrator) out.perpetrator = iface;
    }
    if (!mem::IsPlausiblePointer(out.vehicleAI)) return PlayerStatus::NoVehicleAI;
    if (!mem::IsPlausiblePointer(out.perpetrator)) return PlayerStatus::NoPerpetrator;
    if (!HasVtable(out.vehicleAI, addr::kVtHumanVehicleAI) ||
        !HasVtable(out.perpetrator, addr::kVtHumanPerpetrator)) {
        return PlayerStatus::UnexpectedVtable;
    }
    if (out.perpetrator - out.vehicleAI != addr::kPerpMinusVehicleAI) return PlayerStatus::BadLayout;

    uint32_t pursuit = 0;
    if (!mem::ReadU32(out.vehicleAI + addr::kVehicleAIPursuitOffset, pursuit)) {
        return PlayerStatus::BadPursuit;
    }
    if (pursuit != 0 && !HasVtable(pursuit, addr::kVtAIPursuit)) return PlayerStatus::BadPursuit;
    out.pursuit = pursuit;
    return PlayerStatus::Found;
}

bool ReadPlayerDamage(const Player& player, int32_t& out) {
    return player.perpetrator != 0 &&
           mem::Read(player.perpetrator + addr::kPerpCostToStateOffset, &out, sizeof(out));
}

bool ReadCostToState(const Player& player, uint32_t& out) {
    if (player.pursuit == 0 || !HasVtable(player.pursuit, addr::kVtAIPursuit)) return false;
    uint32_t value = 0;
    if (!mem::CallCalcTotalCostToState(player.pursuit, value)) return false;
    // The main thread may have ended the pursuit while we were reading it.
    uint32_t still = 0;
    if (!mem::ReadU32(player.vehicleAI + addr::kVehicleAIPursuitOffset, still) ||
        still != player.pursuit || !HasVtable(player.pursuit, addr::kVtAIPursuit)) {
        return false;
    }
    out = value;
    return true;
}

bool ReadCashAddress(uintptr_t& address) {
    uintptr_t database = 0;
    uintptr_t profile = 0;
    if (!mem::ReadPointer(addr::kFEDatabase, database) ||
        !mem::ReadPointer(database + addr::kUserProfileOffset, profile)) {
        return false;
    }
    address = profile + addr::kCashOffset;
    return true;
}

bool ReadCash(int32_t& cash) {
    uintptr_t address = 0;
    return ReadCashAddress(address) && mem::Read(address, &cash, sizeof(cash));
}

bool AddCash(int32_t amount, int32_t& before, int32_t& after) {
    uintptr_t address = 0;
    return ReadCashAddress(address) && mem::AddInt32Saturating(address, amount, before, after);
}

}  // namespace game
