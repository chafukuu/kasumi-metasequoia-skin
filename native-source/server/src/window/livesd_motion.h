#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// A deterministic presentation model. The host owns event meaning, visibility,
// the monotonic clock, image loading and rendering; no input text is retained.
namespace LiveSDMotion
{
enum class State
{
    Idle = 0,
    Idle2 = 1,
    Happy = 2,
    Miss = 3,
};

struct Settings
{
    double stageWidth = 520.0;
    double width = 161.28;
    double height = 185.472;
    // The current skin uses a 246 DIP stage and a 233 DIP foot anchor.  They
    // are optional so older aggregate initializers keep their previous ABI.
    double stageHeight = 246.0;
    double footY = 233.0;
};

struct Pose
{
    // Foot-centre X from the left of the stage; positive jump is upwards.
    double xDip = 0.0;
    double jumpDip = 0.0;
    // scaleX includes facing. During a turn its magnitude never reaches zero.
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDeg = 0.0;
    // anim001, anim002, anim003, anim004: Idle, Idle2, Happy, Miss.
    std::array<double, 4> weights{1.0, 0.0, 0.0, 0.0};
};

class Model
{
  public:
    explicit Model(Settings settings = {}) : settings_(Sanitize(settings))
    {
        Reset(0.0);
    }

    void Reset(double nowMs)
    {
        nowMs = SafeTime(nowMs, 0.0);
        timeMs_ = nowMs;
        lastPulseMs_ = nowMs;
        hasPulse_ = false;
        intervalCount_ = 0;
        intervalCursor_ = 0;
        intervals_.fill(250.0);
        tempoMs_ = 250.0;
        targetTempoMs_ = 250.0;
        continuousPulses_ = 0;
        activity_ = 0.0;
        velocity_ = 0.0;
        xDip_ = settings_.stageWidth * 0.5;
        candidateAnchorVisible_ = false;
        phase_ = 0.0;
        breathPhase_ = 0.0;
        direction_ = 1;
        facing_ = 1;
        turnFrom_ = 1;
        turnTo_ = 1;
        turning_ = false;
        turnStartMs_ = nowMs;
        enterActionActive_ = false;
        enterActionStartMs_ = nowMs;
        state_ = State::Idle;
        stateStartMs_ = nowMs;
        specialUntilMs_ = nowMs;
        special_ = false;
        autoHappy_ = false;
        weights_ = {1.0, 0.0, 0.0, 0.0};
        blendFrom_ = weights_;
        random_ = 0x4C534431U;
        nextIdleMs_ = nowMs + IdleHoldMs();
    }

    // A visible candidate card gives the mascot slightly more travel room to
    // the left. Keep the English/caret-only stage symmetric.
    void SetCandidateAnchorVisible(bool visible)
    {
        candidateAnchorVisible_ = visible;
    }

    // Call only for an accepted insertion. Deletion and cancellation do not
    // imply Miss. Cadence-bearing packets can update speed even when Server
    // receipt times tie; packets without cadence are coalesced at same-tick.
    void Pulse(double nowMs, std::uint32_t typingIntervalMs = 0)
    {
        if (!std::isfinite(nowMs) || nowMs < timeMs_)
            return;
        nowMs = SafeTime(nowMs, timeMs_);
        Advance(nowMs);
        if (hasPulse_ && nowMs == lastPulseMs_ && typingIntervalMs == 0)
            return;
        if (hasPulse_)
        {
            if (typingIntervalMs >= kPhraseBreakMs)
            {
                // A real pause starts a fresh phrase. Same-tick pulses still
                // contribute to activity but cannot establish a fast tempo.
                intervalCount_ = 0;
                intervalCursor_ = 0;
                targetTempoMs_ = 250.0;
                continuousPulses_ = 0;
            }
            else if (typingIntervalMs >= kMinimumTempoSampleMs)
            {
                // This interval was measured at TSF key-observation time,
                // before asynchronous caret reads and pipe batching.
                intervals_[intervalCursor_] =
                    static_cast<double>((std::min)(typingIntervalMs, static_cast<std::uint32_t>(1200)));
                intervalCursor_ = (intervalCursor_ + 1) % intervals_.size();
                intervalCount_ = (std::min)(intervalCount_ + 1, intervals_.size());
                targetTempoMs_ = ObservedTempoMs();
            }
        }
        hasPulse_ = true;
        lastPulseMs_ = nowMs;
        continuousPulses_ = (std::min)(continuousPulses_ + 1U, 1000000U);
        // Require several measured intervals before comparing typing speed;
        // this avoids treating the default tempo as an immediate fast input.
        if (!special_ && continuousPulses_ >= kHappyMinimumPulses && intervalCount_ >= kHappyMinimumTempoSamples &&
            ObservedTempoMs() <= kHappyEnterIntervalMs)
        {
            autoHappy_ = true;
            BeginState(State::Happy, nowMs);
        }
    }

