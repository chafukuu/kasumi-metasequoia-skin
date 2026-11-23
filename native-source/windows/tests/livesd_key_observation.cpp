#include "Key/LiveSDKeyObservationGuard.h"

#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint32_t A = 'A', B = 'B';
constexpr std::uint64_t FirstDown = 1, RepeatDown = (std::uint64_t{1} << 30) | 1;

struct Observations
{
    LiveSDKeyObservationGuard guard;
    unsigned emitted = 0;
    std::array<unsigned, 256> perKey{};

    void Test(std::uint32_t key, std::uint32_t time, std::uint64_t data = FirstDown)
    {
        Record(key, guard.Observe(key, time, data, false));
    }

    void Actual(std::uint32_t key, std::uint32_t time, std::uint64_t data = FirstDown)
    {
        Record(key, guard.Observe(key, time, data, true));
    }

    void Record(std::uint32_t key, bool emit)
    {
        if (emit)
        {
            ++emitted;
            if (key < perKey.size())
                ++perKey[key];
        }
    }
};

void Expect(unsigned actual, unsigned expected, const char *description)
{
    if (actual != expected)
        throw std::runtime_error(std::string(description) + ": expected " + std::to_string(expected) + ", got " +
                                 std::to_string(actual));
}
} // namespace

int main()
{
    const std::vector<std::pair<const char *, std::function<void()>>> cases = {
        {"test_actual_with_changed_time_and_data_counts_once",
         [] {
             Observations events;
             events.Test(A, 100, 1);
             events.Actual(A, 900, 0xfeed);
             Expect(events.emitted, 1, "one character despite changed callback metadata");
         }},
        {"repeated_test_queries_count_once",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Test(A, 100);
             events.Test(A, 100);
             events.Actual(A, 101, RepeatDown);
             Expect(events.emitted, 1, "three queries and one actual action");
         }},
        {"interleaved_keys_keep_independent_actual_pairs",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Test(B, 101);
             events.Actual(A, 999, 7);
             events.Actual(B, 999, 7);
             Expect(events.emitted, 2, "two interleaved character keys");
             Expect(events.perKey[A], 1, "A must not be recounted after B");
             Expect(events.perKey[B], 1, "B must be paired once");
         }},
        {"test_queries_are_deduplicated_across_other_keys",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Test(B, 101);
             events.Test(A, 100);
             events.Actual(A, 800, 3);
             events.Actual(B, 801, 4);
             Expect(events.emitted, 2, "interleaved repeated query is not a new key");
         }},
        {"multiple_repeat_tests_pair_with_identical_actual_metadata",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Test(A, 120, RepeatDown);
             events.Test(A, 140, RepeatDown);
             events.Test(A, 160, RepeatDown);
             for (unsigned i = 0; i < 4; ++i)
                 events.Actual(A, 900, 0x1234);
             events.Actual(A, 900, 0x1234);
             Expect(events.emitted, 5, "four paired actions then a real direct action with identical metadata");
         }},
        {"actual_only_host_keeps_every_real_action",
         [] {
             Observations events;
             events.Actual(A, 100);
             events.Actual(A, 101, RepeatDown);
             events.Actual(A, 101, RepeatDown);
             events.Actual(B, 101, RepeatDown);
             Expect(events.emitted, 4, "actual actions do not become timestamp debounce queries");
             Expect(events.perKey[A], 3, "all three actual A actions");
         }},
        {"test_only_passthrough_host_counts_fresh_key_presses",
         [] {
             Observations events;
             for (unsigned i = 0; i < 4; ++i)
             {
                 events.Test(A, 100);
                 events.guard.KeyUp(A);
             }
             Expect(events.emitted, 4, "four English passthrough presses with no actual callback");
         }},
        {"test_only_auto_repeat_is_not_debounced",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Test(A, 125, RepeatDown);
             events.Test(A, 150, RepeatDown);
             events.Test(A, 175, RepeatDown);
             Expect(events.emitted, 4, "distinct repeat messages must all count");
         }},
        {"keyup_allows_same_tick_same_key_fresh_press",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Actual(A, 300, 7);
             events.guard.KeyUp(A);
             events.Test(A, 100);
             events.Actual(A, 300, 7);
             Expect(events.emitted, 2, "keyup separates otherwise identical physical presses");
         }},
        {"keyup_clears_unmatched_passthrough_test",
         [] {
             Observations events;
             events.Test(A, 100);
             events.guard.KeyUp(A);
             events.Actual(A, 100);
             Expect(events.emitted, 2, "fresh direct action cannot consume old passthrough query");
         }},
        {"other_keyup_does_not_clear_pending_pairs",
         [] {
             Observations events;
             events.Test(A, 100);
             events.guard.KeyUp(B);
             events.Actual(A, 900, 7);
             Expect(events.emitted, 1, "unrelated keyup preserves the A pair");
         }},
        {"repeat_flag_can_identify_a_distinct_same_tick_test",
         [] {
             Observations events;
             events.Test(A, 100, FirstDown);
             events.Test(A, 100, RepeatDown);
             events.Actual(A, 500, 0);
             events.Actual(A, 500, 0);
             Expect(events.emitted, 2, "same-tick initial press and repeat");
         }},
        {"timestamp_wrap_does_not_merge_two_keys",
         [] {
             Observations events;
             events.Test(A, 0xffffffffU, RepeatDown);
             events.Actual(A, 17, 0);
             events.Test(A, 0, RepeatDown);
             events.Actual(A, 17, 0);
             Expect(events.emitted, 2, "GetMessageTime wrap");
         }},
        {"keyup_and_reset_handle_boundaries_without_touching_other_keys",
         [] {
             Observations events;
             events.Test(0, 10);
             events.Test(255, 10);
             events.Test(256, 10);
             events.Test(0xffffffffU, 10);
             events.guard.KeyUp(256);
             events.guard.KeyUp(0xffffffffU);
             events.Actual(0, 11, 9);
             events.Actual(255, 11, 9);
             events.Actual(256, 11, 9);
             events.Actual(0xffffffffU, 11, 9);
             Expect(events.emitted, 2, "only representable virtual keys emit");
             events.guard.KeyUp(255);
             events.Test(255, 10);
             Expect(events.emitted, 3, "highest valid key can start again");
         }},
        {"test_signature_preserves_all_key_data_bits",
         [] {
             Observations events;
             events.Test(A, 100, std::uint64_t{1} << 60);
             events.Test(A, 100, std::uint64_t{2} << 60);
             events.Actual(A, 200, 0);
             events.Actual(A, 200, 0);
             Expect(events.emitted, 2, "different 64-bit key data remains distinct");
         }},
        {"reset_clears_all_signatures_and_unmatched_pairs",
         [] {
             Observations events;
             events.Test(A, 100);
             events.Test(B, 101);
             events.guard.Reset();
             events.Actual(A, 100);
             events.Test(B, 101);
             events.Actual(B, 900, 7);
             Expect(events.emitted, 4, "new lifecycle starts with no old pending actions or signatures");
             Expect(events.perKey[A], 2, "A direct action after reset");
             Expect(events.perKey[B], 2, "B query after reset");
         }},
        {"rapid_same_key_presses_are_never_time_debounced",
         [] {
             Observations events;
             for (unsigned i = 0; i < 100; ++i)
             {
                 events.Test(A, 100);
                 events.Actual(A, 100, 0);
                 events.guard.KeyUp(A);
             }
             Expect(events.emitted, 100, "100 distinct presses sharing metadata");
         }},
    };

    unsigned failures = 0;
    for (const auto &test : cases)
    {
        try
        {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << "RESULT cases=" << cases.size() << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
