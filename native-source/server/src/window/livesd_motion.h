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
    // Chinese candidate movement keeps its original left limit. The existing
    // right-edge margin also defines a left-edge Enter guard; display stages
    // ignore it.
    double rightBoundaryInset = 0.0;
};

struct Pose
{
    // Foot-centre position from the top-left of the stage; positive jump is upwards.
    double xDip = 0.0;
    double yDip = 0.0;
    double jumpDip = 0.0;
    // scaleX includes facing. During a turn it narrows continuously through
    // zero at each horizontal mirror handoff.
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDeg = 0.0;
    // anim001, anim002, anim003, anim004: Idle, Idle2, Happy, Miss.
    std::array<double, 4> weights{1.0, 0.0, 0.0, 0.0};
};

// Bounds of non-transparent source pixels, normalized to the image canvas.
// The host derives these from its decoded alpha masks without changing assets.
struct VisibleBounds
{
    double left = 0.0;
    double top = 0.0;
    double right = 1.0;
    double bottom = 1.0;
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
        velocityY_ = 0.0;
        xDip_ = settings_.stageWidth * 0.5;
        yDip_ = settings_.footY;
        candidateWidthDip_ = settings_.stageWidth;
        targetCandidateWidthDip_ = settings_.stageWidth;
        candidateLeftDip_ = 0.0;
        targetCandidateLeftDip_ = 0.0;
        hasCandidateWidth_ = false;
        candidateAnchorVisible_ = false;
        displayBoundaryMode_ = false;
        phase_ = 0.0;
        breathPhase_ = 0.0;
        direction_ = 1;
        directionY_ = -1;
        facing_ = 1;
        turnFrom_ = 1;
        turnTo_ = 1;
        turning_ = false;
        turnStartMs_ = nowMs;
        turnHopStarted_ = false;
        turnInterruptedEnter_ = false;
        enterActionActive_ = false;
        enterActionStartMs_ = nowMs;
        enterRecoveryActive_ = false;
        enterRecoveryStartMs_ = nowMs;
        state_ = State::Idle;
        stateStartMs_ = nowMs;
        specialUntilMs_ = nowMs;
        special_ = false;
        autoHappy_ = false;
        deferredHappy_ = false;
        weights_ = {1.0, 0.0, 0.0, 0.0};
        blendFrom_ = weights_;
        random_ = 0x4C534431U;
        nextIdleMs_ = nowMs + IdleHoldMs();
    }

    void SetCandidateAnchorVisible(bool visible)
    {
        candidateAnchorVisible_ = visible;
    }

    void SetVisibleFrameBounds(const std::array<VisibleBounds, 4> &bounds)
    {
        for (std::size_t index = 0; index < bounds.size(); ++index)
        {
            const auto &source = bounds[index];
            if (!std::isfinite(source.left) || !std::isfinite(source.top) || !std::isfinite(source.right) ||
                !std::isfinite(source.bottom))
                continue;
            VisibleBounds normalized{std::clamp(source.left, 0.0, 1.0), std::clamp(source.top, 0.0, 1.0),
                                     std::clamp(source.right, 0.0, 1.0), std::clamp(source.bottom, 0.0, 1.0)};
            if (normalized.left < normalized.right && normalized.top < normalized.bottom)
                visibleFrameBounds_[index] = normalized;
        }
    }

    // Candidate cards can grow while the transparent LiveSD host remains at
    // the skin's reserved maximum width. Smooth only the model's movement
    // boundary; never resize the HWND or recreate its D2D surface per key.
    void SetCandidateWidthDip(double widthDip)
    {
        SetCandidateIntervalDip(0.0, widthDip);
    }

    void SetCandidateIntervalDip(double leftDip, double widthDip)
    {
        if (!std::isfinite(leftDip) || !std::isfinite(widthDip) || widthDip <= 0.0)
            return;
        leftDip = std::clamp(leftDip, 0.0, (std::max)(0.0, settings_.stageWidth - 1.0));
        widthDip = std::clamp(widthDip, 1.0, settings_.stageWidth - leftDip);
        targetCandidateLeftDip_ = leftDip;
        targetCandidateWidthDip_ = widthDip;
        if (!hasCandidateWidth_)
        {
            candidateLeftDip_ = leftDip;
            candidateWidthDip_ = widthDip;
            hasCandidateWidth_ = true;
            xDip_ = std::clamp(xDip_, MovementLeft(), MovementRight());
        }
    }

    // Install the destination interval before a host-level Chinese return.
    // The host preserves the rendered position with a decaying visual offset.
    void SetCandidateIntervalImmediateDip(double leftDip, double widthDip)
    {
        if (!std::isfinite(leftDip) || !std::isfinite(widthDip) || widthDip <= 0.0)
            return;
        leftDip = std::clamp(leftDip, 0.0, (std::max)(0.0, settings_.stageWidth - 1.0));
        widthDip = std::clamp(widthDip, 1.0, settings_.stageWidth - leftDip);
        candidateLeftDip_ = targetCandidateLeftDip_ = leftDip;
        candidateWidthDip_ = targetCandidateWidthDip_ = widthDip;
        hasCandidateWidth_ = true;
        xDip_ = std::clamp(xDip_, MovementLeft(), MovementRight());
    }

    // English-mode movement follows the full display instead of the compact
    // caret/candidate stage. Keep the sprite inside the display while allowing
    // its opaque artwork to reach the screen edge.
    void SetDisplayBoundaryMode(bool enabled)
    {
        if (displayBoundaryMode_ == enabled)
            return;
        const double oldLeft = MovementLeft();
        const double oldRight = MovementRight();
        const double oldRange = (std::max)(0.0, oldRight - oldLeft);
        const double progress = oldRange > 0.01 ? std::clamp((xDip_ - oldLeft) / oldRange, 0.0, 1.0) : 0.5;
        const double oldTop = MovementTop();
        const double oldBottom = MovementBottom();
        const double oldVerticalRange = (std::max)(0.0, oldBottom - oldTop);
        const double verticalProgress =
            oldVerticalRange > 0.01 ? std::clamp((yDip_ - oldTop) / oldVerticalRange, 0.0, 1.0) : 0.5;
        displayBoundaryMode_ = enabled;
        const double newLeft = MovementLeft();
        const double newRight = MovementRight();
        xDip_ = std::clamp(newLeft + progress * (newRight - newLeft), newLeft, newRight);
        const double newTop = MovementTop();
        const double newBottom = MovementBottom();
        yDip_ = std::clamp(newTop + verticalProgress * (newBottom - newTop), newTop, newBottom);
        if (!enabled)
        {
            yDip_ = settings_.footY;
            velocityY_ = 0.0;
        }
    }

    bool DisplayBoundaryMode() const
    {
        return displayBoundaryMode_;
    }

    double StageWidthDip() const
    {
        return settings_.stageWidth;
    }

    double StageHeightDip() const
    {
        return settings_.stageHeight;
    }

    double CandidateMovementWidthDip() const
    {
        return hasCandidateWidth_ ? candidateWidthDip_ : settings_.stageWidth;
    }

    double CandidateMovementLeftDip() const
    {
        return hasCandidateWidth_ ? candidateLeftDip_ : 0.0;
    }

    double MovementCenterDip() const
    {
        return (MovementLeft() + MovementRight()) * 0.5;
    }

    // English mode can use a display-wide stage. Keep the mascot at the same
    // relative point in its available travel interval when the host resizes
    // that stage, so switching modes does not teleport it to an edge.
    void SetStageWidthDip(double widthDip)
    {
        if (!std::isfinite(widthDip))
            return;
        widthDip = std::clamp(widthDip, settings_.width, 16384.0);
        if (std::abs(settings_.stageWidth - widthDip) < 0.01)
            return;
        const double oldLeft = MovementLeft();
        const double oldRight = MovementRight();
        const double oldRange = (std::max)(0.0, oldRight - oldLeft);
        const double progress = oldRange > 0.01 ? std::clamp((xDip_ - oldLeft) / oldRange, 0.0, 1.0) : 0.5;
        settings_.stageWidth = widthDip;
        candidateWidthDip_ = (std::min)(candidateWidthDip_, widthDip);
        targetCandidateWidthDip_ = (std::min)(targetCandidateWidthDip_, widthDip);
        candidateLeftDip_ = std::clamp(candidateLeftDip_, 0.0, widthDip - candidateWidthDip_);
        targetCandidateLeftDip_ = std::clamp(targetCandidateLeftDip_, 0.0, widthDip - targetCandidateWidthDip_);
        const double newLeft = MovementLeft();
        const double newRight = MovementRight();
        xDip_ = std::clamp(newLeft + progress * (newRight - newLeft), newLeft, newRight);
    }

    void SetStageSizeDip(double widthDip, double heightDip)
    {
        if (std::isfinite(widthDip))
            SetStageWidthDip(widthDip);
        if (!std::isfinite(heightDip) || heightDip <= 0.0)
            return;
        heightDip = std::clamp(heightDip, settings_.height, 16384.0);
        if (std::abs(settings_.stageHeight - heightDip) < 0.01)
            return;
        const double oldTop = MovementTop();
        const double oldBottom = MovementBottom();
        const double oldRange = (std::max)(0.0, oldBottom - oldTop);
        const double progress = oldRange > 0.01 ? std::clamp((yDip_ - oldTop) / oldRange, 0.0, 1.0) : 0.5;
        settings_.stageHeight = heightDip;
        settings_.footY = std::clamp(settings_.footY, settings_.height, settings_.stageHeight);
        const double newTop = MovementTop();
        const double newBottom = MovementBottom();
        yDip_ = std::clamp(newTop + progress * (newBottom - newTop), newTop, newBottom);
    }

    void SetPositionDip(double xDip)
    {
        if (!std::isfinite(xDip))
            return;
        xDip_ = std::clamp(xDip, MovementLeft(), MovementRight());
        velocity_ = 0.0;
    }

    void SetPositionDip(double xDip, double yDip)
    {
        SetPositionDip(xDip);
        if (std::isfinite(yDip))
        {
            yDip_ = std::clamp(yDip, MovementTop(), MovementBottom());
            velocityY_ = 0.0;
        }
    }

    // Preserve a manually released display-stage anchor until the next
    // simulation step can apply the current pose's visible-boundary clamp.
    void SetPositionDipExact(double xDip, double yDip)
    {
        if (std::isfinite(xDip))
        {
            xDip_ = xDip;
            velocity_ = 0.0;
        }
        if (std::isfinite(yDip))
        {
            yDip_ = yDip;
            velocityY_ = 0.0;
        }
    }

    double FootYDip() const
    {
        return settings_.footY;
    }

    void SetFootYDip(double yDip)
    {
        if (!std::isfinite(yDip))
            return;
        settings_.footY = std::clamp(yDip, 0.0, settings_.stageHeight);
        if (!displayBoundaryMode_)
            yDip_ = settings_.footY;
    }

    Pose FitPoseToDisplay(Pose pose) const
    {
        // A dragged/returning pose can be outside the candidate interval, but
        // its visible pixels must still stay on the physical display surface.
        const auto bounds = ProjectVisibleOffsets(pose);
        double left = 1.0 - bounds.left;
        double right = settings_.stageWidth - 1.0 - bounds.right;
        if (left > right)
            left = right = (left + right) * 0.5;
        const auto vertical = VerticalLimits(bounds);
        pose.xDip = std::clamp(pose.xDip, left, right);
        pose.yDip = std::clamp(pose.yDip, vertical[0], vertical[1]);
        return pose;
    }

    double ClampPositionDip(double xDip) const
    {
        if (!std::isfinite(xDip))
            return xDip_;
        return std::clamp(xDip, MovementLeft(), MovementRight());
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
            if (turning_ && !enterActionActive_)
                deferredHappy_ = true;
            else
                BeginState(State::Happy, nowMs);
        }
    }

    // Enter keeps walking and pose timers running, but a boundary turn owns
    // the transform. Discard Enter during a turn; never queue it for later.
    void Enter(double nowMs)
    {
        if (!std::isfinite(nowMs) || nowMs < timeMs_)
            return;
        nowMs = SafeTime(nowMs, timeMs_);
        Advance(nowMs);
        if (turning_)
            return;
        if (ShouldSuppressEnterNearLeftBoundary())
            return;
        if (!enterActionActive_)
        {
            enterActionStartMs_ = nowMs;
            enterActionActive_ = true;
            enterRecoveryActive_ = false;
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
        Pose pose = BuildPose(timeMs_);
        if (enterActionActive_)
            FitAerialPoseToStage(&pose);
        FitVisiblePoseToStage(&pose);
        return pose;
    }

    // Keep the three flips and main flight on their authored timeline, then
    // replace the grounded tail with the next hop before releasing the turn.
    bool NeedsTurnFrameCadence() const
    {
        return turning_;
    }

  private:
    Pose BuildPose(double nowMs, bool includeEnter = true) const
    {
        const double hop = (std::max)(0.0, std::sin(phase_));
        const double landing = (std::max)(0.0, -std::sin(phase_));
        const double breath = std::sin(breathPhase_);
        double turnWidth = 1.0;
        int facing = facing_;
        double actionJump = 0.0;
        double actionScaleY = 1.0;
        double actionRotation = 0.0;
        const double turnElapsed = nowMs - turnStartMs_;
        const double turnHopBlend = turning_ ? Smooth((turnElapsed - kTurnHopStartMs) / kTurnHopBlendMs) : 0.0;
        if (turning_)
        {
            const double progress = std::clamp(turnElapsed / kTurnPresetDurationMs, 0.0, 1.0);
            const double axis = AxisFlipScaleAt(progress);
            // The preset's signed scale is relative to the direction we had
            // before turning.  Three reversals therefore land on turnTo_.
            facing = turnFrom_ * (axis < 0.0 ? -1 : 1);
            turnWidth = std::abs(axis);
            SampleAxisAction(progress, &actionJump, &actionScaleY);
        }
        if (enterActionActive_ && includeEnter)
        {
            const double progress = std::clamp((nowMs - enterActionStartMs_) / kEnterDurationMs, 0.0, 1.0);
            SampleAerialAction(progress, &actionJump, &actionScaleY, &actionRotation);
        }
        Pose pose;
        pose.xDip = xDip_;
        pose.yDip = yDip_;
        // Idle1 and Idle2 are byte-identical in the first asset pack, so a
        // frame crossfade alone is invisible. Give the mascot a small but
        // readable breathing bob even when no key pulse has arrived; typing
        // still dominates with the larger beat-driven hop below.
        const double idleBob = (1.65 + 1.15 * breath) * (1.0 - 0.7 * activity_);
        pose.jumpDip = settings_.height * 0.055 * hop * activity_ + idleBob;
        const double widthScale = 1.0 + activity_ * (0.026 * landing - 0.024 * hop);
        const double walkingWidthBlend = turning_ ? 1.0 - Smooth(turnElapsed / kTurnPoseBlendMs) + turnHopBlend : 1.0;
        // Keep a visible axis through the mirror handoff. Do not hard-clamp
        // the curve near a fixed width: that clamp introduced a discontinuous
        // width snap when entering and leaving the narrow region.
        const double minimumWidth = turning_ ? 0.18 : 0.22;
        pose.scaleX = static_cast<double>(facing) *
                      (std::max)(minimumWidth, turnWidth * (1.0 + (widthScale - 1.0) * walkingWidthBlend));
        pose.scaleY = 1.0 + activity_ * (0.035 * hop - 0.028 * landing) + 0.015 * breath * (1.0 - 0.7 * activity_);
        pose.rotationDeg = 2.2 * std::sin(phase_) * activity_ + 1.1 * breath * (1.0 - 0.7 * activity_);
        if (turning_)
        {
            if (turnElapsed >= kTurnHopStartMs)
            {
                // The landing squash becomes the next takeoff immediately.
                // Do not fade the hop itself from zero again at turn exit.
                pose.jumpDip -= idleBob * (1.0 - turnHopBlend);
                pose.scaleY = actionScaleY + (pose.scaleY - 1.0) * turnHopBlend;
                pose.rotationDeg *= turnHopBlend;
            }
            else
            {
                const double blend = Smooth(turnElapsed / kTurnPoseBlendMs);
                pose.jumpDip += (actionJump - pose.jumpDip) * blend;
                pose.scaleY += (actionScaleY - pose.scaleY) * blend;
                pose.rotationDeg = turnInterruptedEnter_ ? 0.0 : pose.rotationDeg * (1.0 - blend);
            }
        }
        if (enterActionActive_ && includeEnter)
        {
            pose.jumpDip = actionJump;
            pose.scaleY = actionScaleY;
            pose.rotationDeg = actionRotation;
            // The aerial preset keeps the sprite facing forward; preserve the
            // current side while the in-plane rotation does the work.
            pose.scaleX = static_cast<double>(facing_) * (std::max)(0.22, widthScale);
        }
        else if (enterRecoveryActive_ && includeEnter && !turning_)
        {
            // Natural Enter landing retains its separate recovery contract.
            const double blend = Smooth((nowMs - enterRecoveryStartMs_) / kEnterRecoveryMs);
            pose.jumpDip *= blend;
            pose.scaleY = 1.0 + (pose.scaleY - 1.0) * blend;
            pose.rotationDeg *= blend;
        }
        pose.weights = weights_;
        return pose;
    }

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
    static constexpr double kTurnPresetDurationMs = 900.0;
    static constexpr double kTurnHopStartMs = 630.0;
    static constexpr double kTurnHopBlendMs = 90.0;
    static constexpr double kTurnDurationMs = kTurnHopStartMs + kTurnHopBlendMs;
    static constexpr double kEnterDurationMs = 1600.0;
    static constexpr double kEnterRecoveryMs = 120.0;
    static constexpr double kTurnPoseBlendMs = 84.0;

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
        if (!std::isfinite(settings.rightBoundaryInset) || settings.rightBoundaryInset <= 0.0)
            settings.rightBoundaryInset = 0.0;
        else
            settings.rightBoundaryInset = (std::min)(settings.rightBoundaryInset, 4096.0);
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
        // Give each flip enough time to remain smooth at the host's 12 ms
        // turn cadence. The former short easing plus a hard narrow-axis clamp
        // could change width by over half a sprite in one sample.
        constexpr double start = 0.153;
        constexpr double end = 0.60;
        constexpr double window = 0.13;
        constexpr double transitionFraction = 0.95;
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
            // Preserve the flight's landing squash and release it into the
            // walking takeoff instead of spending 270 ms on a grounded tail.
            localScaleY =
                1.0 -
                landingSquash * (1.0 - Smooth((progress * kTurnPresetDurationMs - kTurnHopStartMs) / kTurnHopBlendMs));
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

    struct ProjectedBounds
    {
        double left = 1.0e9;
        double top = 1.0e9;
        double right = -1.0e9;
        double bottom = -1.0e9;
    };

    ProjectedBounds ProjectVisibleOffsets(const Pose &pose) const
    {
        const double radians = pose.rotationDeg * kPi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        ProjectedBounds projected;
        for (std::size_t index = 0; index < visibleFrameBounds_.size(); ++index)
        {
            // A crossfade can expose either source, including its outermost
            // pixels. Do not average rectangles and clip a fading pose.
            if (pose.weights[index] <= 1.0e-12)
                continue;
            const auto &bounds = visibleFrameBounds_[index];
            for (double sourceX : {bounds.left, bounds.right})
                for (double sourceY : {bounds.top, bounds.bottom})
                {
                    const double x = (sourceX - 0.5) * settings_.width * pose.scaleX;
                    const double y = (sourceY - 0.5) * settings_.height * pose.scaleY;
                    const double transformedX = x * cosine - y * sine;
                    const double transformedY = x * sine + y * cosine - pose.jumpDip - settings_.height * 0.5;
                    projected.left = (std::min)(projected.left, transformedX);
                    projected.right = (std::max)(projected.right, transformedX);
                    projected.top = (std::min)(projected.top, transformedY);
                    projected.bottom = (std::max)(projected.bottom, transformedY);
                }
        }
        return projected;
    }

    ProjectedBounds MovementVisibleOffsets(const Pose &pose) const
    {
        auto bounds = ProjectVisibleOffsets(pose);
        if (turning_ && !displayBoundaryMode_)
        {
            // Candidate stages retain the wider turn envelope while their
            // movement interval is settling. Display-wide stages use the
            // actual frame so a boundary turn cannot pull the foot inward on
            // its first frame.
            Pose envelope = pose;
            envelope.scaleX = 1.026;
            const auto forward = ProjectVisibleOffsets(envelope);
            envelope.scaleX = -1.026;
            const auto mirrored = ProjectVisibleOffsets(envelope);
            bounds.left = (std::min)({bounds.left, forward.left, mirrored.left});
            bounds.right = (std::max)({bounds.right, forward.right, mirrored.right});
        }
        return bounds;
    }

    double TravelLeft() const
    {
        return displayBoundaryMode_ ? 0.0 : CandidateMovementLeftDip();
    }

    double TravelRight() const
    {
        if (displayBoundaryMode_)
            return settings_.stageWidth;
        const double cardLeft = CandidateMovementLeftDip();
        return cardLeft + (std::max)(1.0, CandidateMovementWidthDip() - settings_.rightBoundaryInset);
    }

    double LeftBoundaryEnterGuardDip() const
    {
        if (displayBoundaryMode_)
            return 0.0;
        // Bound the guard so a narrow candidate still retains a full mascot
        // lane between its original left limit and the inset right limit.
        const double availableInset =
            CandidateMovementWidthDip() - settings_.rightBoundaryInset - settings_.width - 2.0;
        return (std::min)(settings_.rightBoundaryInset, (std::max)(0.0, availableInset));
    }

    bool ShouldSuppressEnterNearLeftBoundary() const
    {
        if (direction_ >= 0)
            return false;
        const double guard = LeftBoundaryEnterGuardDip();
        return guard > 0.0 && xDip_ - MovementLeft() <= guard;
    }

    double HorizontalPadding() const
    {
        return (std::min)(1.0, (TravelRight() - TravelLeft()) * 0.25);
    }

    std::array<double, 2> HorizontalLimits(const ProjectedBounds &bounds) const
    {
        double left = TravelLeft() + HorizontalPadding() - bounds.left;
        double right = TravelRight() - HorizontalPadding() - bounds.right;
        if (left > right)
            left = right = (left + right) * 0.5;
        return {left, right};
    }

    std::array<double, 2> VerticalLimits(const ProjectedBounds &bounds) const
    {
        const double padding = (std::min)(1.0, settings_.stageHeight * 0.25);
        double top = padding - bounds.top;
        double bottom = settings_.stageHeight - padding - bounds.bottom;
        if (top > bottom)
            top = bottom = (top + bottom) * 0.5;
        return {top, bottom};
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
        const double centreY = pose->yDip - pose->jumpDip - settings_.height * 0.5;
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
        // The GitHub fit can reach zero at a real screen edge. Preserve a
        // visible axis there; the alpha fit below translates only as needed.
        pose->scaleX = std::copysign((std::max)(0.22, (std::min)(std::abs(pose->scaleX), fit)), pose->scaleX);
    }

    void FitVisiblePoseToStage(Pose *pose) const
    {
        if (!pose)
            return;
        auto bounds = ProjectVisibleOffsets(*pose);
        const double availableWidth = TravelRight() - TravelLeft() - 2.0 * HorizontalPadding();
        const double availableHeight = settings_.stageHeight - 2.0 * (std::min)(1.0, settings_.stageHeight * 0.25);
        const double fit = (std::min)(
            {1.0, availableWidth / (bounds.right - bounds.left), availableHeight / (bounds.bottom - bounds.top)});
        if (fit < 1.0)
        {
            // Very small skins can be narrower than a rotated visible body.
            // Scale both axes together; never squash a normal Enter to zero.
            pose->scaleX *= fit;
            pose->scaleY *= fit;
            bounds = ProjectVisibleOffsets(*pose);
        }
        const auto horizontal = HorizontalLimits(bounds);
        const auto vertical = VerticalLimits(bounds);
        pose->xDip = std::clamp(pose->xDip, horizontal[0], horizontal[1]);
        pose->yDip = std::clamp(pose->yDip, vertical[0], vertical[1]);
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

    double MovementLeft() const
    {
        return HorizontalLimits(MovementVisibleOffsets(BuildPose(timeMs_, false)))[0];
    }

    double MovementRight() const
    {
        return HorizontalLimits(MovementVisibleOffsets(BuildPose(timeMs_, false)))[1];
    }

    double MovementTop() const
    {
        return displayBoundaryMode_ ? VerticalLimits(ProjectVisibleOffsets(BuildPose(timeMs_, false)))[0]
                                    : settings_.footY;
    }

    double MovementBottom() const
    {
        return displayBoundaryMode_ ? VerticalLimits(ProjectVisibleOffsets(BuildPose(timeMs_, false)))[1]
                                    : settings_.footY;
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
        turnHopStarted_ = false;
        turning_ = true;
        turnInterruptedEnter_ = enterActionActive_;
        // A moving aerial action can reach an edge. The turn immediately
        // replaces it, with no aerial tail or delayed Enter after landing.
        enterActionActive_ = false;
        enterRecoveryActive_ = false;
        velocity_ = 0.0;
    }

    void Step(double nowMs, double dtMs)
    {
        tempoMs_ = Follow(tempoMs_, targetTempoMs_, dtMs, 180.0);
        const double nextCandidateWidth = Follow(candidateWidthDip_, targetCandidateWidthDip_, dtMs, 90.0);
        // A shorter row must not drag a mascot at its right edge hundreds of
        // DIPs in one frame. Let the boundary settle at walking speed.
        candidateWidthDip_ += std::clamp(nextCandidateWidth - candidateWidthDip_,
                                         -settings_.width * 1.35 * dtMs * 0.001, settings_.width * 2.7 * dtMs * 0.001);
        const double nextCandidateLeft = Follow(candidateLeftDip_, targetCandidateLeftDip_, dtMs, 90.0);
        candidateLeftDip_ += std::clamp(nextCandidateLeft - candidateLeftDip_, -settings_.width * 2.7 * dtMs * 0.001,
                                        settings_.width * 2.7 * dtMs * 0.001);
        candidateLeftDip_ = std::clamp(candidateLeftDip_, 0.0, settings_.stageWidth - candidateWidthDip_);
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
        double phaseDtMs = dtMs;
        if (turning_ && !turnHopStarted_ && nowMs - turnStartMs_ >= kTurnHopStartMs)
        {
            phase_ = 0.0;
            phaseDtMs = nowMs - (turnStartMs_ + kTurnHopStartMs);
            turnHopStarted_ = true;
        }
        phase_ = std::fmod(phase_ + 2.0 * kPi * phaseDtMs / periodMs, 2.0 * kPi);
        breathPhase_ = std::fmod(breathPhase_ + 2.0 * kPi * dtMs / 2600.0, 2.0 * kPi);
        UpdateWeights(nowMs);
        const double pace = 0.72 + 0.36 * std::clamp((600.0 - tempoMs_) / 450.0, 0.0, 1.0);
        const double targetVelocity = static_cast<double>(direction_) * settings_.width * pace * activity_;
        velocity_ = Follow(velocity_, targetVelocity, dtMs, 145.0);
        const double targetVelocityY =
            displayBoundaryMode_ ? static_cast<double>(directionY_) * settings_.height * 0.48 * pace * activity_ : 0.0;
        velocityY_ = Follow(velocityY_, targetVelocityY, dtMs, 180.0);
        if (activity_ == 0.0 && std::abs(velocity_) < 0.001)
            velocity_ = 0.0;
        {
            xDip_ += velocity_ * dtMs * 0.001;
            if (displayBoundaryMode_)
                yDip_ += velocityY_ * dtMs * 0.001;
            else
                yDip_ = settings_.footY;
            // Test the same transformed visible pixels that are drawn this
            // frame, including breathing, typing hops and pose crossfades.
            // Resolve the model position here so Sample() does not hold a
            // moving body against an unrelated, inset foot-anchor boundary.
            const auto bounds = MovementVisibleOffsets(BuildPose(nowMs, false));
            const auto horizontal = HorizontalLimits(bounds);
            const auto vertical = VerticalLimits(bounds);
            xDip_ = std::clamp(xDip_, horizontal[0], horizontal[1]);
            if (!turning_ && std::abs(velocity_) > 1.0e-6 && horizontal[1] > horizontal[0] + 0.01)
            {
                if (xDip_ >= horizontal[1] && direction_ > 0)
                {
                    BeginTurn(-1, nowMs);
                }
                else if (xDip_ <= horizontal[0] && direction_ < 0)
                {
                    BeginTurn(1, nowMs);
                }
            }
            if (displayBoundaryMode_)
            {
                yDip_ = std::clamp(yDip_, vertical[0], vertical[1]);
                if (yDip_ >= vertical[1] && directionY_ > 0)
                {
                    directionY_ = -1;
                    velocityY_ = -std::abs(velocityY_);
                }
                else if (yDip_ <= vertical[0] && directionY_ < 0)
                {
                    directionY_ = 1;
                    velocityY_ = std::abs(velocityY_);
                }
            }
            // Starting a turn replaces the ordinary hop with its authored
            // takeoff. Contain that new pose without retriggering the turn.
            const auto turnedBounds = MovementVisibleOffsets(BuildPose(nowMs, false));
            const auto turnedHorizontal = HorizontalLimits(turnedBounds);
            xDip_ = std::clamp(xDip_, turnedHorizontal[0], turnedHorizontal[1]);
            if (displayBoundaryMode_)
            {
                const auto turnedVertical = VerticalLimits(turnedBounds);
                yDip_ = std::clamp(yDip_, turnedVertical[0], turnedVertical[1]);
            }
        }
        if (turning_ && nowMs - turnStartMs_ >= kTurnDurationMs)
        {
            facing_ = turnTo_;
            turning_ = false;
            // The next hop is already underway. Preserve its phase and full
            // amplitude; there is no grounded recovery frame after the turn.
        }
        if (enterActionActive_ && nowMs - enterActionStartMs_ >= kEnterDurationMs)
        {
            enterActionActive_ = false;
            enterRecoveryActive_ = true;
            enterRecoveryStartMs_ = enterActionStartMs_ + kEnterDurationMs;
        }
        if (enterRecoveryActive_ && nowMs - enterRecoveryStartMs_ >= kEnterRecoveryMs)
            enterRecoveryActive_ = false;
        const bool deferStateForTurn = turning_;
        const bool hasTempo = intervalCount_ >= kHappyMinimumTempoSamples;
        const bool typingFast = hasPulse_ && hasTempo && ObservedTempoMs() <= kHappyExitIntervalMs &&
                                (nowMs - lastPulseMs_) < kHappyPauseTimeoutMs;
        if (!deferStateForTurn && deferredHappy_)
        {
            deferredHappy_ = false;
            if (typingFast && !special_)
            {
                autoHappy_ = true;
                BeginState(State::Happy, nowMs);
            }
            else if (state_ != State::Happy)
                autoHappy_ = false;
        }
        if (!deferStateForTurn)
        {
            if (state_ == State::Happy && ((autoHappy_ && !typingFast) || (!autoHappy_ && nowMs >= specialUntilMs_)))
                BeginState(State::Idle, nowMs);
            else if (state_ == State::Miss && nowMs >= specialUntilMs_)
                BeginState(State::Idle, nowMs);
            if (!special_ && nowMs >= nextIdleMs_)
                BeginState(state_ == State::Idle ? State::Idle2 : State::Idle, nowMs);
        }
        UpdateWeights(nowMs);
    }

    void Advance(double nowMs)
    {
        if (nowMs <= timeMs_)
            return;
        const double elapsed = nowMs - timeMs_;
        // At most 250ms of simulation after a stall, in <=16ms substeps. The
        // model is sampled against absolute time, so this remains independent
        // of the host's render cadence while bounding catch-up work.
        const double integrate = (std::min)(elapsed, 250.0);
        if (elapsed > 1000.0)
        {
            velocity_ = 0.0;
            velocityY_ = 0.0;
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

    // Standalone motion tools use the original four 400x460 source alpha
    // rectangles. A native host replaces these with its decoded mask bounds.
    std::array<VisibleBounds, 4> visibleFrameBounds_{{
        {81.0 / 400.0, 107.0 / 460.0, 349.0 / 400.0, 424.0 / 460.0},
        {81.0 / 400.0, 107.0 / 460.0, 349.0 / 400.0, 424.0 / 460.0},
        {82.0 / 400.0, 90.0 / 460.0, 318.0 / 400.0, 411.0 / 460.0},
        {66.0 / 400.0, 78.0 / 460.0, 343.0 / 400.0, 428.0 / 460.0},
    }};

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
    double velocityY_ = 0.0;
    double xDip_ = 0.0;
    double yDip_ = 0.0;
    double candidateLeftDip_ = 0.0;
    double targetCandidateLeftDip_ = 0.0;
    double candidateWidthDip_ = 0.0;
    double targetCandidateWidthDip_ = 0.0;
    bool hasCandidateWidth_ = false;
    bool candidateAnchorVisible_ = false;
    bool displayBoundaryMode_ = false;
    double phase_ = 0.0;
    double breathPhase_ = 0.0;
    int direction_ = 1;
    int directionY_ = 1;
    int facing_ = 1;
    int turnFrom_ = 1;
    int turnTo_ = 1;
    bool turning_ = false;
    double turnStartMs_ = 0.0;
    bool turnHopStarted_ = false;
    bool turnInterruptedEnter_ = false;
    bool enterActionActive_ = false;
    double enterActionStartMs_ = 0.0;
    bool enterRecoveryActive_ = false;
    double enterRecoveryStartMs_ = 0.0;
    State state_ = State::Idle;
    double stateStartMs_ = 0.0;
    double specialUntilMs_ = 0.0;
    bool special_ = false;
    bool autoHappy_ = false;
    bool deferredHappy_ = false;
    std::array<double, 4> weights_{1.0, 0.0, 0.0, 0.0};
    std::array<double, 4> blendFrom_{1.0, 0.0, 0.0, 0.0};
    std::uint32_t random_ = 0;
    double nextIdleMs_ = 0.0;
};
} // namespace LiveSDMotion