    // Trigger the 1.6s high-jump aerial two-turn preset.  Repeated Enter
    // observations while an action is already in flight are coalesced so a
    // delayed duplicate key notification cannot restart the visible motion.
    void Enter(double nowMs)
    {
        if (!std::isfinite(nowMs) || nowMs < timeMs_)
            return;
        nowMs = SafeTime(nowMs, timeMs_);
        Advance(nowMs);
        if (!enterActionActive_)
        {
            enterActionStartMs_ = nowMs;
            enterActionActive_ = true;
        }
    }

    // Explicit four-state control is useful to a debug host; it never creates
    // input or changes the inferred tempo. Reasserting a held state is a no-op.
    void SetState(State state, double nowMs)
    {
        if (!std::isfinite(nowMs) || nowMs < timeMs_)
            return;
        nowMs = SafeTime(nowMs, timeMs_);
        Advance(nowMs);
        const int index = static_cast<int>(state);
        if (index < 0 || index > 3)
            state = State::Idle;
        BeginState(state, nowMs);
    }

    Pose Sample(double nowMs)
    {
        Advance(SafeTime(nowMs, timeMs_));
        const double hop = (std::max)(0.0, std::sin(phase_));
        const double landing = (std::max)(0.0, -std::sin(phase_));
        const double breath = std::sin(breathPhase_);
        double turnWidth = 1.0;
        int facing = facing_;
        double actionJump = 0.0;
        double actionScaleY = 1.0;
        double actionRotation = 0.0;
        if (turning_)
        {
            const double progress = std::clamp((timeMs_ - turnStartMs_) / kTurnDurationMs, 0.0, 1.0);
            const double axis = AxisFlipScaleAt(progress);
            // The preset's signed scale is relative to the direction we had
            // before turning.  Three reversals therefore land on turnTo_.
            facing = turnFrom_ * (axis < 0.0 ? -1 : 1);
            turnWidth = std::abs(axis);
            SampleAxisAction(progress, &actionJump, &actionScaleY);
        }
        if (enterActionActive_)
        {
            const double progress = std::clamp((timeMs_ - enterActionStartMs_) / kEnterDurationMs, 0.0, 1.0);
            SampleAerialAction(progress, &actionJump, &actionScaleY, &actionRotation);
        }
        Pose pose;
        pose.xDip = xDip_;
        // Idle1 and Idle2 are byte-identical in the first asset pack, so a
        // frame crossfade alone is invisible. Give the mascot a small but
        // readable breathing bob even when no key pulse has arrived; typing
        // still dominates with the larger beat-driven hop below.
        const double idleBob = (1.65 + 1.15 * breath) * (1.0 - 0.7 * activity_);
        pose.jumpDip = settings_.height * 0.055 * hop * activity_ + idleBob;
        const double widthScale = 1.0 + activity_ * (0.026 * landing - 0.024 * hop);
        // The imported axis-flip preset intentionally narrows to 0.18. Keep
        // ordinary direction changes at the historical 0.22 safety floor.
        const double minimumWidth = turning_ ? 0.18 : 0.22;
        pose.scaleX = static_cast<double>(facing) * (std::max)(minimumWidth, turnWidth * widthScale);
        pose.scaleY = 1.0 + activity_ * (0.035 * hop - 0.028 * landing) + 0.015 * breath * (1.0 - 0.7 * activity_);
        pose.rotationDeg = 2.2 * std::sin(phase_) * activity_ + 1.1 * breath * (1.0 - 0.7 * activity_);
        if (turning_)
        {
            pose.jumpDip = actionJump;
            pose.scaleY = actionScaleY;
            pose.rotationDeg = 0.0;
        }
        if (enterActionActive_)
        {
            pose.jumpDip = actionJump;
            pose.scaleY = actionScaleY;
            pose.rotationDeg = actionRotation;
            // The aerial preset keeps the sprite facing forward; preserve the
            // current side while the in-plane rotation does the work.
            pose.scaleX = static_cast<double>(facing_) * (std::max)(0.22, widthScale);
            FitAerialPoseToStage(&pose);
        }
        pose.weights = weights_;
        return pose;
    }

