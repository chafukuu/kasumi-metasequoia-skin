#pragma once

#include <wincodec.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace msimeui
{
// Frame timing of an animation, kept apart from the pixels so a GPU cache can keep it once the
// decoded frames are uploaded.
struct AnimationTimeline
{
    // How long each frame stays on screen, in milliseconds, already normalized by
    // NormalizeFrameDelayMs.
    std::vector<uint32_t> frameDelaysMs;
    // How many times the whole animation plays; 0 repeats forever. After the last play the last
    // frame stays on screen.
    uint32_t playCount = 0;

    struct Sample
    {
        size_t frameIndex = 0;
        // Elapsed time (ms since the animation started) at which the frame on screen changes
        // next. Empty when nothing changes any more: a single frame, or a finished animation that
        // now holds its last frame.
        std::optional<uint64_t> nextChangeMs;
    };

    uint64_t LoopDurationMs() const;
    Sample SampleAt(uint64_t elapsedMs) const;
};

// Browsers show frames that ask for 10 ms or less for 100 ms instead; so does this library, so
// an animation runs at the speed its author previewed it at.
uint32_t NormalizeFrameDelayMs(uint32_t delayMs);

// Decoding limits; an animation beyond them shows its still first frame. At namespace scope so
// it is complete where AnimatedImage uses it as a default argument.
struct AnimatedImageLimits
{
    uint32_t maxFrames = 1024;
    // All frames together, decoded (width * height * 4 * frames).
    uint64_t maxDecodedBytes = 64ull * 1024 * 1024;
    uint64_t maxFileBytes = 64ull * 1024 * 1024;
    uint32_t maxDimension = 16384;
};

// A decoded still or animated image: every frame composited onto a full-size, premultiplied
// BGRA canvas, ready to upload as it is.
//
// Animated PNG is decoded per the APNG specification (acTL / fcTL / fdAT): each frame is rebuilt
// as a standalone PNG and decoded with WIC, then composited honoring dispose_op and blend_op.
// Animated GIF goes through WIC's multi-frame GIF decoder with the same compositing rules. Any
// other container that WIC reports as multi-frame (animated WebP when the installed codec exposes
// its frames) is played frame by frame at 100 ms. Malformed or oversized animations fall back to
// the still first frame; they never fail harder than the still image would.
class AnimatedImage
{
  public:
    enum class Format
    {
        Still,
        Apng,
        Gif,
        MultiFrame,
    };

    using Limits = AnimatedImageLimits;

    // Returns nothing when not even the still first frame can be decoded.
    static std::optional<AnimatedImage> DecodeFile(IWICImagingFactory *factory, const std::wstring &filePath,
                                                   const Limits &limits = Limits());
    static std::optional<AnimatedImage> DecodeMemory(IWICImagingFactory *factory, const uint8_t *data, size_t size,
                                                     const Limits &limits = Limits());
    // Cheap check without decoding pixels: true when the file looks like an animation (an APNG
    // acTL chunk, or a WIC container with more than one frame). Still images can skip DecodeFile.
    static bool MayBeAnimated(IWICImagingFactory *factory, const std::wstring &filePath);

    uint32_t Width() const
    {
        return width_;
    }
    uint32_t Height() const
    {
        return height_;
    }
    size_t FrameCount() const
    {
        return frames_.size();
    }
    bool IsAnimated() const
    {
        return frames_.size() > 1;
    }
    Format GetFormat() const
    {
        return format_;
    }
    const AnimationTimeline &Timeline() const
    {
        return timeline_;
    }
    // Premultiplied BGRA, Width() * 4 bytes per row, the whole canvas.
    const std::vector<uint8_t> &FramePixels(size_t index) const
    {
        return frames_[index];
    }
    uint64_t DecodedBytes() const
    {
        return static_cast<uint64_t>(width_) * height_ * 4 * frames_.size();
    }

  private:
    friend struct AnimatedImageDecoder;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    Format format_ = Format::Still;
    AnimationTimeline timeline_;
    std::vector<std::vector<uint8_t>> frames_;
};
} // namespace msimeui
