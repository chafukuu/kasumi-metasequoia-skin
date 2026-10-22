#pragma once

#include "skin/candidate_skin_catalog.h"
#include "window/candidate_typed_stars.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Layout.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

// Input events are consumed by the presenter. This visual only owns decorative state and draws it.
class CandidateTypedStarOverlay : public msimeui::Visual
{
  public:
    using Clock = msimeui::DeviceResources::AnimationClock;

    CandidateTypedStarOverlay() : particles_(RuntimeSeed())
    {
    }

    void Configure(const std::optional<CandidateSkinCatalog::TypedStars> &config, const std::wstring &root)
    {
        enabled_ = config.has_value();
        Reset();
        if (!enabled_)
            return;
        settings_ = {config->originXDip, config->originYDip, config->minXDip,
                     config->maxXDip,    config->minYDip,    config->maxYDip};
        settings_.widthMin = config->widthMinDip;
        settings_.widthMax = config->widthMaxDip;
        settings_.riseMin = config->riseMinDip;
        settings_.riseMax = config->riseMaxDip;
        settings_.endXMin = config->endXMinDip;
        settings_.endXMax = config->endXMaxDip;
        settings_.endYMin = config->endYMinDip;
        settings_.endYMax = config->endYMaxDip;
        settings_.durationMinMs = config->durationMinMs;
        settings_.durationMaxMs = config->durationMaxMs;
        settings_.fadeInMinMs = config->fadeInMinMs;
        settings_.fadeInMaxMs = config->fadeInMaxMs;
        settings_.fadeOutMinMs = config->fadeOutMinMs;
        settings_.fadeOutMaxMs = config->fadeOutMaxMs;
        settings_.rotationMinDeg = config->rotationMinDeg;
        settings_.rotationMaxDeg = config->rotationMaxDeg;
        settings_.spinMinDeg = config->spinMinDeg;
        settings_.spinMaxDeg = config->spinMaxDeg;
        settings_.pairSeparation = config->pairSeparationDip;
        yellow_ = root + std::filesystem::u8path(config->yellowImage).wstring();
        pink_ = root + std::filesystem::u8path(config->pinkImage).wstring();
        const auto yellowAspect = PngAspectRatio(yellow_);
        const auto pinkAspect = PngAspectRatio(pink_);
        if (!yellowAspect || !pinkAspect)
        {
            enabled_ = false;
            return;
        }
        settings_.yellowAspectRatio = *yellowAspect;
        settings_.pinkAspectRatio = *pinkAspect;
        settings_.blueEnabled = !config->blueImage.empty();
        blue_.clear();
        if (settings_.blueEnabled)
        {
            blue_ = root + std::filesystem::u8path(config->blueImage).wstring();
            const auto blueAspect = PngAspectRatio(blue_);
            if (!blueAspect)
            {
                enabled_ = false;
                return;
            }
            settings_.blueAspectRatio = *blueAspect;
        }
        enabled_ = CandidateTypedStars::System::IsValidSettings(settings_);
        if (enabled_)
            particles_.Configure(settings_);
    }

    void SetTime(Clock::time_point time)
    {
        time_ = time;
    }

    const CandidateTypedStars::Settings &Parameters() const
    {
        return settings_;
    }

    void SpawnPairs(std::uint64_t count, Clock::time_point now)
    {
        if (!enabled_)
            return;
        time_ = now;
        // Capacity is bounded in the model; stale batches cannot stall the UI thread.
        count = (std::min)(count, static_cast<std::uint64_t>(CandidateTypedStars::System::kCapacity / 2));
        for (std::uint64_t i = 0; i < count; ++i)
            particles_.SpawnPair(Milliseconds(now));
        if (count)
            InvalidateVisual();
    }

    void Reset()
    {
        particles_.Reset();
    }