    // 0.9 s at 12 ms preserves the approximately 75 rendered turn samples
    // the previous 1.2 s / 16 ms timeline displayed. The host remains in
    // charge of scheduling and may still coalesce a delayed frame.
    bool NeedsTurnFrameCadence() const
    {
        return turning_;
    }

  private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr std::uint32_t kMinimumTempoSampleMs = 20;
    static constexpr double kPhraseBreakMs = 1000.0;
    static constexpr double kHappyEnterIntervalMs = 460.0;
    static constexpr double kHappyExitIntervalMs = 700.0;
    static constexpr double kHappyPauseTimeoutMs = 1000.0;
    static constexpr unsigned kHappyMinimumPulses = 4U;
    static constexpr std::size_t kHappyMinimumTempoSamples = 3U;
    // Timings and shape constants are copied from motion-presets.  The
    // source preset is authored in 400x460 sprite pixels; the jump is capped
    // to the available stage headroom after conversion to DIP.
    static constexpr double kTurnDurationMs = 900.0;
    static constexpr double kEnterDurationMs = 1600.0;
    static constexpr double kCandidateLeftExpansionDip = 8.0;

    static double SafeTime(double value, double fallback)
    {
        return std::isfinite(value) ? std::clamp(value, -1.0e15, 1.0e15) : fallback;
    }

    static Settings Sanitize(Settings settings)
    {
        if (!std::isfinite(settings.stageWidth) || settings.stageWidth <= 0.0)
            settings.stageWidth = 520.0;
        settings.stageWidth = std::clamp(settings.stageWidth, 1.0, 16384.0);
        if (!std::isfinite(settings.width) || settings.width <= 0.0)
            settings.width = 161.28;
        settings.width = std::clamp(settings.width, 1.0, settings.stageWidth);
        if (!std::isfinite(settings.height) || settings.height <= 0.0)
            settings.height = 185.472;
        settings.height = std::clamp(settings.height, 1.0, 4096.0);
        if (!std::isfinite(settings.stageHeight) || settings.stageHeight <= 0.0)
            settings.stageHeight = 246.0;
        settings.stageHeight = std::clamp(settings.stageHeight, 1.0, 16384.0);
        if (!std::isfinite(settings.footY) || settings.footY <= 0.0)
            settings.footY = settings.stageHeight - 13.0;
        settings.footY = std::clamp(settings.footY, settings.height, settings.stageHeight);
        return settings;
    }

    static double Smooth(double value)
    {
        value = std::clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
    }

    static double Follow(double value, double target, double dtMs, double timeConstantMs)
    {
        return target + (value - target) * std::exp(-dtMs / timeConstantMs);
    }

