#pragma once

#include <algorithm>
#include <cmath>

// Decoration state is independent of movement cadence, visibility and Enter
// animation arbitration. Only an explicit reset key clears a running combo.
namespace LiveSDCombo
{
class Counter
{
  public:
    static constexpr unsigned Maximum = 999;
    unsigned Count() const
    {
        return count_;
    }

    unsigned DisplayCount() const
    {
        return displayCount_;
    }

    double Opacity(double nowMs, bool animationsEnabled) const
    {
        if (!animationsEnabled)
            return count_ >= 2 ? 1.0 : 0.0;
        if (fadeDurationMs_ <= 0.0)
            return fadeTarget_;
        const double elapsed = nowMs - fadeStartMs_;
        if (!std::isfinite(elapsed))
            return fadeTarget_;
        const double t = std::clamp(elapsed / fadeDurationMs_, 0.0, 1.0);
        const double smooth = t * t * (3.0 - 2.0 * t);
        return fadeFrom_ + (fadeTarget_ - fadeFrom_) * smooth;
    }

    void Increment(double nowMs)
    {
        if (count_ < Maximum)
        {
            ++count_;
            if (count_ >= 2)
            {
                displayCount_ = count_;
                incrementMs_ = nowMs;
                SetVisible(true, nowMs);
            }
        }
    }

    void Reset(double nowMs)
    {
        count_ = 0;
        SetVisible(false, nowMs);
    }

    double Scale(double nowMs, bool animationsEnabled) const
    {
        const double elapsed = nowMs - incrementMs_;
        if (!animationsEnabled || !std::isfinite(elapsed) || elapsed < 0.0 || elapsed >= 140.0)
            return 1.0;
        const double t = elapsed / 140.0;
        const double phase = t < 0.42 ? t / 0.42 : (1.0 - t) / 0.58;
        const double smooth = phase * phase * (3.0 - 2.0 * phase);
        return 1.0 + 0.18 * smooth;
    }

  private:
    void SetVisible(bool visible, double nowMs)
    {
        const double target = visible ? 1.0 : 0.0;
        if (target == fadeTarget_)
            return;
        fadeFrom_ = Opacity(nowMs, true);
        fadeTarget_ = target;
        fadeStartMs_ = nowMs;
        fadeDurationMs_ = visible ? 160.0 : 180.0;
    }

    unsigned count_ = 0;
    unsigned displayCount_ = 0;
    double incrementMs_ = -1.0e15;
    double fadeFrom_ = 0.0;
    double fadeTarget_ = 0.0;
    double fadeStartMs_ = 0.0;
    double fadeDurationMs_ = 0.0;
};
} // namespace LiveSDCombo
