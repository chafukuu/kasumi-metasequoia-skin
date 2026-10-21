#pragma once

#include <d2d1.h>

namespace msimeui
{
// Monotonic clock in milliseconds, shared by every animation so values sampled in
// one frame agree with each other.
double NowMs();

// CSS cubic-bezier() timing function. Solves the curve for x = t and returns y.
struct Easing
{
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 1.0f;
    float y2 = 1.0f;

    float operator()(float t) const;

    static constexpr Easing Linear()
    {
        return {0.0f, 0.0f, 1.0f, 1.0f};
    }
    // CSS `ease`.
    static constexpr Easing Ease()
    {
        return {0.25f, 0.1f, 0.25f, 1.0f};
    }
    static constexpr Easing EaseIn()
    {
        return {0.42f, 0.0f, 1.0f, 1.0f};
    }
    static constexpr Easing EaseOut()
    {
        return {0.0f, 0.0f, 0.58f, 1.0f};
    }
    static constexpr Easing EaseInOut()
    {
        return {0.42f, 0.0f, 0.58f, 1.0f};
    }
};

// A float that moves from one value to another over time. Sampling is pure: Value()
// can be called any number of times per frame. Retargeting starts from the value
// currently on screen, so an interrupted animation never jumps.
class Tween
{
  public:
    Tween() = default;
    explicit Tween(float value);

    // Jumps to `value` and stops any running animation.
    void Snap(float value);
    // Animates from the current on-screen value to `target`. A zero duration snaps.
    void AnimateTo(float target, double durationMs, Easing easing, double nowMs);
    void Start(float from, float to, double durationMs, Easing easing, double nowMs);

    float Value(double nowMs) const;
    float Target() const
    {
        return to_;
    }
    bool IsRunning(double nowMs) const;

  private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    double startMs_ = 0.0;
    double durationMs_ = 0.0;
    Easing easing_ = Easing::Linear();
};

float Lerp(float from, float to, float t);
D2D1_COLOR_F LerpColor(const D2D1_COLOR_F &from, const D2D1_COLOR_F &to, float t);
} // namespace msimeui
