#include "msimeui/Animation.h"

#include <algorithm>
#include <cmath>
#include <windows.h>

namespace msimeui
{
namespace
{
float BezierComponent(float a1, float a2, float t)
{
    // B(t) for a curve anchored at 0 and 1 with control points a1 and a2.
    const float u = 1.0f - t;
    return 3.0f * u * u * t * a1 + 3.0f * u * t * t * a2 + t * t * t;
}

float BezierSlope(float a1, float a2, float t)
{
    const float u = 1.0f - t;
    return 3.0f * u * u * a1 + 6.0f * u * t * (a2 - a1) + 3.0f * t * t * (1.0f - a2);
}
} // namespace

double NowMs()
{
    static const double frequency = [] {
        LARGE_INTEGER value = {};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / frequency;
}

float Easing::operator()(float t) const
{
    t = std::clamp(t, 0.0f, 1.0f);
    if (x1 == y1 && x2 == y2)
    {
        return t;
    }

    // Newton-Raphson for the curve parameter whose x equals t, bisection as fallback.
    float s = t;
    for (int i = 0; i < 8; ++i)
    {
        const float x = BezierComponent(x1, x2, s) - t;
        if (std::fabs(x) < 1e-5f)
        {
            return BezierComponent(y1, y2, s);
        }
        const float slope = BezierSlope(x1, x2, s);
        if (std::fabs(slope) < 1e-6f)
        {
            break;
        }
        s -= x / slope;
    }

    float low = 0.0f;
    float high = 1.0f;
    s = t;
    for (int i = 0; i < 24; ++i)
    {
        const float x = BezierComponent(x1, x2, s);
        if (std::fabs(x - t) < 1e-5f)
        {
            break;
        }
        if (x < t)
        {
            low = s;
        }
        else
        {
            high = s;
        }
        s = (low + high) * 0.5f;
    }
    return BezierComponent(y1, y2, s);
}

Tween::Tween(float value) : from_(value), to_(value)
{
}

void Tween::Snap(float value)
{
    from_ = value;
    to_ = value;
    durationMs_ = 0.0;
}

void Tween::AnimateTo(float target, double durationMs, Easing easing, double nowMs)
{
    if (target == to_)
    {
        return;
    }
    Start(Value(nowMs), target, durationMs, easing, nowMs);
}

void Tween::Start(float from, float to, double durationMs, Easing easing, double nowMs)
{
    if (durationMs <= 0.0 || from == to)
    {
        Snap(to);
        return;
    }
    from_ = from;
    to_ = to;
    startMs_ = nowMs;
    durationMs_ = durationMs;
    easing_ = easing;
}

float Tween::Value(double nowMs) const
{
    if (durationMs_ <= 0.0)
    {
        return to_;
    }
    const double elapsed = nowMs - startMs_;
    if (elapsed >= durationMs_)
    {
        return to_;
    }
    if (elapsed <= 0.0)
    {
        return from_;
    }
    return Lerp(from_, to_, easing_(static_cast<float>(elapsed / durationMs_)));
}

bool Tween::IsRunning(double nowMs) const
{
    return durationMs_ > 0.0 && nowMs - startMs_ < durationMs_;
}

float Lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

D2D1_COLOR_F LerpColor(const D2D1_COLOR_F &from, const D2D1_COLOR_F &to, float t)
{
    return D2D1::ColorF(Lerp(from.r, to.r, t), Lerp(from.g, to.g, t), Lerp(from.b, to.b, t), Lerp(from.a, to.a, t));
}
} // namespace msimeui
