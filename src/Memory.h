#pragma once

// Guarded access to game memory. Every function catches access violations with SEH and
// reports failure instead of crashing the game. The game's main thread keeps running while
// we read, so a pointer that was valid a moment ago may already be freed.

#include <cstddef>
#include <cstdint>

namespace mem {

// User-mode, 4-byte aligned and below 2 GB (speed.exe is not Large Address Aware).
bool IsPlausiblePointer(uintptr_t p);

bool Read(uintptr_t address, void* out, size_t size);
bool ReadU32(uintptr_t address, uint32_t& out);
// ReadU32 plus IsPlausiblePointer on the result.
bool ReadPointer(uintptr_t address, uintptr_t& out);

// Calls AIPursuit::CalcTotalCostToState on an IPursuit*.
bool CallCalcTotalCostToState(uintptr_t pursuit, uint32_t& out);

// Atomically adds `amount` (>= 0) to the int32 at `address`, saturating at INT32_MAX.
bool AddInt32Saturating(uintptr_t address, int32_t amount, int32_t& before, int32_t& after);

}  // namespace mem
