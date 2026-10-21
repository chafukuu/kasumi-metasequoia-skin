#pragma once

namespace CandidateWheel
{
// Whole notches taken out of the accumulator, split by direction. Only one of
// the two is ever non-zero for a single message.
struct PagingSteps
{
    int page_up = 0;
    int page_down = 0;
};

// Folds one raw WM_MOUSEWHEEL delta into `accumulator` and takes out the whole
// notches it completes. High-precision mice and trackpads report deltas smaller
// than a notch, so the remainder has to survive across messages; `notch` is
// WHEEL_DELTA for real input and a plain number in tests.
constexpr PagingSteps ConsumeWheelDelta(int &accumulator, int delta, int notch)
{
    PagingSteps steps;
    if (notch <= 0)
    {
        return steps;
    }
    // A reversal means the user changed direction. Carrying the old remainder
    // over would eat part of the first notch of the new direction.
    if ((accumulator > 0 && delta < 0) || (accumulator < 0 && delta > 0))
    {
        accumulator = 0;
    }
    accumulator += delta;
    while (accumulator >= notch)
    {
        accumulator -= notch;
        ++steps.page_up;
    }
    while (accumulator <= -notch)
    {
        accumulator += notch;
        ++steps.page_down;
    }
    return steps;
}
} // namespace CandidateWheel