    double AxisActionJumpPeakDip() const
    {
        // 220/210 source pixels map through the sprite scale, but the stage
        // only has footY-height DIP above the unjumped canvas.  Keep every
        // action frame inside the configured stage instead of clipping the
        // head when a skin uses the current 246 DIP canvas.
        const double headroom = (std::max)(0.0, settings_.footY - settings_.height);
        return (std::min)(headroom, settings_.height * 0.26);
    }

    double AerialActionJumpPeakDip() const
    {
        // At the apex the imported spin stretches vertically by 4.5%. Reserve
        // that extra height before lifting the feet so an upright 360-degree
        // frame stays inside the canvas as well.
        const double headroom = (std::max)(0.0, settings_.footY - settings_.height * 1.045);
        return (std::min)(headroom, settings_.height * 0.26);
    }

    static double AxisFlipScaleAt(double progress)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        constexpr int count = 3;
        constexpr double start = 0.153;
        constexpr double end = 0.60;
        constexpr double window = 0.095;
        constexpr double transitionFraction = 0.62;
        const double slot = (end - start) / count;
        const double transition = (std::min)(window, slot * transitionFraction);
        double scale = progress < start ? 1.0 : (count % 2 ? -1.0 : 1.0);
        for (int index = 0; index < count; ++index)
        {
            const double flipStart = start + index * slot;
            const double flipEnd = flipStart + transition;
            const double before = index % 2 ? -1.0 : 1.0;
            const double after = -before;
            if (progress < flipStart)
            {
                scale = before;
                break;
            }
            if (progress < flipEnd)
            {
                const double p = Smooth((progress - flipStart) / transition);
                scale = before + (after - before) * p;
                break;
            }
            scale = after;
        }
        constexpr double minimum = 0.18;
        // With the shortened 0.9 s timeline, a normal display cadence can
        // otherwise sample on opposite sides of the mirror axis while both
        // poses are visibly wide. Hold the authored narrow width through the
        // axis neighbourhood so every signed handoff remains continuous.
        constexpr double narrowHold = 0.42;
        if (std::abs(scale) < narrowHold)
            scale = scale < 0.0 ? -minimum : minimum;
        return scale;
    }

    void SampleAxisAction(double progress, double *jump, double *scaleY) const
    {
        if (!jump || !scaleY)
            return;
        constexpr double takeoffEnd = 0.08;
        constexpr double flightEnd = 0.70;
        constexpr double landingSquash = 0.16;
        double localJump = 0.0;
        double localScaleY = 1.0;
        if (progress < takeoffEnd)
        {
            const double takeoff = progress / takeoffEnd;
            localScaleY = 1.0 - 0.12 * std::sin(kPi * takeoff);
        }
        else if (progress < flightEnd)
        {
            const double air = (progress - takeoffEnd) / (flightEnd - takeoffEnd);
            localJump = AxisActionJumpPeakDip() * std::pow(std::sin(kPi * air), 1.8);
            localScaleY = 1.0 - landingSquash * Smooth((air - 0.88) / 0.12);
        }
        else
        {
            const double elapsed = progress - flightEnd;
            const double landingDuration = (std::max)(0.001, 1.0 - flightEnd);
            const double durationSeconds = kTurnDurationMs * 0.001;
            const double decay = 0.22 * durationSeconds / 4.8;
            localJump =
                12.0 * std::exp(-elapsed / decay) * std::sin(kPi * std::clamp(elapsed / landingDuration, 0.0, 1.0));
            localScaleY = 1.0 - landingSquash * std::exp(-elapsed / (0.20 * durationSeconds / 4.8)) *
                                    std::sin(kPi * std::clamp(elapsed / landingDuration, 0.0, 1.0));
        }
        *jump = (std::max)(0.0, localJump);
        *scaleY = localScaleY;
    }

