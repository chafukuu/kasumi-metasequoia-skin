#pragma once

#include <array>
#include <cstdint>
#include <limits>

// TSF's test and actual callbacks describe one key-down. A host can pump
// messages or adjust lParam between them, so message equality alone cannot
// pair the callbacks. Keep independent outstanding tests for each virtual key.
class LiveSDKeyObservationGuard
{
  public:
    bool Observe(std::uint32_t key, std::uint32_t messageTime, std::uint64_t keyData, bool actualKeyDown)
    {
        if (key >= keys_.size())
            return false;
        auto &state = keys_[key];
        if (!actualKeyDown)
        {
            if (state.hasTest && state.testTime == messageTime && state.testData == keyData)
                return false;
            state.hasTest = true;
            state.testTime = messageTime;
            state.testData = keyData;
            if (state.pendingTests != (std::numeric_limits<std::uint32_t>::max)())
                ++state.pendingTests;
            return true;
        }
        // Actual callbacks are actions, not repeated preflight queries. Two
        // actual events can share incidental metadata after a message pump.
        if (state.pendingTests)
        {
            --state.pendingTests;
            return false;
        }
        // A few hosts offer only OnKeyDown. Preserve their observation path.
        return true;
    }

    void KeyUp(std::uint32_t key)
    {
        if (key < keys_.size())
            keys_[key] = {};
    }

    void Reset()
    {
        keys_ = {};
    }

  private:
    struct State
    {
        std::uint64_t testData = 0;
        std::uint32_t testTime = 0;
        std::uint32_t pendingTests = 0;
        bool hasTest = false;
    };
    std::array<State, 256> keys_{};
};
