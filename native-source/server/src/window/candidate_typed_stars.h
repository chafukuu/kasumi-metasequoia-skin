#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

// Pure decorative state. Times are caller-supplied monotonic milliseconds;
// coordinates are card-local DIPs, independent of layout, DPI and input state.
namespace CandidateTypedStars
{
enum class Color
{
    Yellow,
    Pink,
    Blue
};

struct Settings
{
    double originX = 66.52;
    double originY = 22.57;
    double minX = 8.0;
    double maxX = 107.0;
    double minY = -100.0;
    double maxY = 132.0;
    double widthMin = 19.0;
    double widthMax = 23.0;
    double riseMin = 24.0;
    double riseMax = 38.0;
    double endXMin = 18.0;
    double endXMax = 94.0;
    double endYMin = 96.0;
    double endYMax = 108.0;
    double durationMinMs = 1200.0;
    double durationMaxMs = 1450.0;
    double fadeInMinMs = 90.0;
    double fadeInMaxMs = 130.0;
    double fadeOutMinMs = 260.0;
    double fadeOutMaxMs = 360.0;
    double rotationMinDeg = -24.0;
    double rotationMaxDeg = 24.0;
    double spinMinDeg = -70.0;
    double spinMaxDeg = 70.0;
    double pairSeparation = 22.0;
    double yellowAspectRatio = 114.0 / 136.0;
    double pinkAspectRatio = 114.0 / 136.0;
    double blueAspectRatio = 114.0 / 136.0;
    bool blueEnabled = false;
};

struct Sample
{
    // Bitmap center; rotation is about this point.
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rotationDeg = 0.0;
    double opacity = 0.0;
    Color color = Color::Yellow;
};

class System
{
  public:
    static constexpr std::size_t kCapacity = 64;
    static constexpr double kAspectRatio = 114.0 / 136.0;
    static constexpr double kFrameMs = 1000.0 / 60.0;

    explicit System(std::uint32_t seed) : random_(seed), paletteRandom_(seed ^ 0x9e3779b9U)
    {
        particles_.reserve(kCapacity);
    }

    // Validate the complete geometry before accepting an event. PNG aspect
    // ratios come from the decoded images; they are never guessed or stretched.
    static bool IsValidSettings(const Settings &settings)
    {
        const double values[] = {settings.originX,
                                 settings.originY,
                                 settings.minX,
                                 settings.maxX,
                                 settings.minY,
                                 settings.maxY,
                                 settings.widthMin,
                                 settings.widthMax,
                                 settings.riseMin,
                                 settings.riseMax,
                                 settings.endXMin,
                                 settings.endXMax,
                                 settings.endYMin,
                                 settings.endYMax,
                                 settings.durationMinMs,
                                 settings.durationMaxMs,
                                 settings.fadeInMinMs,
                                 settings.fadeInMaxMs,
                                 settings.fadeOutMinMs,
                                 settings.fadeOutMaxMs,
                                 settings.rotationMinDeg,
                                 settings.rotationMaxDeg,
                                 settings.spinMinDeg,
                                 settings.spinMaxDeg,
                                 settings.pairSeparation,
                                 settings.yellowAspectRatio,
                                 settings.pinkAspectRatio,
                                 settings.blueAspectRatio};
        for (double value : values)
            if (!std::isfinite(value))
                return false;
        if (settings.minX >= settings.maxX || settings.minY >= settings.maxY ||
            !std::isfinite(settings.maxX - settings.minX) || !std::isfinite(settings.maxY - settings.minY) ||
            settings.widthMin <= 0.0 || settings.widthMin > settings.widthMax || settings.riseMin <= 0.0 ||
            settings.riseMin > settings.riseMax || settings.endXMin > settings.endXMax ||
            settings.endYMin > settings.endYMax || settings.endYMin < settings.originY ||
            settings.durationMinMs <= 0.0 || settings.durationMinMs > settings.durationMaxMs ||
            settings.fadeInMinMs <= 0.0 || settings.fadeInMinMs > settings.fadeInMaxMs ||
            settings.fadeOutMinMs <= 0.0 || settings.fadeOutMinMs > settings.fadeOutMaxMs ||
            settings.fadeInMaxMs + settings.fadeOutMaxMs > settings.durationMinMs ||
            settings.rotationMinDeg > settings.rotationMaxDeg || settings.spinMinDeg > settings.spinMaxDeg ||
            !std::isfinite(settings.rotationMaxDeg - settings.rotationMinDeg) ||
            !std::isfinite(settings.spinMaxDeg - settings.spinMinDeg) ||
            !std::isfinite(settings.rotationMinDeg + settings.spinMinDeg) ||
            !std::isfinite(settings.rotationMaxDeg + settings.spinMaxDeg) || settings.pairSeparation <= 0.0 ||
            settings.yellowAspectRatio <= 0.0 || settings.pinkAspectRatio <= 0.0 || settings.blueAspectRatio <= 0.0)
            return false;
        const double shortestFadeIn = settings.fadeInMinMs / settings.durationMaxMs;
        const double shortestFadeOut = settings.fadeOutMinMs / settings.durationMaxMs;
        if (shortestFadeIn <= 0.0 || shortestFadeIn >= 1.0 || shortestFadeOut <= 0.0 || shortestFadeOut >= 1.0 ||
            1.0 - shortestFadeOut >= 1.0)
            return false;
        const double radius = SafetyRadius(settings);
        if (!std::isfinite(radius) || settings.originX < settings.minX + radius ||
            settings.originX > settings.maxX - radius || settings.originY < settings.minY + radius ||
            settings.originY > settings.maxY - radius || settings.originY - settings.riseMax < settings.minY + radius ||
            settings.endYMin < settings.minY + radius || settings.endYMax > settings.maxY - radius)
            return false;
        const double left = (std::max)(settings.endXMin, settings.minX + radius);
        const double right = (std::min)(settings.endXMax, settings.maxX - radius);
        if (right - left < settings.pairSeparation)
            return false;
        const double drop = settings.endYMax - settings.originY;
        const double rootSum = std::sqrt(settings.riseMax) + std::sqrt(settings.riseMax + drop);
        return std::isfinite(rootSum * rootSum);
    }

