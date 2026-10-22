#include "Tracker.h"

#include "Check.h"

#include <climits>

using cfd::Payout;
using cfd::Tracker;
using Event = Tracker::Event;

namespace cfd {
// Found by argument-dependent lookup from CheckEqual.
std::ostream& operator<<(std::ostream& os, Tracker::Event e) {
    return os << static_cast<int>(e);
}
}  // namespace cfd

namespace {

constexpr uintptr_t kPursuitA = 0x12340000;
constexpr uintptr_t kPursuitB = 0x23450000;

void PursuitCountsFromZero() {
    Tracker t(Tracker::NewObject::CountFromZero);

    auto r = t.Sample(kPursuitA, 0);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, 0);

    r = t.Sample(kPursuitA, 250);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.before, 0);
    CHECK_EQ(r.after, 250);
    CHECK_EQ(r.delta, 250);

    r = t.Sample(kPursuitA, 250);
    CHECK_EQ(r.event, Event::None);
    CHECK_EQ(r.delta, 0);

    r = t.Sample(kPursuitA, 750);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.delta, 500);

    // A pursuit first seen with a total already pays all of it, as the HUD counts it.
    r = t.Sample(kPursuitB, 2000);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.before, 0);
    CHECK_EQ(r.delta, 2000);
}

void DropWithoutEndIsNewBaseline() {
    Tracker t(Tracker::NewObject::CountFromZero);
    t.Sample(kPursuitA, 0);
    t.Sample(kPursuitA, 5000);

    auto r = t.Sample(kPursuitA, 1000);
    CHECK_EQ(r.event, Event::Decrease);
    CHECK_EQ(r.delta, 0);

    r = t.Sample(kPursuitA, 1500);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.delta, 500);
}

void ReturnAfterGonePaysNothingTwice() {
    Tracker t(Tracker::NewObject::CountFromZero);
    t.Sample(kPursuitA, 0);
    t.Sample(kPursuitA, 3000);
    t.ObjectGone();

    auto r = t.Sample(kPursuitA, 3000);
    CHECK_EQ(r.event, Event::None);
    CHECK_EQ(r.delta, 0);

    t.ObjectGone();
    r = t.Sample(kPursuitA, 3500);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.delta, 500);
}

void ReusedAddressIsNewPursuit() {
    Tracker t(Tracker::NewObject::CountFromZero);
    t.Sample(kPursuitA, 0);
    t.Sample(kPursuitA, 30000);
    t.ObjectGone();

    // The next pursuit got the same address and has already earned 2000.
    auto r = t.Sample(kPursuitA, 2000);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, 2000);

    r = t.Sample(kPursuitA, 2250);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.delta, 250);
}

void BaselineMode() {
    Tracker t(Tracker::NewObject::TakeAsBaseline);

    auto r = t.Sample(kPursuitA, 1000);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, 0);

    r = t.Sample(kPursuitA, 1500);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.delta, 500);

    r = t.Sample(kPursuitB, 300);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, 0);

    t.ObjectGone();
    r = t.Sample(kPursuitB, 100);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, 0);
}

void ImplausibleJumpIsIgnored() {
    Tracker t(Tracker::NewObject::CountFromZero);
    t.Sample(kPursuitA, 100);

    auto r = t.Sample(kPursuitA, 100 + Tracker::kMaxDelta + 1);
    CHECK_EQ(r.event, Event::Implausible);
    CHECK_EQ(r.delta, 0);

    r = t.Sample(kPursuitA, 100 + Tracker::kMaxDelta + 51);
    CHECK_EQ(r.event, Event::Increase);
    CHECK_EQ(r.delta, 50);

    r = t.Sample(kPursuitB, Tracker::kMaxDelta + 1);
    CHECK_EQ(r.event, Event::Implausible);
    CHECK_EQ(r.delta, 0);

    // Exactly the limit is still believed.
    r = t.Sample(kPursuitA, Tracker::kMaxDelta);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, Tracker::kMaxDelta);
}

void NullObjectIsIgnored() {
    Tracker t(Tracker::NewObject::CountFromZero);
    t.ObjectGone();
    auto r = t.Sample(0, 500);
    CHECK_EQ(r.event, Event::None);
    CHECK_EQ(t.Object(), uintptr_t{0});

    r = t.Sample(kPursuitA, 500);
    CHECK_EQ(r.event, Event::NewObject);
    CHECK_EQ(r.delta, 500);
}

void PayoutWholeMultiplier() {
    Payout p(1.0);
    CHECK_EQ(p.Accrue(250), 250);
    CHECK_EQ(p.Pending(), 250);
    CHECK_EQ(p.NextChunk(), 250);
    p.Paid(250);
    CHECK_EQ(p.Pending(), 0);
    CHECK_EQ(p.Accrue(0), 0);
    CHECK_EQ(p.Accrue(-5), 0);
}

void PayoutFractionsCarryOver() {
    Payout tenth(0.1);
    CHECK_EQ(tenth.Accrue(50), 5);

    Payout p(0.3);
    CHECK_EQ(p.Accrue(1), 0);
    CHECK_EQ(p.Accrue(1), 0);
    CHECK_EQ(p.Accrue(1), 0);
    CHECK_EQ(p.Accrue(1), 1);
    CHECK(p.Carry() > 0.19 && p.Carry() < 0.21);

    Payout q(2.5);
    CHECK_EQ(q.Accrue(3), 7);
    CHECK_EQ(q.Accrue(1), 3);
    CHECK_EQ(q.Pending(), 10);

    // Over many small increases nothing is lost to rounding.
    Payout r(0.37);
    int64_t total = 0;
    for (int i = 0; i < 1000; ++i) total += r.Accrue(7);
    CHECK_EQ(total, 2590);
}

void PayoutZeroMultiplier() {
    Payout p(0.0);
    CHECK_EQ(p.Accrue(1000), 0);
    CHECK_EQ(p.Pending(), 0);
}

void PayoutLargeAmounts() {
    Payout p(1000.0);
    CHECK_EQ(p.Accrue(5'000'000), int64_t{5'000'000'000});
    CHECK_EQ(p.NextChunk(), INT_MAX);
    p.Paid(INT_MAX);
    CHECK_EQ(p.Pending(), int64_t{5'000'000'000} - INT_MAX);
    p.Discard();
    CHECK_EQ(p.Pending(), 0);
    CHECK_EQ(p.Carry(), 0.0);
}

}  // namespace

int main() {
    PursuitCountsFromZero();
    DropWithoutEndIsNewBaseline();
    ReturnAfterGonePaysNothingTwice();
    ReusedAddressIsNewPursuit();
    BaselineMode();
    ImplausibleJumpIsIgnored();
    NullObjectIsIgnored();
    PayoutWholeMultiplier();
    PayoutFractionsCarryOver();
    PayoutZeroMultiplier();
    PayoutLargeAmounts();
    return Finish("TrackerTest");
}
