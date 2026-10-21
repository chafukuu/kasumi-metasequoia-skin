#include "tests/includes/test_framework.h"
#include "window/candidate_wheel_paging.h"

namespace
{
constexpr int kNotch = 120; // WHEEL_DELTA
} // namespace

TEST_CASE(candidate_wheel_emits_one_step_per_full_notch)
{
    int accumulator = 0;

    const auto up = CandidateWheel::ConsumeWheelDelta(accumulator, kNotch, kNotch);
    REQUIRE_EQ(up.page_up, 1);
    REQUIRE_EQ(up.page_down, 0);
    REQUIRE_EQ(accumulator, 0);

    const auto down = CandidateWheel::ConsumeWheelDelta(accumulator, -kNotch, kNotch);
    REQUIRE_EQ(down.page_up, 0);
    REQUIRE_EQ(down.page_down, 1);
    REQUIRE_EQ(accumulator, 0);
}

TEST_CASE(candidate_wheel_accumulates_high_precision_deltas_across_messages)
{
    int accumulator = 0;

    // A high-precision mouse reports fractions of a notch. None of these may
    // page on their own; the sixth completes the first full notch.
    for (int i = 0; i < 5; ++i)
    {
        const auto partial = CandidateWheel::ConsumeWheelDelta(accumulator, 20, kNotch);
        REQUIRE_EQ(partial.page_up, 0);
        REQUIRE_EQ(partial.page_down, 0);
    }
    const auto completing = CandidateWheel::ConsumeWheelDelta(accumulator, 20, kNotch);
    REQUIRE_EQ(completing.page_up, 1);
    REQUIRE_EQ(accumulator, 0);
}

TEST_CASE(candidate_wheel_reports_every_notch_of_a_single_burst)
{
    int accumulator = 0;

    // A flick can arrive as one oversized delta. Every notch in it has to be
    // reported, or a fast scroll silently loses pages.
    const auto burst = CandidateWheel::ConsumeWheelDelta(accumulator, -3 * kNotch - 40, kNotch);
    REQUIRE_EQ(burst.page_down, 3);
    REQUIRE_EQ(burst.page_up, 0);
    REQUIRE_EQ(accumulator, -40);
}

TEST_CASE(candidate_wheel_drops_the_remainder_when_direction_reverses)
{
    int accumulator = 0;

    CandidateWheel::ConsumeWheelDelta(accumulator, 80, kNotch);
    REQUIRE_EQ(accumulator, 80);

    // Without the reset the leftover +80 would cancel most of the first notch
    // back down, so the reversal would need an extra notch to take effect.
    const auto reversed = CandidateWheel::ConsumeWheelDelta(accumulator, -kNotch, kNotch);
    REQUIRE_EQ(reversed.page_down, 1);
    REQUIRE_EQ(reversed.page_up, 0);
    REQUIRE_EQ(accumulator, 0);
}

TEST_CASE(candidate_wheel_ignores_a_non_positive_notch)
{
    int accumulator = 0;

    const auto steps = CandidateWheel::ConsumeWheelDelta(accumulator, kNotch, 0);
    REQUIRE_EQ(steps.page_up, 0);
    REQUIRE_EQ(steps.page_down, 0);
    REQUIRE_EQ(accumulator, 0);
}