    void SampleAerialAction(double progress, double *jump, double *scaleY, double *rotation) const
    {
        if (!jump || !scaleY || !rotation)
            return;
        constexpr double duration = 1.6;
        constexpr double ratio = duration / 4.8;
        constexpr double anticipationEnd = 0.45 * ratio;
        constexpr double flightEnd = 2.55 * ratio;
        constexpr double landingEnd = 3.10 * ratio;
        constexpr double landingSquash = 0.14;
        double localJump = 0.0;
        double localScaleY = 1.0;
        double localRotation = 0.0;
        const double time = std::clamp(progress, 0.0, 1.0) * duration;
        if (time < anticipationEnd)
        {
            const double anticipation = time / anticipationEnd;
            localScaleY = 1.0 - 0.12 * std::sin(kPi * anticipation);
        }
        else if (time < flightEnd)
        {
            const double air = (time - anticipationEnd) / (flightEnd - anticipationEnd);
            const double arc = 4.0 * air * (1.0 - air);
            localJump = AerialActionJumpPeakDip() * arc;
            localRotation = 360.0 * 2.0 * Smooth(air);
            localScaleY = 1.0 + 0.045 * std::sin(kPi * air);
        }
        else if (time < landingEnd)
        {
            const double landing = Smooth((time - flightEnd) / (landingEnd - flightEnd));
            localRotation = 720.0;
            localScaleY = 1.0 - landingSquash * std::sin(kPi * landing) * std::exp(-1.8 * landing);
        }
        else
        {
            const double tail = std::clamp((time - landingEnd) / (duration - landingEnd), 0.0, 1.0);
            const double idlePhase = 0.5 + 0.5 * std::sin(2.0 * kPi * tail - kPi / 2.0);
            (void)idlePhase;
            localJump = 1.5 * std::pow(std::sin(kPi * tail), 2.0);
            localRotation = 720.0;
        }
        *jump = (std::max)(0.0, localJump);
        *scaleY = localScaleY;
        *rotation = localRotation;
    }

    void FitAerialPoseToStage(Pose *pose) const
    {
        if (!pose)
            return;
        // The fixed LiveSD stage is deliberately compact.  A rotated rectangle
        // is wider/taller than the upright sprite, especially during the high
        // point of the Enter action. Narrow the horizontal axis only as needed
        // to keep its projected source rectangle on the transparent stage. The
        // factor is continuous in jump height and rotation, so no clipped frame
        // or one-frame resize is exposed to the compositor.
        const double radians = pose->rotationDeg * kPi / 180.0;
        const double halfWidth = settings_.width * 0.5;
        const double halfHeight = settings_.height * 0.5 * std::abs(pose->scaleY);
        const double centreX = pose->xDip;
        const double centreY = settings_.footY - pose->jumpDip - settings_.height * 0.5;
        const double horizontalMargin = (std::max)(0.0, (std::min)(centreX, settings_.stageWidth - centreX));
        const double verticalMargin = (std::max)(0.0, (std::min)(centreY, settings_.stageHeight - centreY));
        const double absCos = std::abs(std::cos(radians));
        const double absSin = std::abs(std::sin(radians));
        const double horizontalRoom = horizontalMargin - absSin * halfHeight;
        const double verticalRoom = verticalMargin - absCos * halfHeight;
        const double byHorizontal = absCos * halfWidth > 0.0 ? horizontalRoom / (absCos * halfWidth) : 1.0;
        const double byVertical = absSin * halfWidth > 0.0 ? verticalRoom / (absSin * halfWidth) : 1.0;
        // Leave a sub-pixel numerical margin so an exact tangent does not turn
        // into a negative coordinate after the Direct2D double-to-float path.
        const double fit = std::clamp((std::min)(byHorizontal, byVertical), 0.0, 1.0) * (1.0 - 1.0e-12);
        pose->scaleX = std::copysign((std::min)(std::abs(pose->scaleX), fit), pose->scaleX);
    }

