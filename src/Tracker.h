#pragma once

// Pure payout logic, no WinAPI, so it can be unit-tested on its own.

#include <cstdint>

namespace cfd {

// Turns periodic samples of a counter that belongs to some game object into increases to
// pay for. The object is identified by its address.
class Tracker {
public:
    enum class NewObject {
        CountFromZero,   // the whole first value is an increase (a new pursuit, like the HUD)
        TakeAsBaseline,  // the first value is only a baseline
    };

    enum class Event {
        None,         // same object, same value
        Increase,     // same object, value grew: pay `delta`
        NewObject,    // first sample of an object; `delta` > 0 only with CountFromZero
        Decrease,     // same object, value dropped: new baseline, nothing paid
        Implausible,  // jump larger than kMaxDelta: new baseline, nothing paid
    };

    struct Result {
        Event event = Event::None;
        int64_t before = 0;
        int64_t after = 0;
        int64_t delta = 0;
    };

    // A pursuit's total grows by a few thousand per poll at most. Anything this large is a
    // misread of memory that was being freed or reused.
    static constexpr int64_t kMaxDelta = 10'000'000;

    explicit Tracker(NewObject newObject) : newObject_(newObject) {}

    Result Sample(uintptr_t object, int64_t value);

    // The tracked object is known to be gone (the player has no pursuit). It stays remembered,
    // so a sample of the same object afterwards pays nothing twice; but if that sample has
    // dropped, the address was reused by a new object.
    void ObjectGone() { gone_ = object_ != 0; }

    uintptr_t Object() const { return object_; }
    int64_t Value() const { return value_; }

private:
    NewObject newObject_;
    uintptr_t object_ = 0;
    int64_t value_ = 0;
    bool gone_ = false;
};

// Converts increases into whole dollars. Fractions carry over between payouts, so a small
// multiplier still pays in full eventually. Dollars that could not be written yet stay
// pending until the next attempt.
class Payout {
public:
    explicit Payout(double multiplier) : multiplier_(multiplier) {}

    // Returns the whole dollars this increase added to the pending amount.
    int64_t Accrue(int64_t delta);

    int64_t Pending() const { return pending_; }
    double Carry() const { return carry_; }
    // The next amount to write, at most INT32_MAX.
    int32_t NextChunk() const;
    void Paid(int64_t amount);
    // Forgets everything owed, e.g. when cash is already at INT32_MAX.
    void Discard();

private:
    double multiplier_;
    double carry_ = 0.0;
    int64_t pending_ = 0;
};

}  // namespace cfd
