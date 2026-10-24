#pragma once

// Addresses and offsets for speed.exe v1.3 (PE timestamp 0x438E4C8C, base 0x400000).
// Every value comes from refs/nfsmw/config/SPEED_EXE_1_3/symbols.json and/or dumpbin
// disassembly of this exe.

#include <cstdint>

namespace addr {

// Image
inline constexpr uintptr_t kImageBase     = 0x00400000;
inline constexpr uint32_t  kPeTimestamp   = 0x438E4C8C;

// ?TheGameFlowManager@@3VGameFlowManager@@A, int32 state at offset 0.
// cmp dword ptr [0x925E90], 6 at 0x46E159.
inline constexpr uintptr_t kGameFlowState = 0x00925E90;
inline constexpr int32_t   kFlowRacing    = 6;  // GAMEFLOW_STATE_RACING (GameFlow.hpp)

// ?fObj@GRaceStatus@@0PAV1@A. Race context at +0x1964, 2 = GRace::kRaceContext_Career
// (AIPerpVehicle::AddCostToState 0x409454 does the same check).
inline constexpr uintptr_t kRaceStatus        = 0x0091E000;
inline constexpr uintptr_t kRaceContextOffset = 0x1964;
inline constexpr int32_t   kContextCareer     = 2;

// ListableSet<IPlayer> local bucket, read by GManager::GetPlayerPursuitInterfaces 0x5EB9E0:
// count = [0x92D884], first IPlayer* = [[0x92D87C]].
inline constexpr uintptr_t kLocalPlayerCount = 0x0092D884;
inline constexpr uintptr_t kLocalPlayerArray = 0x0092D87C;

// LocalPlayer as IPlayer. Slot +0x04 GetSimable is the thunk 0x6F8FB0 (sub ecx,20h) into
// 0x6F8F30 (mov eax,[ecx+18h]), so ISimable* = [iplayer - 0x08].
inline constexpr uintptr_t kVtLocalPlayerIPlayer = 0x008B0B10;
inline constexpr uintptr_t kPlayerSimableOffset  = static_cast<uintptr_t>(-0x08);

// ISimable -> UTL::COM::Object at +0x04. _IList::Find 0x5D59F0 reads the sorted
// {handle, interface} array between [list+0x04] and [list+0x08].
inline constexpr uintptr_t kSimableComOffset = 0x04;
inline constexpr uintptr_t kListBeginOffset  = 0x04;
inline constexpr uintptr_t kListEndOffset    = 0x08;
inline constexpr uint32_t  kMaxInterfaces    = 64;  // sanity cap, not a game value

// Interface handles are the addresses of the _IHandle functions.
inline constexpr uint32_t kHandleVehicleAI   = 0x00403700;  // ?_IHandle@IVehicleAI@@SAPAXXZ
inline constexpr uint32_t kHandlePerpetrator = 0x004037E0;  // ?_IHandle@IPerpetrator@@SAPAXXZ

// The player's AI object is AIVehicleHuman.
inline constexpr uintptr_t kVtHumanVehicleAI   = 0x008929F0;  // ??_7AIVehicleHuman@@6BIVehicleAI@@@
inline constexpr uintptr_t kVtHumanPerpetrator = 0x00892988;  // ??_7AIVehicleHuman@@6BIPerpetrator@@@
// AddCostToState 0x409410: IVehicleAI = IPerpetrator - 0x70C.
inline constexpr uintptr_t kPerpMinusVehicleAI = 0x70C;

// AIVehicle::GetPursuit 0x431D70: mov eax,[ecx+70h]. Null outside a pursuit.
inline constexpr uintptr_t kVehicleAIPursuitOffset = 0x70;
inline constexpr uintptr_t kVtAIPursuit            = 0x00892770;  // ??_7AIPursuit@@6B@

// AIPerpVehicle::GetCostToState 0x409050: mov eax,[ecx+20h]. int32, damage done by the player.
inline constexpr uintptr_t kPerpCostToStateOffset = 0x20;

// AIPursuit::CalcTotalCostToState 0x40AA00, __thiscall on IPursuit*, returns uint32.
// Leaf function: only reads fields +0x13C..+0x188 of this. The HUD shows its result.
inline constexpr uintptr_t kCalcTotalCostToState = 0x0040AA00;

// ?FEDatabase@@3PAVcFrontendDatabase@@A. CurrentCash (int32) = [[0x91CF90] + 0x10] + 0xB4:
// +0x10 CurrentUserProfiles[0], +0xA8 mTheCareerSettings, +0x0C CurrentCash.
inline constexpr uintptr_t kFEDatabase       = 0x0091CF90;
inline constexpr uintptr_t kUserProfileOffset = 0x10;
inline constexpr uintptr_t kCashOffset        = 0xA8 + 0x0C;

}  // namespace addr
