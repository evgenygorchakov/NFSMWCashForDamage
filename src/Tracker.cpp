#include "Tracker.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace cfd {

Tracker::Result Tracker::Sample(uintptr_t object, int64_t value) {
    Result r;
    if (object == 0) return r;

    const bool reused = gone_ && object == object_ && value < value_;
    gone_ = false;

    if (object != object_ || reused) {
        object_ = object;
        value_ = value;
        r.event = Event::NewObject;
        r.before = newObject_ == NewObject::CountFromZero ? 0 : value;
        r.after = value;
        if (r.after - r.before > kMaxDelta) {
            r.event = Event::Implausible;
        } else {
            r.delta = std::max<int64_t>(0, r.after - r.before);
        }
        return r;
    }

    r.before = value_;
    r.after = value;
    if (value > value_) {
        if (value - value_ > kMaxDelta) {
            r.event = Event::Implausible;
        } else {
            r.event = Event::Increase;
            r.delta = value - value_;
        }
    } else if (value < value_) {
        r.event = Event::Decrease;
    }
    value_ = value;
    return r;
}

int64_t Payout::Accrue(int64_t delta) {
    if (delta <= 0 || multiplier_ <= 0.0) return 0;
    carry_ += static_cast<double>(delta) * multiplier_;
    // The epsilon keeps 0.1 * 50 = 4.9999999 from losing a dollar to rounding.
    const double whole = std::floor(carry_ + 1e-6);
    if (whole < 1.0) return 0;
    carry_ = std::max(0.0, carry_ - whole);
    const auto dollars = static_cast<int64_t>(whole);
    pending_ += dollars;
    return dollars;
}

int32_t Payout::NextChunk() const {
    return static_cast<int32_t>(std::min<int64_t>(pending_, INT_MAX));
}

void Payout::Paid(int64_t amount) {
    pending_ = std::max<int64_t>(0, pending_ - amount);
}

void Payout::Discard() {
    pending_ = 0;
    carry_ = 0.0;
}

}  // namespace cfd