    double ObservedTempoMs() const
    {
        if (intervalCount_ == 0)
            return 1200.0;
        auto sorted = intervals_;
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(intervalCount_));
        const std::size_t middle = intervalCount_ / 2;
        if ((intervalCount_ & 1U) != 0U)
            return sorted[middle];
        return (sorted[middle - 1] + sorted[middle]) * 0.5;
    }

    double IdleHoldMs()
    {
        // Fixed-seed, per-model variation. Draw once per idle transition, not
        // per frame or key, so the same call sequence is exactly repeatable.
        random_ ^= random_ << 13U;
        random_ ^= random_ >> 17U;
        random_ ^= random_ << 5U;
        const double relaxed = 340.0 + 280.0 * static_cast<double>(random_ & 0xFFFFU) / 65535.0;
        const double performing = std::clamp(tempoMs_ * 0.75, 120.0, 360.0);
        return relaxed + (performing - relaxed) * activity_;
    }

    double Inset() const
    {
        // Reserve the projected full canvas at maximum sway/width, plus a
        // small gap. Contain/transparent image margins remain the host's job.
        return (std::min)(settings_.stageWidth * 0.5, 4.0 + settings_.width * 0.54 + settings_.height * 0.06);
    }

    void UpdateWeights(double nowMs)
    {
        const double blend = Smooth((nowMs - stateStartMs_) / 90.0);
        const std::size_t target = static_cast<std::size_t>(state_);
        double sum = 0.0;
        for (std::size_t index = 0; index < weights_.size(); ++index)
        {
            weights_[index] = (std::max)(0.0, blendFrom_[index] * (1.0 - blend) + (index == target ? blend : 0.0));
            sum += weights_[index];
        }
        for (double &weight : weights_)
            weight /= sum;
    }

    void BeginState(State state, double nowMs)
    {
        if (state == state_)
            return;
        UpdateWeights(nowMs);
        blendFrom_ = weights_;
        state_ = state;
        if (state != State::Happy)
            autoHappy_ = false;
        stateStartMs_ = nowMs;
        special_ = state == State::Happy || state == State::Miss;
        specialUntilMs_ = nowMs + (state == State::Happy ? 1300.0 : 900.0);
        if (!special_)
            nextIdleMs_ = nowMs + IdleHoldMs();
    }

    void BeginTurn(int direction, double nowMs)
    {
        if (direction == direction_)
            return;
        direction_ = direction;
        turnFrom_ = facing_;
        turnTo_ = direction;
        turnStartMs_ = nowMs;
        turning_ = true;
        velocity_ = 0.0;
    }

    void Step(double nowMs, double dtMs)
    {
        tempoMs_ = Follow(tempoMs_, targetTempoMs_, dtMs, 180.0);
        double targetActivity = 0.0;
        if (hasPulse_)
        {
            const double age = (std::max)(0.0, nowMs - lastPulseMs_);
            const double sustain = std::clamp(tempoMs_ * 1.6, 220.0, 800.0);
            const double stop = std::clamp(tempoMs_ * 3.2 + 350.0, 900.0, 1700.0);
            const double phraseStrength = 0.35 + 0.65 * (std::min)(static_cast<double>(continuousPulses_) / 4.0, 1.0);
            targetActivity = phraseStrength * (1.0 - Smooth((age - sustain) / (stop - sustain)));
        }
        activity_ = Follow(activity_, targetActivity, dtMs, targetActivity > activity_ ? 85.0 : 260.0);
        if (activity_ < 0.00001)
            activity_ = 0.0;
        const double periodMs = std::clamp(tempoMs_ * 2.0, 280.0, 900.0);
        phase_ = std::fmod(phase_ + 2.0 * kPi * dtMs / periodMs, 2.0 * kPi);
        breathPhase_ = std::fmod(breathPhase_ + 2.0 * kPi * dtMs / 2600.0, 2.0 * kPi);
        const double pace = 0.72 + 0.36 * std::clamp((600.0 - tempoMs_) / 450.0, 0.0, 1.0);
        const double targetVelocity = static_cast<double>(direction_) * settings_.width * pace * activity_;
        velocity_ = Follow(velocity_, targetVelocity, dtMs, 145.0);
        if (activity_ == 0.0 && std::abs(velocity_) < 0.001)
            velocity_ = 0.0;
        const double baseInset = Inset();
        const double left = (std::max)(0.0, baseInset - (candidateAnchorVisible_ ? kCandidateLeftExpansionDip : 0.0));
        const double right = settings_.stageWidth - baseInset;
        xDip_ = std::clamp(xDip_ + velocity_ * dtMs * 0.001, left, right);
        if (right > left + 0.01)
        {
            if (xDip_ >= right && direction_ > 0)
                BeginTurn(-1, nowMs);
            else if (xDip_ <= left && direction_ < 0)
                BeginTurn(1, nowMs);
        }
        if (turning_ && nowMs - turnStartMs_ >= kTurnDurationMs)
        {
            facing_ = turnTo_;
            turning_ = false;
        }
        if (enterActionActive_ && nowMs - enterActionStartMs_ >= kEnterDurationMs)
            enterActionActive_ = false;
        const bool hasTempo = intervalCount_ >= kHappyMinimumTempoSamples;
        const bool typingFast = hasPulse_ && hasTempo && ObservedTempoMs() <= kHappyExitIntervalMs &&
                                (nowMs - lastPulseMs_) < kHappyPauseTimeoutMs;
        if (state_ == State::Happy && ((autoHappy_ && !typingFast) || (!autoHappy_ && nowMs >= specialUntilMs_)))
            BeginState(State::Idle, nowMs);
        else if (state_ == State::Miss && nowMs >= specialUntilMs_)
            BeginState(State::Idle, nowMs);
        if (!special_ && nowMs >= nextIdleMs_)
            BeginState(state_ == State::Idle ? State::Idle2 : State::Idle, nowMs);
        UpdateWeights(nowMs);
    }

    void Advance(double nowMs)
    {
        if (nowMs <= timeMs_)
            return;
        const double elapsed = nowMs - timeMs_;
        // At most 250ms of simulation after a stall, in <=16ms substeps. We
        // use the current absolute event age so a long pause cannot replay a
        // backlog of movement, hops, auto poses or idle changes.
        const double integrate = (std::min)(elapsed, 250.0);
        if (elapsed > 1000.0)
        {
            velocity_ = 0.0;
            activity_ = 0.0;
        }
        double cursor = nowMs - integrate;
        while (cursor < nowMs)
        {
            const double dt = (std::min)(16.0, nowMs - cursor);
            cursor += dt;
            Step(cursor, dt);
        }
        timeMs_ = nowMs;
    }

    Settings settings_;
    double timeMs_ = 0.0;
    double lastPulseMs_ = 0.0;
    bool hasPulse_ = false;
    std::array<double, 5> intervals_{};
    std::size_t intervalCount_ = 0;
    std::size_t intervalCursor_ = 0;
    double tempoMs_ = 250.0;
    double targetTempoMs_ = 250.0;
    unsigned continuousPulses_ = 0;
    double activity_ = 0.0;
    double velocity_ = 0.0;
    double xDip_ = 0.0;
    bool candidateAnchorVisible_ = false;
    double phase_ = 0.0;
    double breathPhase_ = 0.0;
    int direction_ = 1;
    int facing_ = 1;
    int turnFrom_ = 1;
    int turnTo_ = 1;
    bool turning_ = false;
    double turnStartMs_ = 0.0;
    bool enterActionActive_ = false;
    double enterActionStartMs_ = 0.0;
    State state_ = State::Idle;
    double stateStartMs_ = 0.0;
    double specialUntilMs_ = 0.0;
    bool special_ = false;
    bool autoHappy_ = false;
    std::array<double, 4> weights_{1.0, 0.0, 0.0, 0.0};
    std::array<double, 4> blendFrom_{1.0, 0.0, 0.0, 0.0};
    std::uint32_t random_ = 0;
    double nextIdleMs_ = 0.0;
};
} // namespace LiveSDMotion