    void Configure(const Settings &settings)
    {
        settings_ = settings;
        paletteCursor_ = palette_.size();
        Reset();
    }

    void Reset()
    {
        particles_.clear();
    }

    void SpawnPair(double nowMs)
    {
        if (!std::isfinite(nowMs) || !IsValidSettings(settings_) || !std::isfinite(nowMs + settings_.durationMaxMs) ||
            nowMs + settings_.durationMinMs <= nowMs)
            return;
        particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                        [nowMs](const Particle &particle) { return nowMs >= particle.endMs; }),
                         particles_.end());

        // Reserve the maximum rotated rectangle, including the original image
        // aspect ratio, before choosing either endpoint. No per-frame clipping
        // or adjustment can bend an otherwise ballistic flight.
        const double radius = SafetyRadius(settings_);
        const double left = (std::max)(settings_.endXMin, settings_.minX + radius);
        const double right = (std::min)(settings_.endXMax, settings_.maxX - radius);

        double endX1 = Uniform(left, right);
        double endX2 = endX1;
        for (int attempt = 0; attempt < 32 && std::abs(endX2 - endX1) < settings_.pairSeparation; ++attempt)
            endX2 = Uniform(left, right);
        if (std::abs(endX2 - endX1) < settings_.pairSeparation)
        {
            // A bounded fallback keeps every event a distinct two-star pair.
            endX1 = Uniform(left, right - settings_.pairSeparation);
            endX2 = Uniform(endX1 + settings_.pairSeparation, right);
            if (Uniform(0.0, 1.0) < 0.5)
                std::swap(endX1, endX2);
        }
        const bool firstPink = Uniform(0.0, 1.0) < 0.5;
        // Keep the legacy motion RNG sequence unchanged when enabling the new palette.
        const Color firstColor = settings_.blueEnabled ? NextColor() : firstPink ? Color::Pink : Color::Yellow;
        const Color secondColor = settings_.blueEnabled ? NextColor() : firstPink ? Color::Yellow : Color::Pink;
        Particle first = MakeParticle(nowMs, endX1, firstColor);
        Particle second = MakeParticle(nowMs, endX2, secondColor);
        while (particles_.size() + 2 > kCapacity)
            particles_.erase(particles_.begin(), particles_.begin() + 2);
        particles_.push_back(first);
        particles_.push_back(second);
    }

    std::vector<Sample> Sample(double nowMs) const
    {
        std::vector<CandidateTypedStars::Sample> samples;
        if (!std::isfinite(nowMs))
            return samples;
        samples.reserve(particles_.size());
        for (const Particle &particle : particles_)
        {
            if (nowMs < particle.startMs || nowMs >= particle.endMs)
                continue;
            const double u = (nowMs - particle.startMs) / (particle.endMs - particle.startMs);
            const double x = settings_.originX + (particle.endX - settings_.originX) * u;
            const double y = settings_.originY + u * (particle.verticalLinear + particle.verticalQuadratic * u);
            const double alphaIn = SmoothStep(u / particle.fadeInU);
            const double alphaOut = 1.0 - SmoothStep((u - particle.fadeOutU) / (1.0 - particle.fadeOutU));
            samples.push_back({x, y, particle.width, particle.height, particle.rotation + particle.spin * u,
                               alphaIn * alphaOut, particle.color});
        }
        return samples;
    }

    bool HasLive(double nowMs) const
    {
        if (!std::isfinite(nowMs))
            return false;
        return std::any_of(particles_.begin(), particles_.end(), [nowMs](const Particle &particle) {
            return nowMs >= particle.startMs && nowMs < particle.endMs;
        });
    }

    std::optional<double> NextFrameMs(double nowMs) const
    {
        if (!HasLive(nowMs))
            return std::nullopt;
        double nextMs = nowMs + kFrameMs;
        for (const Particle &particle : particles_)
            if (nowMs >= particle.startMs && nowMs < particle.endMs)
                nextMs = (std::min)(nextMs, particle.endMs);
        return nextMs;
    }

  private:
    struct Particle
    {
        double startMs;
        double endMs;
        double endX;
        double width;
        double height;
        double verticalLinear;
        double verticalQuadratic;
        double fadeInU;
        double fadeOutU;
        double rotation;
        double spin;
        Color color;
    };

    static double SmoothStep(double value)
    {
        const double t = (std::clamp)(value, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    double Uniform(double low, double high)
    {
        return std::uniform_real_distribution<double>(low, high)(random_);
    }

    static double SafetyRadius(const Settings &settings)
    {
        double aspect = (std::min)(settings.yellowAspectRatio, settings.pinkAspectRatio);
        if (settings.blueEnabled)
            aspect = (std::min)(aspect, settings.blueAspectRatio);
        return 0.5 * std::hypot(settings.widthMax, settings.widthMax / aspect);
    }

    Color NextColor()
    {
        if (paletteCursor_ == palette_.size())
        {
            // Every completed bag emits exactly two blue, one pink and one yellow.
            palette_ = {Color::Blue, Color::Blue, Color::Pink, Color::Yellow};
            std::shuffle(palette_.begin(), palette_.end(), paletteRandom_);
            paletteCursor_ = 0;
        }
        return palette_[paletteCursor_++];
    }

    Particle MakeParticle(double nowMs, double endX, Color color)
    {
        const double duration = Uniform(settings_.durationMinMs, settings_.durationMaxMs);
        const double rise = Uniform(settings_.riseMin, settings_.riseMax);
        const double drop = Uniform(settings_.endYMin, settings_.endYMax) - settings_.originY;
        // Given rise r and final drop d, solve the upward initial velocity and
        // downward acceleration. Both are constant for the whole lifetime.
        const double rootRise = std::sqrt(rise);
        const double rootSum = rootRise + std::sqrt(rise + drop);
        const double width = Uniform(settings_.widthMin, settings_.widthMax);
        const double aspect = color == Color::Blue   ? settings_.blueAspectRatio
                              : color == Color::Pink ? settings_.pinkAspectRatio
                                                     : settings_.yellowAspectRatio;
        return {nowMs,
                nowMs + duration,
                endX,
                width,
                width / aspect,
                -2.0 * rootRise * rootSum,
                rootSum * rootSum,
                Uniform(settings_.fadeInMinMs, settings_.fadeInMaxMs) / duration,
                1.0 - Uniform(settings_.fadeOutMinMs, settings_.fadeOutMaxMs) / duration,
                Uniform(settings_.rotationMinDeg, settings_.rotationMaxDeg),
                Uniform(settings_.spinMinDeg, settings_.spinMaxDeg),
                color};
    }

    Settings settings_;
    std::mt19937 random_;
    std::mt19937 paletteRandom_;
    std::array<Color, 4> palette_{};
    std::size_t paletteCursor_ = palette_.size();
    std::vector<Particle> particles_;
};
} // namespace CandidateTypedStars