    std::optional<Clock::time_point> NextFrameDeadline() const
    {
        if (!enabled_)
            return std::nullopt;
        const auto next = particles_.NextFrameMs(Milliseconds(time_));
        if (!next)
            return std::nullopt;
        return Clock::time_point(
            std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(*next)));
    }

    msimeui::SizeF Measure(const msimeui::SizeF &) override
    {
        return {0.0f, 0.0f};
    }

    void Arrange(const msimeui::RectF &rect) override
    {
        bounds_ = rect;
    }

    bool HitTest(const msimeui::PointF &) const override
    {
        return false;
    }

    void Render(msimeui::DeviceResources &resources) override
    {
        if (!enabled_ || !resources.AreAnimationsEnabled())
        {
            Reset();
            return;
        }
        auto *target = resources.GetRenderTarget();
        if (!target || bounds_.width <= 0.0f || bounds_.height <= 0.0f)
            return;
        target->PushAxisAlignedClip(
            D2D1::RectF(bounds_.x + static_cast<float>(settings_.minX), bounds_.y + static_cast<float>(settings_.minY),
                        bounds_.x + static_cast<float>(settings_.maxX), bounds_.y + static_cast<float>(settings_.maxY)),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        D2D1_MATRIX_3X2_F transform;
        target->GetTransform(&transform);
        for (const auto &sample : particles_.Sample(Milliseconds(time_)))
        {
            if (sample.opacity <= 0.0)
                continue;
            D2D1_SIZE_F sourceSize{};
            const auto &path = sample.color == CandidateTypedStars::Color::Blue   ? blue_
                               : sample.color == CandidateTypedStars::Color::Pink ? pink_
                                                                                  : yellow_;
            auto *bitmap = resources.GetBitmapFromFile(path, &sourceSize);
            if (!bitmap || sourceSize.width <= 0.0f || sourceSize.height <= 0.0f)
                continue;
            const float x = bounds_.x + static_cast<float>(sample.x);
            const float y = bounds_.y + static_cast<float>(sample.y);
            const float width = static_cast<float>(sample.width);
            const float height = static_cast<float>(sample.height);
            target->SetTransform(
                D2D1::Matrix3x2F::Rotation(static_cast<float>(sample.rotationDeg), D2D1::Point2F(x, y)) * transform);
            target->DrawBitmap(bitmap,
                               D2D1::RectF(x - width / 2.0f, y - height / 2.0f, x + width / 2.0f, y + height / 2.0f),
                               static_cast<float>(sample.opacity), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        target->SetTransform(transform);
        target->PopAxisAlignedClip();
    }

  private:
    // Read only PNG metadata here; DeviceResources remains the sole bitmap decoder.
    // All configured colors may use different image proportions without stretching or unsafe clipping.
    static std::optional<double> PngAspectRatio(const std::wstring &path)
    {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        std::array<unsigned char, 24> bytes{};
        if (!file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
            return std::nullopt;
        constexpr std::array<unsigned char, 16> header{137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82};
        if (!std::equal(header.begin(), header.end(), bytes.begin()))
            return std::nullopt;
        const auto integer = [&bytes](std::size_t at) {
            return (static_cast<std::uint32_t>(bytes[at]) << 24) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
                   (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
        };
        const auto width = integer(16), height = integer(20);
        if (!width || !height || width > 16384 || height > 16384)
            return std::nullopt;
        return static_cast<double>(width) / height;
    }

    static double Milliseconds(Clock::time_point time)
    {
        return std::chrono::duration<double, std::milli>(time.time_since_epoch()).count();
    }

    static std::uint32_t RuntimeSeed()
    {
        try
        {
            return std::random_device{}();
        }
        catch (...)
        {
            return static_cast<std::uint32_t>(Clock::now().time_since_epoch().count());
        }
    }

    CandidateTypedStars::Settings settings_;
    CandidateTypedStars::System particles_;
    Clock::time_point time_{};
    std::wstring yellow_;
    std::wstring pink_;
    std::wstring blue_;
    bool enabled_ = false;
};
