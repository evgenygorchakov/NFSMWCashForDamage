#pragma once

// Reads game state through the verified offsets in Addresses.h. Nothing here calls into
// the game except AIPursuit::CalcTotalCostToState, a leaf function that only reads fields.

#include <cstdint>
#include <string>

namespace game {

enum class BuildCheck {
    Ok,
    NotV13,       // a different exe: base address or PE timestamp differ
    CodeDiffers,  // v1.3, but code or vtables we rely on differ (another plugin patched them?)
};

// Checks the image header and the code and vtables we rely on. On mismatch fills
// `failure` with every differing spot (expected vs found).
BuildCheck VerifyBuild(std::string& failure);

bool ReadFlowState(int32_t& state);
const char* FlowStateName(int32_t state);

// False when GRaceStatus::fObj is null or unreadable.
bool ReadRaceContext(int32_t& context);

enum class PlayerStatus {
    Found,
    NoLocalPlayer,
    BadPlayer,
    BadSimable,
    BadInterfaceList,
    NoVehicleAI,
    NoPerpetrator,
    UnexpectedVtable,
    BadLayout,
    BadPursuit,
};
const char* PlayerStatusName(PlayerStatus status);

struct Player {
    uintptr_t iplayer = 0;
    uintptr_t simable = 0;
    uintptr_t com = 0;
    uintptr_t vehicleAI = 0;
    uintptr_t perpetrator = 0;
    uintptr_t pursuit = 0;  // 0 outside a pursuit
};

PlayerStatus ReadPlayer(Player& out);

// AIPerpVehicle::mCostToState: what the player broke or hit during pursuits.
bool ReadPlayerDamage(const Player& player, int32_t& out);
// AIPursuit::CalcTotalCostToState, the "Cost to State" number on the HUD. Fails when the
// player has no pursuit or the pursuit went away during the call.
bool ReadCostToState(const Player& player, uint32_t& out);

bool ReadCashAddress(uintptr_t& address);
bool ReadCash(int32_t& cash);
// Adds to the career cash, saturating at INT32_MAX.
bool AddCash(int32_t amount, int32_t& before, int32_t& after);

}  // namespace game
