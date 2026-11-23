#include "window/livesd_host.h"

#include "config/ime_config.h"
#include "defines/globals.h"
#include "ipc/ipc.h"
#include "msimeui/DeviceResources.h"
#include "skin/candidate_skin_catalog.h"
#include "utils/common_utils.h"
#include "utils/window_utils.h"
#include "window/candidate_animation_scheduler.h"
#include "window/combo_alpha_coverage.h"
#include "window/livesd_combo.h"
#include "window/livesd_motion.h"
#include "window/small_windows.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <windowsx.h>

namespace LiveSDHost
{
namespace
{
using Clock = msimeui::DeviceResources::AnimationClock;
constexpr UINT kFrameMessage = WM_APP + 0x3B1;
constexpr UINT_PTR kFrameTimer = 0x3B1;
constexpr UINT_PTR kPointerTimer = 0x3B2;
// Leave a small scheduling margin above 120 updates per second. UI-thread
// dispatch and composition commits otherwise lose the margin to timer jitter.
constexpr auto kActiveFrameInterval = std::chrono::nanoseconds(8'250'000);
constexpr auto kReducedMotionFrameInterval = std::chrono::milliseconds(250);
constexpr LONG kInvalidCaretY = -100000;
constexpr float kTeleportDistanceDip = 48.0f;
constexpr double kTeleportFadeOutMs = 100.0;
constexpr double kTeleportFadeInMs = 120.0;
constexpr double kClickFadeInMs = 180.0;
constexpr double kClickFadeOutMs = 180.0;
constexpr double kFocusAnchorWaitMs = 48.0;
constexpr double kHitRegionRefreshMs = 16.0;
// Candidate cards are fit-to-content, so their centre can move by a few
// pixels on every edit (especially while undo is held).  Follow those small
// anchor changes over a short, frame-driven interval instead of moving the
// companion HWND synchronously on every layout notification.
constexpr double kCandidateFollowMs = 88.0;
constexpr wchar_t kWindowClass[] = L"MetasequoiaLiveSD";
std::mutex postMutex;
bool acceptingActivity = true;
std::atomic<std::uint64_t> scheduledFrameWakes{0};
std::atomic<std::uint64_t> renderedFrames{0};
std::atomic<std::uint64_t> missedFrameSlots{0};
std::atomic<std::uint64_t> maximumFrameLatenessNs{0};
std::atomic<std::uint64_t> maximumRenderDurationNs{0};
std::atomic<std::uint64_t> lastFrameWakeNs{0};
std::atomic<std::uint64_t> frameWakeIntervalCount{0};
std::array<std::atomic<std::uint64_t>, 2048> frameWakeIntervalsNs{};
std::atomic<std::uint64_t> renderDurationCount{0};
std::array<std::atomic<std::uint64_t>, 2048> renderDurationsNs{};

void RecordFrameWake(Clock::time_point deadline)
{
    const auto now = Clock::now();
    const auto nowNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    const auto previousNs = lastFrameWakeNs.exchange(nowNs, std::memory_order_relaxed);
    scheduledFrameWakes.fetch_add(1, std::memory_order_relaxed);
    if (previousNs != 0 && nowNs > previousNs)
    {
        const auto index = frameWakeIntervalCount.fetch_add(1, std::memory_order_relaxed);
        frameWakeIntervalsNs[index % frameWakeIntervalsNs.size()].store(nowNs - previousNs, std::memory_order_relaxed);
    }
    if (now <= deadline)
        return;
    const auto lateness =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - deadline).count());
    auto maximum = maximumFrameLatenessNs.load(std::memory_order_relaxed);
    while (maximum < lateness &&
           !maximumFrameLatenessNs.compare_exchange_weak(maximum, lateness, std::memory_order_relaxed))
    {
    }
}
struct CandidateAnchor
{
    RECT card{};
    float scale = 1.0f;
    std::uint64_t client = 0;
    std::uint64_t epoch = 0;
};
std::optional<CandidateAnchor> candidateAnchor;

struct InputModeUpdate
{
    std::uint64_t client = 0;
    std::uint64_t epoch = 0;
    bool englishInputMode = false;
};
std::optional<InputModeUpdate> pendingInputModeState;
LiveSDCombo::Counter comboCounter;

double NowMs()
{
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

bool AnimationsEnabled()
{
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled;
}

struct Host
{
    struct Placement
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        float scale = 1.0f;
    };

    enum class PlacementFade
    {
        None,
        Out,
        In,
    };

    HWND window = nullptr;
    msimeui::DeviceResources resources;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> blendedPose;
    CandidateAnimationScheduler scheduler;
    std::optional<Clock::time_point> frameDeadline;
    bool frameWaitableTimer = false;
    LiveSDMotion::Model motion;
    std::optional<CandidateSkinCatalog::LiveSD> config;
    double reservedStageWidthDip = 520.0;
    std::array<std::wstring, 4> images;
    std::wstring clickImage;
    struct AlphaMask
    {
        UINT width = 0, height = 0;
        std::vector<BYTE> alpha;
    };
    std::array<AlphaMask, 4> poseMasks;
    std::array<LiveSDMotion::VisibleBounds, 4> poseMaskBounds;
    AlphaMask poseHitMask;
    unsigned poseHitMaskBits = (std::numeric_limits<unsigned>::max)();
    AlphaMask clickMask;
    std::array<std::wstring, 10> comboImages;
    std::wstring comboLabelImage;
    std::array<AlphaMask, 10> comboMasks;
    AlphaMask comboLabelMask;
    std::array<ComboAlphaCoverage::Mask, 10> comboCoverage;
    ComboAlphaCoverage::Mask comboLabelCoverage;
    struct ComboGlyph
    {
        const AlphaMask *mask = nullptr;
        D2D1_RECT_F destination{};
        const ComboAlphaCoverage::Mask *coverage = nullptr;
        ComboAlphaCoverage::Footprint footprint{};
    };
    std::array<ComboGlyph, 4> comboGlyphs{};
    size_t comboGlyphCount = 0;
    unsigned renderedComboCount = (std::numeric_limits<unsigned>::max)();
    float renderedComboScale = 1.0f;
    float comboOpacity = 0.0f;
    HRGN hitRegion = nullptr;
    std::vector<RECT> hitSpans;
    std::vector<DWORD> hitRegionData;
    double lastHitRegionUpdateMs = -1.0e15;
    bool hitRegionNeedsRefresh = true;
    bool hitRegionClickActive = false;
    unsigned hitRegionPoseBits = (std::numeric_limits<unsigned>::max)();
    bool pointerDown = false;
    bool dragging = false;
    bool dragHitRegionRelaxed = false;
    POINT pointerStart{};
    POINT pointerLast{};
    POINT dragOrigin{};
    double dragPoseStartXDip = 0.0;
    double dragPoseStartYDip = 0.0;
    std::optional<POINT> manualOrigin;
    std::optional<double> manualScreenXDip;
    std::optional<double> manualScreenYDip;
    bool manualDisplayBoundaryStage = false;
    bool chineseAnchoredOnDisplay = false;
    struct ChineseReturn
    {
        double offsetX = 0.0;
        double offsetY = 0.0;
        double startMs = 0.0;
        double velocityX = 0.0;
        double velocityY = 0.0;
    };
    std::optional<ChineseReturn> chineseReturn;
    bool manualPositionPinned = false;
    std::optional<POINT> pendingFocusCaret;
    double pendingFocusCaretDueMs = 0.0;
    bool awaitingCandidateAfterCaret = false;
    LiveSDMotion::Pose heldPose{};
    double clickStartMs = 0.0;
    double clickUntilMs = 0.0;

    ~Host()
    {
        if (hitRegion)
            DeleteObject(hitRegion);
    }

    static AlphaMask LoadAlpha(const std::wstring &path)
    {
        AlphaMask mask;
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        if (path.empty() ||
            FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(factory.GetAddressOf()))) ||
            FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                      decoder.GetAddressOf())))
            return mask;
        UINT count = 0;
        decoder->GetFrameCount(&count);
        for (UINT index = 0; index < (std::min)(count, 128U); ++index)
        {
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            UINT width = 0, height = 0;
            if (FAILED(decoder->GetFrame(index, frame.GetAddressOf())) || FAILED(frame->GetSize(&width, &height)) ||
                !width || !height || width > 2048 || height > 2048 ||
                FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
                FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                             nullptr, 0, WICBitmapPaletteTypeCustom)))
                continue;
            if (mask.alpha.empty())
            {
                mask.width = width;
                mask.height = height;
                mask.alpha.resize(static_cast<size_t>(width) * height);
            }
            if (width != mask.width || height != mask.height)
                continue;
            std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
            if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
                continue;
            for (size_t i = 0; i < mask.alpha.size(); ++i)
                mask.alpha[i] = (std::max)(mask.alpha[i], pixels[i * 4 + 3]);
        }
        // Keep linear-filtered edge pixels inside the window region as well.
        // The small source-space expansion is computed once on skin reload.
        AlphaMask expanded = mask;
        const int radius = mask.width >= 256 ? 2 : 1;
        for (UINT y = 0; y < mask.height; ++y)
            for (UINT x = 0; x < mask.width; ++x)
                if (mask.alpha[static_cast<size_t>(y) * mask.width + x])
                    for (int dy = -radius; dy <= radius; ++dy)
                        for (int dx = -radius; dx <= radius; ++dx)
                        {
                            const int sx = static_cast<int>(x) + dx, sy = static_cast<int>(y) + dy;
                            if (sx >= 0 && sy >= 0 && sx < static_cast<int>(mask.width) &&
                                sy < static_cast<int>(mask.height))
                                expanded.alpha[static_cast<size_t>(sy) * mask.width + sx] = 255;
                        }
        return expanded;
    }

    static LiveSDMotion::VisibleBounds VisibleBoundsForMask(const AlphaMask &mask)
    {
        if (!mask.width || !mask.height || mask.alpha.empty())
            return {0.0, 0.0, 1.0, 1.0};
        UINT left = mask.width, top = mask.height, right = 0, bottom = 0;
        for (UINT y = 0; y < mask.height; ++y)
            for (UINT x = 0; x < mask.width; ++x)
                if (mask.alpha[static_cast<size_t>(y) * mask.width + x])
                {
                    left = (std::min)(left, x);
                    top = (std::min)(top, y);
                    right = (std::max)(right, x + 1);
                    bottom = (std::max)(bottom, y + 1);
                }
        if (left >= right || top >= bottom)
            return {0.0, 0.0, 1.0, 1.0};
        return {static_cast<double>(left) / mask.width, static_cast<double>(top) / mask.height,
                static_cast<double>(right) / mask.width, static_cast<double>(bottom) / mask.height};
    }

    void UpdateHitRegion(const D2D1_MATRIX_3X2_F &poseTransform, const D2D1_RECT_F &poseDestination,
                         const D2D1_MATRIX_3X2_F &clickTransform, const D2D1_RECT_F &clickDestination,
                         const LiveSDMotion::Pose &pose, bool clicked, float clickOpacity, float opacity)
    {
        if (!placement)
            return;
        const double nowMs = NowMs();
        unsigned activePoseBits = 0;
        for (size_t i = 0; i < pose.weights.size(); ++i)
            if (pose.weights[i] > 0.001)
                activePoseBits |= 1U << i;
        if (!hitRegionNeedsRefresh && clicked == hitRegionClickActive && activePoseBits == hitRegionPoseBits &&
            nowMs - lastHitRegionUpdateMs < kHitRegionRefreshMs)
            return;
        if (activePoseBits != poseHitMaskBits)
        {
            poseHitMask = {};
            poseHitMaskBits = activePoseBits;
            for (size_t i = 0; i < poseMasks.size(); ++i)
            {
                if (!(activePoseBits & (1U << i)) || !poseMasks[i].width || !poseMasks[i].height ||
                    poseMasks[i].alpha.size() != static_cast<size_t>(poseMasks[i].width) * poseMasks[i].height)
                    continue;
                if (!poseHitMask.width)
                {
                    poseHitMask.width = poseMasks[i].width;
                    poseHitMask.height = poseMasks[i].height;
                    poseHitMask.alpha.assign(poseMasks[i].alpha.size(), 0);
                }
                if (poseMasks[i].width != poseHitMask.width || poseMasks[i].height != poseHitMask.height)
                    continue;
                for (size_t pixel = 0; pixel < poseHitMask.alpha.size(); ++pixel)
                    poseHitMask.alpha[pixel] = (std::max)(poseHitMask.alpha[pixel], poseMasks[i].alpha[pixel]);
            }
        }
        const auto makeInverse = [](const D2D1_MATRIX_3X2_F &transform) {
            auto inverse = D2D1::Matrix3x2F(transform._11, transform._12, transform._21, transform._22, transform._31,
                                            transform._32);
            return inverse;
        };
        auto poseInverse = makeInverse(poseTransform);
        const bool poseInvertible = poseInverse.Invert();
        auto clickInverse = makeInverse(clickTransform);
        const bool clickInvertible = clickInverse.Invert();
        auto &spans = hitSpans;
        spans.clear();
        if (spans.capacity() < static_cast<size_t>(placement->height))
            spans.reserve(static_cast<size_t>(placement->height) * 2);
        const auto inside = [&](int x, int y) {
            if (opacity < 0.05f)
                return false;

            const float placementScale = placement->scale;
            const auto sample = [x, y, placementScale](const D2D1::Matrix3x2F &inverse, bool invertible,
                                                       const D2D1_RECT_F &destination, const AlphaMask &mask) {
                if (!invertible || mask.alpha.empty())
                    return false;
                const auto local =
                    inverse.TransformPoint(D2D1::Point2F((x + 0.5f) / placementScale, (y + 0.5f) / placementScale));
                const float u = (local.x - destination.left) / (destination.right - destination.left);
                const float v = (local.y - destination.top) / (destination.bottom - destination.top);
                if (u < 0 || v < 0 || u >= 1 || v >= 1)
                    return false;
                const UINT sx = (std::min)(mask.width - 1, static_cast<UINT>(u * mask.width));
                const UINT sy = (std::min)(mask.height - 1, static_cast<UINT>(v * mask.height));
                return mask.alpha[static_cast<size_t>(sy) * mask.width + sx] > 0;
            };
            if (clicked && clickOpacity * opacity >= 0.05f &&
                sample(clickInverse, clickInvertible, clickDestination, clickMask))
                return true;
            if ((!clicked || (1.0f - clickOpacity) * opacity >= 0.05f) &&
                sample(poseInverse, poseInvertible, poseDestination, poseHitMask))
                return true;
            for (size_t i = 0; comboOpacity > 0.0f && i < comboGlyphCount; ++i)
                if (comboGlyphs[i].coverage->ContainsPixel(comboGlyphs[i].footprint, x, y))
                    return true;
            return false;
        };
        const auto bounds = [](const D2D1_MATRIX_3X2_F &transform, const D2D1_RECT_F &destination) {
            const D2D1::Matrix3x2F matrix(transform._11, transform._12, transform._21, transform._22, transform._31,
                                          transform._32);
            return std::array<D2D1_POINT_2F, 4>{matrix.TransformPoint({destination.left, destination.top}),
                                                matrix.TransformPoint({destination.right, destination.top}),
                                                matrix.TransformPoint({destination.left, destination.bottom}),
                                                matrix.TransformPoint({destination.right, destination.bottom})};
        };
        D2D1_RECT_F visibleDestination = poseDestination;
        bool hasVisibleBounds = false;
        for (size_t index = 0; index < poseMaskBounds.size(); ++index)
        {
            if (pose.weights[index] <= 0.001)
                continue;
            const auto &source = poseMaskBounds[index];
            const float width = poseDestination.right - poseDestination.left;
            const float height = poseDestination.bottom - poseDestination.top;
            const auto visible = D2D1::RectF(poseDestination.left + static_cast<float>(source.left) * width,
                                             poseDestination.top + static_cast<float>(source.top) * height,
                                             poseDestination.left + static_cast<float>(source.right) * width,
                                             poseDestination.top + static_cast<float>(source.bottom) * height);
            if (!hasVisibleBounds)
                visibleDestination = visible;
            else
            {
                visibleDestination.left = (std::min)(visibleDestination.left, visible.left);
                visibleDestination.top = (std::min)(visibleDestination.top, visible.top);
                visibleDestination.right = (std::max)(visibleDestination.right, visible.right);
                visibleDestination.bottom = (std::max)(visibleDestination.bottom, visible.bottom);
            }
            hasVisibleBounds = true;
        }
        const auto poseCorners = bounds(poseTransform, visibleDestination);
        const auto clickCorners = bounds(clickTransform, clickDestination);
        float left = poseCorners[0].x, right = left, top = poseCorners[0].y, bottom = top;
        const auto includeCorners = [&](const auto &corners) {
            for (const auto &corner : corners)
            {
                left = (std::min)(left, corner.x);
                right = (std::max)(right, corner.x);
                top = (std::min)(top, corner.y);
                bottom = (std::max)(bottom, corner.y);
            }
        };
        includeCorners(poseCorners);
        if (clicked && clickOpacity > 0.0f)
            includeCorners(clickCorners);
        for (size_t i = 0; i < comboGlyphCount; ++i)
        {
            // D2D's quad coverage can touch a pixel whose centre lies just
            // outside a fractional destination edge. Include one physical
            // pixel of sampling support, independent of the monitor's DPI.
            const float fringeDip = 1.0f / placement->scale;
            const auto &r = comboGlyphs[i].destination;
            includeCorners(
                bounds(D2D1::Matrix3x2F::Identity(),
                       D2D1::RectF(r.left - fringeDip, r.top - fringeDip, r.right + fringeDip, r.bottom + fringeDip)));
        }
        const int x0 = (std::clamp)(static_cast<int>(std::floor(left * placement->scale)), 0, placement->width);
        const int x1 = (std::clamp)(static_cast<int>(std::ceil(right * placement->scale)), 0, placement->width);
        const int y0 = (std::clamp)(static_cast<int>(std::floor(top * placement->scale)), 0, placement->height);
        const int y1 = (std::clamp)(static_cast<int>(std::ceil(bottom * placement->scale)), 0, placement->height);
        for (int y = y0; y < y1; ++y)
        {
            int start = -1;
            for (int x = x0; x <= x1; ++x)
            {
                const bool filled = x < x1 && inside(x, y);
                if (filled && start < 0)
                    start = x;
                if (!filled && start >= 0)
                {
                    spans.push_back({start, y, x, y + 1});
                    start = -1;
                }
            }
        }
        HRGN next = nullptr;
        if (spans.empty())
            next = CreateRectRgn(0, 0, 0, 0);
        else
        {
            const size_t bytes = sizeof(RGNDATAHEADER) + spans.size() * sizeof(RECT);
            auto &data = hitRegionData;
            data.resize((bytes + sizeof(DWORD) - 1) / sizeof(DWORD));
            auto *region = reinterpret_cast<RGNDATA *>(data.data());
            region->rdh = {sizeof(RGNDATAHEADER),
                           RDH_RECTANGLES,
                           static_cast<DWORD>(spans.size()),
                           static_cast<DWORD>(spans.size() * sizeof(RECT)),
                           {0, 0, placement->width, placement->height}};
            std::copy(spans.begin(), spans.end(), reinterpret_cast<RECT *>(region->Buffer));
            next = ExtCreateRegion(nullptr, static_cast<DWORD>(bytes), region);
        }
        if (!next)
            return;
        if (hitRegion && EqualRgn(hitRegion, next))
        {
            DeleteObject(next);
            lastHitRegionUpdateMs = nowMs;
            hitRegionNeedsRefresh = false;
            hitRegionClickActive = clicked;
            hitRegionPoseBits = activePoseBits;
            return;
        }
        if (hitRegion)
            DeleteObject(hitRegion);
        hitRegion = next;
        lastHitRegionUpdateMs = nowMs;
        hitRegionNeedsRefresh = false;
        hitRegionClickActive = clicked;
        hitRegionPoseBits = activePoseBits;
        // Window regions participate in cross-process hit testing. The empty
        // stage is outside the HWND, rather than relying on HTTRANSPARENT.
        HRGN ownedByWindow = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(ownedByWindow, hitRegion, nullptr, RGN_COPY);
        if (!SetWindowRgn(window, ownedByWindow, FALSE))
            DeleteObject(ownedByWindow);
    }

    void SetFullStageHitRegion()
    {
        if (!window || !placement)
            return;
        HRGN next = CreateRectRgn(0, 0, placement->width, placement->height);
        if (!next)
            return;
        if (hitRegion && EqualRgn(hitRegion, next))
        {
            DeleteObject(next);
            hitRegionNeedsRefresh = true;
            return;
        }
        if (hitRegion)
            DeleteObject(hitRegion);
        hitRegion = next;
        hitRegionNeedsRefresh = true;
        HRGN ownedByWindow = CreateRectRgn(0, 0, placement->width, placement->height);
        if (!ownedByWindow)
            return;
        if (!SetWindowRgn(window, ownedByWindow, FALSE))
            DeleteObject(ownedByWindow);
    }

    void StartClick()
    {
        if (!config || clickImage.empty() || clickMask.alpha.empty())
            return;
        const double nowMs = NowMs();
        heldPose = VisualPose(nowMs);
        if (motion.DisplayBoundaryMode() && manualScreenXDip)
            heldPose.xDip = *manualScreenXDip;
        if (motion.DisplayBoundaryMode() && manualScreenYDip)
            heldPose.yDip = *manualScreenYDip;
        clickStartMs = nowMs;
        clickUntilMs = clickStartMs + config->clickDurationMs;
        hitRegionNeedsRefresh = true;
        resources.SetAnimationEpoch(Clock::now());
        Render();
    }

    std::array<double, 4> ChineseReturnOffset(double nowMs) const
    {
        if (!chineseReturn)
            return {};
        const double elapsed = (std::max)(0.0, nowMs - chineseReturn->startMs);
        constexpr double rate = 5.5 / 100.0;
        const double decay = std::exp(-rate * elapsed);
        const double slopeX = chineseReturn->velocityX + rate * chineseReturn->offsetX;
        const double slopeY = chineseReturn->velocityY + rate * chineseReturn->offsetY;
        const double x = chineseReturn->offsetX + slopeX * elapsed;
        const double y = chineseReturn->offsetY + slopeY * elapsed;
        return {x * decay, y * decay, (slopeX - rate * x) * decay, (slopeY - rate * y) * decay};
    }

    LiveSDMotion::Pose VisualPose(double nowMs)
    {
        auto pose = motion.Sample(nowMs);
        if (manualScreenXDip)
            pose.xDip = *manualScreenXDip;
        if (manualScreenYDip)
            pose.yDip = *manualScreenYDip;
        if (chineseReturn)
        {
            const auto offset = ChineseReturnOffset(nowMs);
            pose.xDip += offset[0];
            pose.yDip += offset[1];
            pose = motion.FitPoseToDisplay(pose);
            if ((std::max)(std::abs(offset[0]), std::abs(offset[1])) < 0.1 &&
                (std::max)(std::abs(offset[2]), std::abs(offset[3])) < 0.01)
                chineseReturn.reset();
        }
        return pose;
    }

    void FollowChineseFootY(double footYDip)
    {
        if (std::abs(motion.FootYDip() - footYDip) < 0.01)
            return;
        const double nowMs = NowMs();
        const auto from = VisualPose(nowMs);
        const auto offset = ChineseReturnOffset(nowMs);
        motion.SetFootYDip(footYDip);
        const auto to = motion.Sample(nowMs);
        // Preserve return position and velocity when the candidate moves on
        // the unchanged display surface; repeated layout updates must not snap.
        chineseReturn = ChineseReturn{from.xDip - to.xDip, from.yDip - to.yDip, nowMs, offset[2], offset[3]};
    }

    void CancelPointer()
    {
        if (dragHitRegionRelaxed)
            hitRegionNeedsRefresh = true;
        pointerDown = false;
        dragging = false;
        dragHitRegionRelaxed = false;
        if (window)
            KillTimer(window, kPointerTimer);
        if (GetCapture() == window)
            ReleaseCapture();
    }

    bool MovePointer(POINT point)
    {
        if (!pointerDown || !placement)
            return false;
        pointerLast = point;
        int dx = point.x - pointerStart.x, dy = point.y - pointerStart.y;
        const UINT dpi = static_cast<UINT>(std::lround(placement->scale * 96.0f));
        dragging = dragging || std::abs(dx) >= GetSystemMetricsForDpi(SM_CXDRAG, dpi) ||
                   std::abs(dy) >= GetSystemMetricsForDpi(SM_CYDRAG, dpi);
        if (!dragging)
            return false;
        const bool beganDragging = !dragHitRegionRelaxed;
        if (beganDragging)
        {
            // Stop a pending return/fade at the pose the user actually grabbed.
            const auto grabbed = heldPose;
            const double screenX = placement->x + grabbed.xDip * placement->scale;
            const double screenY = placement->y + grabbed.yDip * placement->scale;
            chineseReturn.reset();
            chineseAnchoredOnDisplay = false;
            pendingPlacement.reset();
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            placementFade = PlacementFade::None;
            if (manualDisplayBoundaryStage)
            {
                motion.SetDisplayBoundaryMode(true);
                motion.SetPositionDip((screenX - placement->x) / placement->scale,
                                      (screenY - placement->y) / placement->scale);
                dragPoseStartXDip = grabbed.xDip;
                dragPoseStartYDip = grabbed.yDip;
            }
        }
        if (dragging && !motion.DisplayBoundaryMode() && !englishInputMode)
        {
            // Chinese normally uses the compact candidate stage. Once a real
            // drag starts, promote that stage to the current monitor so the
            // pointer can cross the whole screen without clamping at the
            // candidate-card-sized HWND. Hide the old composition before
            // moving/resizing the HWND; otherwise its compact frame can be
            // exposed at the monitor origin until the full-stage present.
            if (IsWindowVisible(window))
            {
                ShowWindow(window, SW_HIDE);
                revealAfterPresent = true;
            }
            manualDisplayBoundaryStage = true;
            manualPositionPinned = false;
            manualOrigin.reset();
            pendingFocusCaret.reset();
            Place();
            if (!placement)
                return false;
            const auto promoted = motion.Sample(NowMs());
            dragPoseStartXDip = promoted.xDip;
            dragPoseStartYDip = promoted.yDip;
        }
        if (beganDragging)
        {
            // Keep the captured pointer on the stable surface while dragging;
            // rebuilding the alpha HRGN for every mouse sample causes flicker.
            SetFullStageHitRegion();
            dragHitRegionRelaxed = true;
        }
        if (motion.DisplayBoundaryMode() && (englishInputMode || manualDisplayBoundaryStage))
        {
            motion.SetPositionDip(dragPoseStartXDip + dx / placement->scale, dragPoseStartYDip + dy / placement->scale);
            // Sample includes the visible-pose edge protection. Preserve that
            // same anchor during the press and release, including screen corners.
            const auto placedPose = motion.Sample(NowMs());
            manualScreenXDip = placedPose.xDip;
            manualScreenYDip = placedPose.yDip;
            heldPose.xDip = *manualScreenXDip;
            heldPose.yDip = *manualScreenYDip;
            if (beganDragging)
                Render();
            return true;
        }
        manualOrigin = POINT{dragOrigin.x + dx, dragOrigin.y + dy};
        Place();
        return true;
    }

    void ReturnChineseToAnchor()
    {
        if (englishInputMode || pointerDown || !manualDisplayBoundaryStage || chineseAnchoredOnDisplay || !placement ||
            !config)
            return;
        const double nowMs = NowMs();
        const auto from = VisualPose(nowMs);
        const auto compact = CalculatePlacement(true);
        if (!compact)
            return;
        const double scale = (std::max)(placement->scale, 0.001f);
        const bool candidateBound = hasCandidate || keepCandidateAnchor;
        const double leftDip = ((candidateBound ? candidate.left : compact->x) - placement->x) / scale;
        const double widthDip = candidateBound ? (candidate.right - candidate.left) / scale : config->stageWidthDip;
        chineseAnchoredOnDisplay = true;
        manualScreenXDip.reset();
        manualScreenYDip.reset();
        motion.SetDisplayBoundaryMode(false);
        motion.SetFootYDip((compact->y - placement->y) / scale + config->footYDip);
        motion.SetCandidateIntervalImmediateDip(leftDip, widthDip);
        motion.SetPositionDip(motion.MovementCenterDip());
        const auto to = motion.Sample(nowMs);
        chineseReturn = ChineseReturn{from.xDip - to.xDip, from.yDip - to.yDip, nowMs};
    }

    void FinishPointer(POINT point)
    {
        if (!pointerDown)
            return;
        if (point.x != pointerLast.x || point.y != pointerLast.y)
            MovePointer(point);
        const bool click = !dragging;
        CancelPointer();
        if (click)
            StartClick();
        else
        {
            // Hold the release until the next Chinese pulse/layout starts return.
            Render();
        }
    }

    void PollPointer()
    {
        if (!pointerDown)
            return;
        if (!IsOwnerCurrent())
        {
            Hide();
            return;
        }
        POINT point{};
        if (!GetCursorPos(&point))
        {
            const bool restoreHitRegion = dragHitRegionRelaxed;
            CancelPointer();
            if (restoreHitRegion)
                Render();
            return;
        }
        // A no-activate background HWND cannot capture mouse messages outside
        // its visible region. Track only the active press, including release,
        // without taking focus or installing a global mouse hook.
        const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
        if (!(GetAsyncKeyState(button) & 0x8000))
            FinishPointer(point);
        else
            MovePointer(point);
    }
    std::uint64_t client = 0;
    std::uint64_t epoch = 0;
    std::string skinKey;
    POINT caret{};
    POINT lastFocusCaret{};
    HWND contextRootWindow = nullptr;
    RECT candidate{};
    float candidateScale = 1.0f;
    std::optional<double> candidateStageLeftOffsetDip;
    bool hasCandidate = false;
    bool englishInputMode = false;
    bool keepCandidateAnchor = false;
    bool candidateContentResizedRequest = false;
    bool focused = false;
    bool visible = false;
    bool placing = false;
    bool revealAfterPresent = false;
    float renderScale = 0.0f;
    std::optional<Placement> placement;
    std::optional<Placement> candidateFollowFrom;
    std::optional<Placement> pendingPlacement;
    std::optional<Placement> candidateFollowTarget;
    double candidateFollowStartMs = 0.0;
    double candidateFollowDurationMs = 0.0;
    double candidateFollowLastMs = 0.0;
    double candidateFollowX = 0.0;
    double candidateFollowY = 0.0;
    double candidateFollowVelocityX = 0.0;
    double candidateFollowVelocityY = 0.0;
    bool smoothCandidatePlacementRequest = false;
    PlacementFade placementFade = PlacementFade::None;
    double placementFadeStartMs = 0.0;
    double placementFadeDurationMs = 0.0;
    float placementFadeStartOpacity = 1.0f;
    bool transparentPlacementPresented = false;

    bool IsOwnerCurrent() const
    {
        if (!focused || !IsPipeActivationCurrent(client, epoch))
            return false;
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return false;
        if (contextRootWindow && IsWindow(contextRootWindow))
        {
            const HWND targetRoot = GetAncestor(contextRootWindow, GA_ROOTOWNER);
            const HWND foregroundRoot = GetAncestor(foreground, GA_ROOTOWNER);
            if (targetRoot && targetRoot == foregroundRoot)
                return true;

            // Text controls can attach their focus thread to a foreground
            // window. Accept the focused child only when it resolves to the
            // same root owner as the context window.
            GUITHREADINFO threadInfo{sizeof(threadInfo)};
            const DWORD foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
            if (foregroundThread && GetGUIThreadInfo(foregroundThread, &threadInfo) && threadInfo.hwndFocus &&
                targetRoot && GetAncestor(threadInfo.hwndFocus, GA_ROOTOWNER) == targetRoot)
                return true;
            return false;
        }
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(foreground, &foregroundProcess);
        return foregroundProcess != 0 && foregroundProcess == static_cast<DWORD>(client >> 32);
    }

    void Hide()
    {
        CancelPointer();
        clickStartMs = 0.0;
        clickUntilMs = 0.0;
        scheduler.Cancel();
        frameDeadline.reset();
        pendingPlacement.reset();
        candidateFollowFrom.reset();
        candidateFollowTarget.reset();
        candidateFollowStartMs = 0.0;
        candidateFollowDurationMs = 0.0;
        candidateStageLeftOffsetDip.reset();
        smoothCandidatePlacementRequest = false;
        candidateContentResizedRequest = false;
        placementFade = PlacementFade::None;
        placementFadeStartOpacity = 1.0f;
        transparentPlacementPresented = false;
        manualPositionPinned = false;
        manualDisplayBoundaryStage = false;
        chineseAnchoredOnDisplay = false;
        chineseReturn.reset();
        pendingFocusCaret.reset();
        awaitingCandidateAfterCaret = false;
        if (window)
        {
            KillTimer(window, kFrameTimer);
            ShowWindow(window, SW_HIDE);
        }
        revealAfterPresent = false;
        visible = false;
    }

    void ScheduleFrameAt(Clock::time_point deadline)
    {
        if (!window)
            return;
        if (scheduler.Schedule(deadline))
        {
            frameWaitableTimer = true;
            // Schedule() may keep an already-armed earlier deadline. Retain
            // the timer's actual phase rather than the newer request.
            frameDeadline = scheduler.ScheduledDeadline();
            KillTimer(window, kFrameTimer);
            return;
        }

        // The waitable timer is preferred (and high-resolution where supported).
        // WM_TIMER is a last-resort fallback and may be limited by Windows' timer
        // resolution, but the next scheduled deadline remains phase aligned.
        frameWaitableTimer = false;
        frameDeadline = deadline;
        const auto now = Clock::now();
        const auto remaining = deadline > now ? deadline - now : Clock::duration::zero();
        const auto delayMs = (std::max)(1LL, std::chrono::ceil<std::chrono::milliseconds>(remaining).count());
        const auto boundedDelay = (std::min)(delayMs, static_cast<long long>((std::numeric_limits<UINT>::max)()));
        SetTimer(window, kFrameTimer, static_cast<UINT>(boundedDelay), nullptr);
    }

    void ScheduleNextFrame(Clock::time_point frameStarted, bool active)
    {
        if (!active)
        {
            ScheduleFrameAt(frameStarted + kReducedMotionFrameInterval);
            return;
        }

        Clock::time_point deadline = frameDeadline.value_or(frameStarted) + kActiveFrameInterval;
        const auto now = Clock::now();
        if (deadline <= now)
        {
            // Render only the latest pose. Skip missed slots arithmetically so
            // a blocked UI thread cannot trigger a catch-up burst.
            const auto skipped = (now - deadline) / kActiveFrameInterval + 1;
            missedFrameSlots.fetch_add(static_cast<std::uint64_t>(skipped), std::memory_order_relaxed);
            deadline += kActiveFrameInterval * skipped;
        }
        ScheduleFrameAt(deadline);
    }

    bool RefreshConfig()
    {
        const std::string id = GetConfiguredCandidateSkin();
        const std::string layout = GetConfiguredCandidateWindowLayout();
        const bool light = ResolveConfiguredTheme(GetConfiguredThemeCand()) == "light";
        const std::string key =
            id + "/" + layout + (light ? "/light/" : "/dark/") + std::to_string(GetCandidateSkinReloadRevision());
        if (key == skinKey)
            return false;
        skinKey = key;
        scheduler.Cancel();
        candidateFollowFrom.reset();
        candidateFollowTarget.reset();
        candidateFollowStartMs = 0.0;
        candidateFollowDurationMs = 0.0;
        candidateStageLeftOffsetDip.reset();
        resources.ClearImageCaches();
        blendedPose.Reset();
        hitRegionNeedsRefresh = true;
        config.reset();
        comboGlyphCount = 0;
        comboImages.fill({});
        comboLabelImage.clear();
        const auto root = std::filesystem::path(CommonUtils::get_ime_data_path_w()) / L"skins";
        const auto package = CandidateSkinCatalog::LoadSupported(root, id, layout, light ? "light" : "dark");
        if (!package || !package->liveSD)
            return true;
        config = package->liveSD;
        reservedStageWidthDip = (std::max)(config->stageWidthDip, package->maxWidthDip.value_or(0.0));
        const auto folder = root / std::filesystem::u8path(id);
        if (config->combo)
        {
            for (size_t i = 0; i < comboImages.size(); ++i)
            {
                comboImages[i] = (folder / std::filesystem::u8path(config->combo->digitImages[i])).wstring();
                comboMasks[i] = LoadAlpha(comboImages[i]);
                comboCoverage[i].Reset(comboMasks[i].width, comboMasks[i].height, comboMasks[i].alpha);
            }
            comboLabelImage = (folder / std::filesystem::u8path(config->combo->labelImage)).wstring();
            comboLabelMask = LoadAlpha(comboLabelImage);
            comboLabelCoverage.Reset(comboLabelMask.width, comboLabelMask.height, comboLabelMask.alpha);
            const bool decoded =
                !comboLabelMask.alpha.empty() && std::all_of(comboMasks.begin(), comboMasks.end(),
                                                             [](const AlphaMask &mask) { return !mask.alpha.empty(); });
            if (!decoded)
                config->combo.reset();
        }
        clickImage = config->clickImage.empty() ? std::wstring()
                                                : (folder / std::filesystem::u8path(config->clickImage)).wstring();
        clickMask = LoadAlpha(clickImage);
        poseHitMask = {};
        poseHitMaskBits = (std::numeric_limits<unsigned>::max)();
        clickStartMs = 0.0;
        clickUntilMs = 0.0;
        const std::array<std::string, 4> relative = {config->idleImage, config->idle2Image, config->happyImage,
                                                     config->missImage};
        for (size_t i = 0; i < images.size(); ++i)
        {
            images[i] = (folder / std::filesystem::u8path(relative[i])).wstring();
            poseMasks[i] = LoadAlpha(images[i]);
        }
        motion = LiveSDMotion::Model({config->stageWidthDip, config->widthDip, config->heightDip,
                                      config->stageHeightDip, config->footYDip, config->rightBoundaryInsetDip});
        motion.Reset(NowMs());
        std::array<LiveSDMotion::VisibleBounds, 4> frameBounds;
        for (size_t i = 0; i < poseMasks.size(); ++i)
            frameBounds[i] = VisibleBoundsForMask(poseMasks[i]);
        poseMaskBounds = frameBounds;
        motion.SetVisibleFrameBounds(frameBounds);
        motion.SetCandidateAnchorVisible(hasCandidate && !englishInputMode);
        return true;
    }

    bool EnsureWindow();

    std::optional<Placement> CalculatePlacement(bool compactChinese = false)
    {
        if (!config || !window)
            return std::nullopt;
        // A visible candidate is the authoritative anchor. Activity messages
        // can carry a newer caret while the candidate card is still settling;
        // using that caret here would make the mascot jump with text insertion
        // instead of staying above the card.
        // Closing a candidate card must not pull the stage back to the caret.
        // Retain its last anchor until a new card or a substantial caret jump.
        const bool useDisplayBoundaryStage =
            (englishInputMode || (manualDisplayBoundaryStage && !compactChinese)) && !manualOrigin;
        const bool useCandidateAnchor = !englishInputMode && (hasCandidate || keepCandidateAnchor);
        const POINT placementPoint =
            manualOrigin && placement && !englishInputMode
                ? POINT{manualOrigin->x + placement->width / 2, manualOrigin->y + placement->height / 2}
                : (useCandidateAnchor ? POINT{candidate.left, candidate.top} : caret);
        const float queriedScale = GetScaleForPoint(placementPoint);
        float scale =
            useCandidateAnchor && !manualOrigin ? candidateScale : (queriedScale > 0.0f ? queriedScale : 1.0f);
        MONITORINFO monitor{sizeof(monitor)};
        RECT work{};
        RECT display{};
        if (GetMonitorInfoW(MonitorFromPoint(placementPoint, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            work = monitor.rcWork;
            display = monitor.rcMonitor;
        }
        else
        {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            display = work;
        }
        if (work.right <= work.left || work.bottom <= work.top)
            return std::nullopt;
        // The surface stays fixed while typing. English uses the monitor's
        // full rectangle; Chinese reserves the skin's maximum
        // width and changes only the model's live candidate boundary.
        if (useDisplayBoundaryStage)
        {
            const double minimumWidthDip = config->widthDip + 24.0;
            scale = (std::min)(scale, static_cast<float>((display.right - display.left) / minimumWidthDip));
        }
        else
        {
            // A small work area can be narrower than the configured DIP stage.
            // Fit the complete stage proportionally before clamping its origin.
            scale = (std::min)(scale, static_cast<float>((work.right - work.left) / reservedStageWidthDip));
        }
        scale = (std::min)(scale, static_cast<float>((work.bottom - work.top) / config->stageHeightDip));
        const int width = useDisplayBoundaryStage
                              ? (std::max)(1, static_cast<int>(display.right - display.left))
                              : (std::max)(1, (std::min)(static_cast<int>(std::ceil(reservedStageWidthDip * scale)),
                                                         static_cast<int>(work.right - work.left)));
        const int height = useDisplayBoundaryStage
                               ? static_cast<int>(display.bottom - display.top)
                               : (std::max)(1, (std::min)(static_cast<int>(std::ceil(config->stageHeightDip * scale)),
                                                          static_cast<int>(work.bottom - work.top)));
        int x =
            useDisplayBoundaryStage ? display.left : caret.x - static_cast<int>(config->stageWidthDip * 0.25 * scale);
        if (useCandidateAnchor)
        {
            // Candidate placement already reserves the maximum card width.
            // Keep the left origin stable while its live movement bounds grow.
            const double initialOffsetDip = 0.0;
            if (!candidateStageLeftOffsetDip)
                candidateStageLeftOffsetDip = initialOffsetDip;
            x = candidate.left + static_cast<int>(std::lround(*candidateStageLeftOffsetDip * scale));
        }
        int y = (useCandidateAnchor ? candidate.top : caret.y) - height;
        if (useCandidateAnchor && y < work.top && candidate.bottom + height <= work.bottom)
            y = candidate.bottom;
        if (manualOrigin && !englishInputMode)
        {
            x = manualOrigin->x;
            y = manualOrigin->y;
        }
        x = (std::clamp)(x, static_cast<int>(work.left),
                         (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - width));
        if (useDisplayBoundaryStage)
        {
            x = display.left;
            y = display.top;
        }
        else
            y = (std::clamp)(y, static_cast<int>(work.top),
                             (std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
        return Placement{x, y, width, height, scale};
    }

    static bool SamePlacement(const Placement &left, const Placement &right)
    {
        return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height &&
               std::abs(left.scale - right.scale) <= 0.001f;
    }

    static bool IsTeleport(const Placement &from, const Placement &to)
    {
        const float distance = kTeleportDistanceDip * (std::max)(from.scale, to.scale);
        return (std::max)(std::abs(to.x - from.x), std::abs(to.y - from.y)) >= distance || from.width != to.width ||
               from.height != to.height || std::abs(from.scale - to.scale) > 0.001f;
    }

    static bool HasSameStageGeometry(const Placement &from, const Placement &to)
    {
        return from.width == to.width && from.height == to.height && std::abs(from.scale - to.scale) <= 0.001f;
    }

    static double Ease(double value)
    {
        value = std::clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
    }

    float ClickOpacity(double nowMs) const
    {
        if (!config || clickUntilMs <= clickStartMs || nowMs < clickStartMs || nowMs >= clickUntilMs)
            return 0.0f;
        const double duration = clickUntilMs - clickStartMs;
        const double elapsed = nowMs - clickStartMs;
        const double fadeIn = (std::min)(kClickFadeInMs, duration * 0.5);
        const double fadeOut = (std::min)(kClickFadeOutMs, duration * 0.5);
        if (fadeIn > 0.0 && elapsed < fadeIn)
            return static_cast<float>(Ease(elapsed / fadeIn));
        const double fadeOutStart = duration - fadeOut;
        if (fadeOut > 0.0 && elapsed > fadeOutStart)
            return static_cast<float>(1.0 - Ease((elapsed - fadeOutStart) / fadeOut));
        return 1.0f;
    }

    float PlacementOpacity(double nowMs) const
    {
        if (placementFade == PlacementFade::None || placementFadeDurationMs <= 0.0)
            return 1.0f;
        const double progress = std::clamp((nowMs - placementFadeStartMs) / placementFadeDurationMs, 0.0, 1.0);
        const float eased = static_cast<float>(Ease(progress));
        if (placementFade == PlacementFade::Out)
            return placementFadeStartOpacity * (1.0f - eased);
        return placementFadeStartOpacity + (1.0f - placementFadeStartOpacity) * eased;
    }

    void UpdateCandidateInterval(const Placement &target)
    {
        if (englishInputMode || !config)
            return;
        if (hasCandidate || keepCandidateAnchor)
        {
            // The reserved host may be clamped left of the actual card near
            // a screen edge. Its local origin is not necessarily the card's.
            // Keep a dragged Chinese host's interval in its own coordinates.
            const double leftDip = manualOrigin ? motion.CandidateMovementLeftDip()
                                                : (candidate.left - target.x) / (std::max)(target.scale, 0.001f);
            motion.SetCandidateIntervalDip(leftDip,
                                           (candidate.right - candidate.left) / (std::max)(target.scale, 0.001f));
        }
        else
            motion.SetCandidateWidthDip(config->stageWidthDip);
    }

    void ApplyPlacement(const Placement &target)
    {
        const bool moved = !placement || placement->x != target.x || placement->y != target.y;
        const bool resized = !placement || placement->width != target.width || placement->height != target.height;
        const bool targetChanged = resized || !placement || std::abs(renderScale - target.scale) > 0.001f;
        const bool displayStage =
            (englishInputMode || (manualDisplayBoundaryStage && !chineseAnchoredOnDisplay)) && !manualOrigin;
        const double targetStageWidthDip = static_cast<double>(target.width) / (std::max)(target.scale, 0.001f);
        const double targetStageHeightDip = static_cast<double>(target.height) / (std::max)(target.scale, 0.001f);
        const bool modelSpaceChanged = motion.DisplayBoundaryMode() != displayStage ||
                                       std::abs(motion.StageWidthDip() - targetStageWidthDip) >= 0.01 ||
                                       std::abs(motion.StageHeightDip() - targetStageHeightDip) >= 0.01;
        double spriteScreenX = static_cast<double>(caret.x);
        double spriteScreenY = static_cast<double>(caret.y);
        if (placement)
        {
            auto pose = pointerDown ? heldPose : VisualPose(NowMs());
            if (manualScreenXDip)
                pose.xDip = *manualScreenXDip;
            if (manualScreenYDip)
                pose.yDip = *manualScreenYDip;
            spriteScreenX = placement->x + pose.xDip * placement->scale;
            spriteScreenY = placement->y + pose.yDip * placement->scale;
        }
        motion.SetDisplayBoundaryMode(displayStage);
        motion.SetCandidateAnchorVisible(hasCandidate && !englishInputMode);
        motion.SetStageSizeDip(targetStageWidthDip, targetStageHeightDip);
        double footYDip = config ? config->footYDip : 233.0;
        if (chineseAnchoredOnDisplay)
            if (const auto compact = CalculatePlacement(true))
                footYDip += (compact->y - target.y) / (std::max)(target.scale, 0.001f);
        motion.SetFootYDip(footYDip);
        if (!displayStage)
            UpdateCandidateInterval(candidateFollowTarget.value_or(target));
        if (modelSpaceChanged && displayStage)
            motion.SetPositionDip((spriteScreenX - target.x) / (std::max)(target.scale, 0.001f),
                                  (spriteScreenY - target.y) / (std::max)(target.scale, 0.001f));
        else if (modelSpaceChanged && !englishInputMode)
            motion.SetPositionDip(motion.MovementCenterDip());
        resources.SetDpiOverride(target.scale * 96.0f);
        placing = true;
        if (moved || resized)
            SetWindowPos(window, nullptr, target.x, target.y, target.width, target.height,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        if (resized)
            resources.Resize(static_cast<UINT>(target.width), static_cast<UINT>(target.height));
        placing = false;
        if (targetChanged)
        {
            // ResizeBuffers/target recreation invalidates D2D bitmap resources
            // created against the old target. Reload all four poses at the new
            // DPI instead of drawing stale COM objects into a blank frame.
            resources.ClearImageCaches();
            blendedPose.Reset();
        }
        renderScale = target.scale;
        placement = target;
        if (moved || resized || targetChanged)
            hitRegionNeedsRefresh = true;
    }

    void BeginFadeOut(const Placement &target, double nowMs, float opacity)
    {
        candidateFollowFrom.reset();
        candidateFollowTarget.reset();
        candidateFollowStartMs = 0.0;
        candidateFollowDurationMs = 0.0;
        pendingPlacement = target;
        placementFade = PlacementFade::Out;
        placementFadeStartMs = nowMs;
        placementFadeStartOpacity = std::clamp(opacity, 0.0f, 1.0f);
        placementFadeDurationMs = (std::max)(30.0, kTeleportFadeOutMs * placementFadeStartOpacity);
        transparentPlacementPresented = false;
    }

    void BeginFadeIn(double nowMs, float opacity)
    {
        placementFade = PlacementFade::In;
        placementFadeStartMs = nowMs;
        placementFadeStartOpacity = std::clamp(opacity, 0.0f, 1.0f);
        placementFadeDurationMs = (std::max)(30.0, kTeleportFadeInMs * (1.0 - placementFadeStartOpacity));
        transparentPlacementPresented = false;
    }

    void AdvanceCandidateFollow(double nowMs)
    {
        if (!candidateFollowTarget || !placement || placementFade == PlacementFade::Out)
            return;
        if (candidateFollowDurationMs <= 0.0)
        {
            ApplyPlacement(*candidateFollowTarget);
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowLastMs = 0.0;
            candidateFollowVelocityX = 0.0;
            candidateFollowVelocityY = 0.0;
            return;
        }
        const Placement &target = *candidateFollowTarget;
        const double dtMs = std::clamp(nowMs - candidateFollowLastMs, 0.0, 100.0);
        if (dtMs > 0.0)
        {
            // Critically damped tracking carries its current velocity forward
            // when another candidate layout arrives. Restarting an ease curve
            // on every width change repeatedly resets the motion and feels
            // like a hitch during rapid edits.
            const double omega = 5.5 / candidateFollowDurationMs;
            const double decay = std::exp(-omega * dtMs);
            const double errorX = candidateFollowX - static_cast<double>(target.x);
            const double constantX = candidateFollowVelocityX + omega * errorX;
            candidateFollowX = static_cast<double>(target.x) + (errorX + constantX * dtMs) * decay;
            candidateFollowVelocityX = (candidateFollowVelocityX - omega * constantX * dtMs) * decay;
            const double errorY = candidateFollowY - static_cast<double>(target.y);
            const double constantY = candidateFollowVelocityY + omega * errorY;
            candidateFollowY = static_cast<double>(target.y) + (errorY + constantY * dtMs) * decay;
            candidateFollowVelocityY = (candidateFollowVelocityY - omega * constantY * dtMs) * decay;
            candidateFollowLastMs = nowMs;
        }
        Placement frame = *placement;
        // Candidate following is only started for same-sized stages. Keep the
        // dimensions and scale exact; interpolating them would recreate the
        // surface while the card is still being laid out.
        frame.x = static_cast<int>(std::lround(candidateFollowX));
        frame.y = static_cast<int>(std::lround(candidateFollowY));
        frame.width = target.width;
        frame.height = target.height;
        frame.scale = target.scale;
        ApplyPlacement(frame);
        if (std::abs(candidateFollowX - target.x) < 0.5 && std::abs(candidateFollowY - target.y) < 0.5 &&
            std::abs(candidateFollowVelocityX) < 0.05 && std::abs(candidateFollowVelocityY) < 0.05)
        {
            ApplyPlacement(target);
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowStartMs = 0.0;
            candidateFollowDurationMs = 0.0;
            candidateFollowLastMs = 0.0;
            candidateFollowVelocityX = 0.0;
            candidateFollowVelocityY = 0.0;
        }
    }

    void BeginCandidateFollow(const Placement &target, double nowMs, double durationMs = kCandidateFollowMs)
    {
        if (!placement || placementFade == PlacementFade::Out || placement->width != target.width ||
            placement->height != target.height || std::abs(placement->scale - target.scale) > 0.001f)
        {
            ApplyPlacement(target);
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowStartMs = 0.0;
            candidateFollowDurationMs = 0.0;
            candidateFollowLastMs = 0.0;
            candidateFollowVelocityX = 0.0;
            candidateFollowVelocityY = 0.0;
            return;
        }
        AdvanceCandidateFollow(nowMs);
        if (candidateFollowTarget)
        {
            // Retarget the active trajectory without resetting its origin,
            // clock or velocity when the candidate card changes width again.
            *candidateFollowTarget = target;
            return;
        }
        if (!placement || SamePlacement(*placement, target))
        {
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowStartMs = 0.0;
            candidateFollowDurationMs = 0.0;
            candidateFollowLastMs = 0.0;
            candidateFollowVelocityX = 0.0;
            candidateFollowVelocityY = 0.0;
            return;
        }
        candidateFollowFrom = *placement;
        candidateFollowTarget = target;
        candidateFollowStartMs = nowMs;
        candidateFollowDurationMs = (std::max)(0.0, durationMs);
        candidateFollowLastMs = nowMs;
        candidateFollowX = static_cast<double>(placement->x);
        candidateFollowY = static_cast<double>(placement->y);
        candidateFollowVelocityX = 0.0;
        candidateFollowVelocityY = 0.0;
    }

    void Place(bool smoothCandidate = false, double smoothDurationMs = kCandidateFollowMs,
               bool smoothLargeTranslation = false)
    {
        motion.SetCandidateAnchorVisible(hasCandidate && !englishInputMode);
        const auto target = CalculatePlacement();
        if (!target)
            return;
        if (placement && HasSameStageGeometry(*placement, *target))
        {
            UpdateCandidateInterval(*target);
            if (chineseAnchoredOnDisplay)
                if (const auto compact = CalculatePlacement(true))
                    FollowChineseFootY((compact->y - target->y) / (std::max)(target->scale, 0.001f) + config->footYDip);
        }
        if (manualOrigin || (pointerDown && manualDisplayBoundaryStage))
        {
            pendingPlacement.reset();
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            placementFade = PlacementFade::None;
            ApplyPlacement(*target);
            return;
        }
        const double nowMs = NowMs();
        smoothCandidate = smoothCandidate || smoothCandidatePlacementRequest;
        smoothCandidatePlacementRequest = false;
        const bool candidateContentResized = candidateContentResizedRequest;
        candidateContentResizedRequest = false;
        // Activity/caret updates are independent from candidate layout.  While
        // a card-follow is already headed to this same anchor, leave it in
        // flight instead of letting a coincident focus packet snap the HWND to
        // the final coordinate.
        if (candidateFollowTarget && SamePlacement(*candidateFollowTarget, *target))
        {
            if (placementFade == PlacementFade::None && AnimationsEnabled())
                AdvanceCandidateFollow(nowMs);
            return;
        }
        if (!placement || !visible || !IsWindowVisible(window) || !AnimationsEnabled())
        {
            pendingPlacement.reset();
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowStartMs = 0.0;
            candidateFollowDurationMs = 0.0;
            placementFade = PlacementFade::None;
            placementFadeStartOpacity = 1.0f;
            ApplyPlacement(*target);
            return;
        }

        if (placementFade == PlacementFade::Out)
        {
            if (SamePlacement(*placement, *target))
            {
                pendingPlacement.reset();
                BeginFadeIn(nowMs, PlacementOpacity(nowMs));
            }
            else
                pendingPlacement = *target;
            return;
        }
        AdvanceCandidateFollow(nowMs);
        if (SamePlacement(*placement, *target))
        {
            if (motion.DisplayBoundaryMode() !=
                    ((englishInputMode || (manualDisplayBoundaryStage && !chineseAnchoredOnDisplay)) &&
                     !manualOrigin) ||
                std::abs(motion.StageWidthDip() -
                         static_cast<double>(target->width) / (std::max)(target->scale, 0.001f)) >= 0.01 ||
                std::abs(motion.StageHeightDip() -
                         static_cast<double>(target->height) / (std::max)(target->scale, 0.001f)) >= 0.01)
                ApplyPlacement(*target);
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowStartMs = 0.0;
            candidateFollowDurationMs = 0.0;
            return;
        }
        const bool teleport = IsTeleport(*placement, *target);
        // Candidate cards are fit-to-content. Their centre can move farther
        // than the caret teleport threshold when a word row grows or shrinks,
        // but that is layout motion rather than a new anchor. Preserve the
        // existing stage surface and follow it frame by frame whenever its
        // geometry/DPI did not change. A large translation without a content
        // size change remains a real relocation and retains the fade protocol.
        if (smoothCandidate && HasSameStageGeometry(*placement, *target) &&
            (!teleport || candidateContentResized || smoothLargeTranslation))
        {
            BeginCandidateFollow(*target, nowMs, smoothDurationMs);
            return;
        }

        if (placementFade == PlacementFade::In && teleport)
        {
            BeginFadeOut(*target, nowMs, PlacementOpacity(nowMs));
            return;
        }
        if (placementFade == PlacementFade::None && teleport)
        {
            BeginFadeOut(*target, nowMs, 1.0f);
            return;
        }
        if (smoothCandidate)
        {
            BeginCandidateFollow(*target, nowMs);
            return;
        }
        ApplyPlacement(*target);
    }

    void AdvancePlacement(double nowMs, bool animationsEnabled)
    {
        if (placementFade == PlacementFade::None)
        {
            if (animationsEnabled)
                AdvanceCandidateFollow(nowMs);
            else
            {
                candidateFollowFrom.reset();
                candidateFollowTarget.reset();
                candidateFollowStartMs = 0.0;
                candidateFollowDurationMs = 0.0;
            }
            return;
        }
        if (!animationsEnabled)
        {
            candidateFollowFrom.reset();
            candidateFollowTarget.reset();
            candidateFollowStartMs = 0.0;
            candidateFollowDurationMs = 0.0;
            if (pendingPlacement)
                ApplyPlacement(*pendingPlacement);
            pendingPlacement.reset();
            placementFade = PlacementFade::None;
            placementFadeStartOpacity = 1.0f;
            return;
        }
        if (candidateFollowTarget)
            AdvanceCandidateFollow(nowMs);
        if (nowMs - placementFadeStartMs < placementFadeDurationMs)
            return;
        if (placementFade == PlacementFade::Out)
        {
            // Present a fully transparent frame at the old location before
            // moving the HWND. Otherwise its last partially visible surface
            // can appear at the destination for one compositor frame.
            if (!transparentPlacementPresented)
                return;
            if (pendingPlacement)
                ApplyPlacement(*pendingPlacement);
            pendingPlacement.reset();
            BeginFadeIn(nowMs, 0.0f);
            return;
        }
        placementFade = PlacementFade::None;
        placementFadeStartOpacity = 1.0f;
    }

    void DrawCombo(ID2D1RenderTarget *target, const LiveSDMotion::Pose &pose, double nowMs, bool animates,
                   float opacity)
    {
        const auto previousGlyphs = comboGlyphs;
        const auto previousGlyphCount = comboGlyphCount;
        comboGlyphCount = 0;
        const float previousOpacity = comboOpacity;
        comboOpacity = static_cast<float>(comboCounter.Opacity(nowMs, animates));
        if ((previousOpacity >= 0.05f) != (comboOpacity >= 0.05f) || (previousOpacity > 0.0f) != (comboOpacity > 0.0f))
            hitRegionNeedsRefresh = true;
        opacity *= comboOpacity;
        if (!config || !config->combo || !placement || opacity <= 0.0f)
            return;
        const auto &settings = *config->combo;
        const auto text = std::to_string(comboCounter.DisplayCount());
        const float pulse = static_cast<float>(comboCounter.Scale(nowMs, animates));
        float digitHeight = static_cast<float>(settings.digitHeightDip) * pulse;
        float slotWidth = static_cast<float>(settings.slotWidthDip) * pulse;
        float labelHeight = static_cast<float>(settings.labelHeightDip);
        float gap = static_cast<float>(settings.labelGapDip);
        D2D1_SIZE_F labelSize{};
        auto *label = resources.GetBitmapFromFile(comboLabelImage, &labelSize);
        if (!label || labelSize.height <= 0.0f || comboLabelMask.alpha.empty())
            return;
        float labelWidth = labelHeight * labelSize.width / labelSize.height;
        float width = (std::max)(labelWidth, slotWidth * static_cast<float>(text.size()));
        float height = digitHeight + gap + labelHeight;
        const float stageWidth = placement->width / placement->scale;
        const float stageHeight = placement->height / placement->scale;
        constexpr float edgeMargin = 2.0f;
        const float fit = (std::min)({1.0f, (std::max)(1.0f, stageWidth - edgeMargin * 2) / width,
                                      (std::max)(1.0f, stageHeight - edgeMargin * 2) / height});
        digitHeight *= fit;
        slotWidth *= fit;
        labelHeight *= fit;
        labelWidth *= fit;
        gap *= fit;
        width *= fit;
        height *= fit;
        const float centre = (std::clamp)(static_cast<float>(pose.xDip + settings.offsetXDip),
                                          edgeMargin + width * 0.5f, stageWidth - edgeMargin - width * 0.5f);
        const float top = (std::clamp)(static_cast<float>(pose.yDip - pose.jumpDip + settings.offsetYDip), edgeMargin,
                                       stageHeight - edgeMargin - height);
        std::array<ID2D1Bitmap *, 3> digits{};
        for (size_t i = 0; i < text.size(); ++i)
        {
            const size_t digit = static_cast<size_t>(text[i] - '0');
            D2D1_SIZE_F size{};
            digits[i] = resources.GetBitmapFromFile(comboImages[digit], &size);
            if (!digits[i] || size.height <= 0.0f || comboMasks[digit].alpha.empty())
            {
                comboGlyphCount = 0;
                return;
            }
            const float glyphWidth = (std::min)(slotWidth, digitHeight * size.width / size.height);
            const float x = centre + (static_cast<float>(i) - static_cast<float>(text.size() - 1) * 0.5f) * slotWidth;
            comboGlyphs[comboGlyphCount++] = {
                &comboMasks[digit], D2D1::RectF(x - glyphWidth * 0.5f, top, x + glyphWidth * 0.5f, top + digitHeight),
                &comboCoverage[digit]};
        }
        comboGlyphs[comboGlyphCount++] = {
            &comboLabelMask,
            D2D1::RectF(centre - labelWidth * 0.5f, top + digitHeight + gap, centre + labelWidth * 0.5f, top + height),
            &comboLabelCoverage};
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> comboContext;
        target->QueryInterface(IID_PPV_ARGS(comboContext.GetAddressOf()));
        const auto drawGlyph = [&](ID2D1Bitmap *bitmap, ComboGlyph &glyph) {
            glyph.footprint =
                glyph.coverage->Place(glyph.destination.left, glyph.destination.top, glyph.destination.right,
                                      glyph.destination.bottom, placement->scale);
            if (comboContext)
                comboContext->DrawBitmap(bitmap, &glyph.destination, opacity,
                                         D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            else
                target->DrawBitmap(bitmap, glyph.destination, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        };
        for (size_t i = 0; i < text.size(); ++i)
            drawGlyph(digits[i], comboGlyphs[i]);
        drawGlyph(label, comboGlyphs[comboGlyphCount - 1]);
        bool moved = previousGlyphCount != comboGlyphCount;
        for (size_t i = 0; i < comboGlyphCount && !moved; ++i)
        {
            const auto &before = previousGlyphs[i].destination;
            const auto &after = comboGlyphs[i].destination;
            moved = before.left != after.left || before.top != after.top || before.right != after.right ||
                    before.bottom != after.bottom;
        }
        if (moved || renderedComboCount != comboCounter.Count() || renderedComboScale != pulse * fit)
        {
            renderedComboCount = comboCounter.Count();
            renderedComboScale = pulse * fit;
            hitRegionNeedsRefresh = true;
        }
    }

    bool Render()
    {
        if (!visible || !config || !window || placing)
            return false;
        if (!IsOwnerCurrent())
        {
            focused = false;
            hasCandidate = false;
            motion.SetCandidateAnchorVisible(false);
            Hide();
            return false;
        }
        if (RefreshConfig())
            Place();
        if (!config)
        {
            Hide();
            return false;
        }
        const auto frameStarted = Clock::now();
        const bool animates = AnimationsEnabled();
        const double nowMs = NowMs();
        if (pendingFocusCaret && nowMs >= pendingFocusCaretDueMs)
        {
            if (!hasCandidate && !manualDisplayBoundaryStage && !manualPositionPinned)
            {
                caret = *pendingFocusCaret;
                keepCandidateAnchor = false;
                awaitingCandidateAfterCaret = true;
                Place();
            }
            pendingFocusCaret.reset();
        }
        AdvancePlacement(nowMs, animates);
        if (!resources.EnsureForComposition(window))
        {
            blendedPose.Reset();
            Hide();
            return false;
        }
        resources.BeginFrame();
        resources.SetAnimationsEnabled(animates);
        if (!resources.BeginDraw())
        {
            blendedPose.Reset();
            Hide();
            return false;
        }
        auto *target = resources.GetRenderTarget();
        target->Clear(D2D1::ColorF(0, 0.0f));
        bool clicked = nowMs < clickUntilMs;
        float clickBlend = clicked ? ClickOpacity(nowMs) : 0.0f;
        if (!pointerDown && motion.DisplayBoundaryMode() && manualScreenXDip)
            motion.SetPositionDipExact(*manualScreenXDip, manualScreenYDip.value_or(motion.Sample(nowMs).yDip));
        LiveSDMotion::Pose pose = pointerDown ? heldPose : VisualPose(nowMs);
        if (!animates)
        {
            pose.jumpDip = 0;
            pose.scaleX = pose.scaleX < 0 ? -1.0 : 1.0;
            pose.scaleY = 1;
            pose.rotationDeg = 0;
            pose.weights = {1, 0, 0, 0};
        }
        D2D1_MATRIX_3X2_F original{};
        target->GetTransform(&original);
        // Ordinary movement is foot anchored.  The Enter aerial preset makes
        // two full planar turns, so rotate and squash around the sprite centre
        // and translate its foot back to the same anchor at 0/360/720 degrees.
        // Rotating around the foot would throw half the canvas below the stage
        // at 180 degrees and look like a sudden crop.
        const float centreY = static_cast<float>(-config->heightDip * 0.5);
        const auto poseTransform =
            D2D1::Matrix3x2F::Translation(0.0f, -centreY) *
            D2D1::Matrix3x2F::Scale(static_cast<float>(pose.scaleX), static_cast<float>(pose.scaleY)) *
            D2D1::Matrix3x2F::Rotation(static_cast<float>(pose.rotationDeg)) *
            D2D1::Matrix3x2F::Translation(static_cast<float>(pose.xDip),
                                          static_cast<float>(pose.yDip - pose.jumpDip + centreY));
        const auto poseDestination =
            D2D1::RectF(static_cast<float>(-config->widthDip / 2), static_cast<float>(-config->heightDip),
                        static_cast<float>(config->widthDip / 2), 0.0f);
        const auto clickTransform =
            D2D1::Matrix3x2F::Translation(static_cast<float>(pose.xDip), static_cast<float>(pose.yDip));
        const auto clickDestination =
            D2D1::RectF(static_cast<float>(-config->widthDip / 2), static_cast<float>(-config->widthDip),
                        static_cast<float>(config->widthDip / 2), 0.0f);
        bool loaded = true;
        std::array<ID2D1Bitmap *, 4> bitmaps{};
        for (size_t i = 0; i < images.size(); ++i)
        {
            // Decode once through DeviceResources' cache, including inactive
            // poses. No per-key image reads and no atlas-coordinate assumptions.
            D2D1_SIZE_F size{};
            auto *bitmap = resources.GetBitmapFromFile(images[i], &size);
            bitmaps[i] = bitmap;
            loaded = loaded && bitmap && size.width == 400.0f && size.height == 460.0f;
        }
        auto *context = resources.GetDeviceContext();
        if (!context)
            loaded = false;
        ID2D1Bitmap *nextClickBitmap = nullptr;
        float clickFrameMix = 0.0f;
        auto *clickBitmap =
            clicked ? resources.GetImageFrameBlend(clickImage, &nextClickBitmap, &clickFrameMix) : nullptr;
        if (clicked && !clickBitmap)
        {
            // A missing optional reaction must not hide the normal LiveSD.
            clickStartMs = 0.0;
            clickUntilMs = 0.0;
            clicked = false;
            clickBlend = 0.0f;
        }
        const float placementOpacity = PlacementOpacity(nowMs);
        const auto previousTargetBlend = context ? context->GetPrimitiveBlend() : D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
        if (clicked && context)
            context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
        if (loaded && context && placementOpacity > 0.0f && (1.0f - clickBlend) > 0.0f)
        {
            if (!blendedPose)
            {
                const auto properties = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_TARGET,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
                loaded = SUCCEEDED(
                    context->CreateBitmap(D2D1::SizeU(400, 460), nullptr, 0, properties, blendedPose.GetAddressOf()));
            }
            if (loaded)
            {
                Microsoft::WRL::ComPtr<ID2D1Image> previousTarget;
                context->GetTarget(previousTarget.GetAddressOf());
                const auto previousBlend = context->GetPrimitiveBlend();
                FLOAT dpiX = 96, dpiY = 96;
                context->GetDpi(&dpiX, &dpiY);
                context->SetTarget(blendedPose.Get());
                context->SetDpi(96, 96);
                context->SetTransform(D2D1::Matrix3x2F::Identity());
                context->Clear(D2D1::ColorF(0, 0.0f));
                // Add premultiplied weighted poses on transparency, then
                // source-over once. Overlapping opaque pixels keep alpha=1.
                context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
                for (size_t i = 0; i < bitmaps.size(); ++i)
                    if (pose.weights[i] > 0)
                        context->DrawBitmap(bitmaps[i], D2D1::RectF(0, 0, 400, 460),
                                            static_cast<float>(pose.weights[i]), D2D1_INTERPOLATION_MODE_LINEAR);
                context->SetTarget(previousTarget.Get());
                context->SetDpi(dpiX, dpiY);
                context->SetPrimitiveBlend(previousBlend);
                context->SetTransform(poseTransform * original);
                target->DrawBitmap(blendedPose.Get(), poseDestination, placementOpacity * (1.0f - clickBlend),
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }
        if (loaded && context && clicked && clickBitmap && clickBlend > 0.0f)
        {
            target->SetTransform(clickTransform * original);
            target->DrawBitmap(clickBitmap, clickDestination, placementOpacity * clickBlend * (1.0f - clickFrameMix),
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (nextClickBitmap && clickFrameMix > 0.0f)
                target->DrawBitmap(nextClickBitmap, clickDestination, placementOpacity * clickBlend * clickFrameMix,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        if (context)
            context->SetPrimitiveBlend(previousTargetBlend);
        target->SetTransform(original);
        DrawCombo(target, pose, nowMs, animates, placementOpacity);
        if (!(pointerDown && dragging && dragHitRegionRelaxed))
            UpdateHitRegion(poseTransform, poseDestination, clickTransform, clickDestination, pose, clicked, clickBlend,
                            placementOpacity);
        const HRESULT result = resources.EndDraw();
        if (result == D2DERR_RECREATE_TARGET || FAILED(result) || FAILED(resources.Present()))
        {
            resources.DiscardTarget();
            blendedPose.Reset();
            Hide();
            return false;
        }
        if (!loaded)
        {
            Hide();
            return false;
        }
        renderedFrames.fetch_add(1, std::memory_order_relaxed);
        const auto renderDuration = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - frameStarted).count());
        const auto durationIndex = renderDurationCount.fetch_add(1, std::memory_order_relaxed);
        renderDurationsNs[durationIndex % renderDurationsNs.size()].store(renderDuration, std::memory_order_relaxed);
        auto maximum = maximumRenderDurationNs.load(std::memory_order_relaxed);
        while (maximum < renderDuration &&
               !maximumRenderDurationNs.compare_exchange_weak(maximum, renderDuration, std::memory_order_relaxed))
        {
        }
        if (placementFade == PlacementFade::Out && nowMs - placementFadeStartMs >= placementFadeDurationMs)
            transparentPlacementPresented = true;
        if (revealAfterPresent)
        {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            revealAfterPresent = false;
        }
        // The motion model continuously changes the idle breath as well as
        // typing/turn/Enter poses, so normal motion uses the 120 Hz clock.
        // Reduced-motion mode retains its low-frequency ownership check.
        ScheduleNextFrame(frameStarted, animates || pointerDown);
        return true;
    }
};

std::unique_ptr<Host> host;

bool ShowActivityOnUiThread(const Activity &activity)
{
    if (!IsPipeActivationCurrent(activity.client, activity.epoch))
        return false;
    if (!host)
        host = std::make_unique<Host>();
    const bool hasCaret = activity.caret.y != kInvalidCaretY;
    const bool changed = !host->focused || host->client != activity.client || host->epoch != activity.epoch;
    const bool isActionUpdate = activity.kind == 1 || activity.kind == 3 || activity.kind == 4;
    if (isActionUpdate)
    {
        // Input actions never change the anchor, even when TSF includes a
        // readable caret. Updating the motion model is enough: the outstanding
        // animation frame samples it on the next active tick. Drawing synchronously here
        // would render an extra full frame for every key, often immediately
        // before the already-scheduled frame.
        const bool recoverableEnglishActivity = !host->focused && host->englishInputMode && !hasCaret &&
                                                (activity.kind == 1 || activity.kind == 3) &&
                                                host->client == activity.client && host->epoch == activity.epoch &&
                                                host->config && host->placement && IsWindow(host->window);
        if ((!host->focused && !recoverableEnglishActivity) || host->client != activity.client ||
            host->epoch != activity.epoch)
            return false;
        if (recoverableEnglishActivity)
        {
            // A real focus loss hides the existing companion, but a browser or
            // editor may return focus before it exposes a caret again.  A
            // sentinel action may revive that same English stage in place;
            // it must never create a window or recalculate its placement.
            host->focused = true;
            if (!host->IsOwnerCurrent())
            {
                host->focused = false;
                return false;
            }
            host->visible = true;
            if (!host->Render())
                return false;
            EnsureSmallWindowsTopmost(L"livesd-focus-recover");
            SyncTopmost();
            ShowWindow(host->window, SW_SHOWNOACTIVATE);
        }
        if (!host->IsOwnerCurrent())
        {
            host->focused = false;
            host->hasCandidate = false;
            host->motion.SetCandidateAnchorVisible(false);
            host->Hide();
            return false;
        }
        if (activity.kind == 4)
        {
            // Ctrl+Z is a passive LiveSD command. Toggle a fixed English
            // position and leave the key available to the host application.
            if (!host->englishInputMode)
                return host->visible && IsWindowVisible(host->window);
            host->manualPositionPinned = !host->manualPositionPinned;
            if (host->manualPositionPinned)
            {
                const auto pose = host->motion.Sample(NowMs());
                host->manualScreenXDip = pose.xDip;
                host->manualScreenYDip = pose.yDip;
            }
            else
            {
                host->manualScreenXDip.reset();
                host->manualScreenYDip.reset();
            }
            return host->visible && IsWindowVisible(host->window);
        }
        if (activity.kind == 1)
        {
            // A released English drag holds while idle. The next insertion
            // resumes roaming from that position instead of pinning every
            // animation frame to a zero-velocity manual anchor.
            if (!host->pointerDown)
            {
                if (!host->englishInputMode)
                    host->ReturnChineseToAnchor();
                else if (!host->manualPositionPinned)
                {
                    host->manualScreenXDip.reset();
                    host->manualScreenYDip.reset();
                }
            }
            host->motion.Pulse(NowMs(), activity.typingIntervalMs);
        }
        else if (activity.kind == 3)
            host->motion.Enter(NowMs());
        return host->visible && IsWindowVisible(host->window);
    }
    host->client = activity.client;
    host->epoch = activity.epoch;
    if (changed)
    {
        host->caret = activity.caret;
        host->lastFocusCaret = activity.caret;
        host->contextRootWindow = activity.contextRootWindow;
    }
    else if (activity.contextRootWindow)
        host->contextRootWindow = activity.contextRootWindow;
    host->focused = true;
    // A delayed activity packet from this epoch must not roll back a newer
    // authoritative StatusSnapshot. Activities seed mode only at focus entry.
    if (changed)
        host->englishInputMode = pendingInputModeState && pendingInputModeState->client == activity.client &&
                                         pendingInputModeState->epoch == activity.epoch
                                     ? pendingInputModeState->englishInputMode
                                     : activity.englishInputMode;
    if (changed)
    {
        host->Hide();
        host->manualOrigin.reset();
        host->manualScreenXDip.reset();
        host->manualScreenYDip.reset();
        host->manualDisplayBoundaryStage = false;
        host->manualPositionPinned = false;
        host->hasCandidate = false;
        host->keepCandidateAnchor = false;
        host->candidateStageLeftOffsetDip.reset();
        host->motion.SetCandidateAnchorVisible(false);
        host->motion.Reset(NowMs());
    }
    if (candidateAnchor && candidateAnchor->client == activity.client && candidateAnchor->epoch == activity.epoch)
    {
        host->candidate = candidateAnchor->card;
        host->candidateScale = candidateAnchor->scale;
        host->hasCandidate = true;
        host->keepCandidateAnchor = false;
        host->motion.SetCandidateAnchorVisible(!host->englishInputMode);
    }
    host->RefreshConfig();
    if (!host->config || !host->EnsureWindow() || !host->IsOwnerCurrent())
    {
        host->Hide();
        return false;
    }
    if (activity.kind == 1)
        host->motion.Pulse(NowMs(), activity.typingIntervalMs);
    else if (activity.kind == 3)
        host->motion.Enter(NowMs());
    // Focus notifications are also emitted after each resolved edit session,
    // but their caret is not a LiveSD animation anchor. Keep the stage stable
    // for ordinary character-sized caret advances; a larger jump is treated as
    // a real relocation and is handled by Place()'s teleport fade. Typing
    // pulses never update placement, even when they carry a readable caret.
    if (!changed && activity.kind == 0 && hasCaret)
    {
        const int dx = activity.caret.x - host->lastFocusCaret.x;
        const int dy = activity.caret.y - host->lastFocusCaret.y;
        host->lastFocusCaret = activity.caret;
        const float scale = GetScaleForPoint(activity.caret);
        if (!host->hasCandidate && !host->manualPositionPinned && !host->manualDisplayBoundaryStage &&
            (std::max)(std::abs(dx), std::abs(dy)) >= kTeleportDistanceDip * (scale > 0 ? scale : 1.0f))
        {
            if (!host->englishInputMode && AnimationsEnabled())
            {
                // A first-key caret notification may precede the authoritative
                // candidate layout. Coalesce them before starting a relocation.
                host->pendingFocusCaret = activity.caret;
                host->pendingFocusCaretDueMs = NowMs() + kFocusAnchorWaitMs;
            }
            else
            {
                host->caret = activity.caret;
                host->keepCandidateAnchor = false;
            }
        }
    }
    const bool showing = !host->visible || !IsWindowVisible(host->window);
    host->Place();
    if (showing && AnimationsEnabled())
        host->BeginFadeIn(NowMs(), 0.0f);
    host->visible = true;
    if (!showing && !changed && AnimationsEnabled())
    {
        // Per-key Focus packets can arrive alongside a TypingPulse and the
        // candidate-anchor notification. The active frame clock already
        // samples the newest pose and placement; a synchronous extra draw
        // here would duplicate presentation for the same keystroke.
        return true;
    }
    // Draw before showing so no previous focus session flashes at the new anchor.
    if (!host->Render())
        return false;
    if (showing)
    {
        EnsureSmallWindowsTopmost(L"livesd-focus");
        SyncTopmost();
        ShowWindow(host->window, SW_SHOWNOACTIVATE);
    }
    return true;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!host)
        return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message)
    {
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        return host->hitRegion && PtInRegion(host->hitRegion, point.x, point.y) ? HTCLIENT : HTTRANSPARENT;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONDOWN:
        if (host->placement)
        {
            host->pointerDown = true;
            host->dragging = false;
            host->pointerStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &host->pointerStart);
            host->pointerLast = host->pointerStart;
            host->dragOrigin = {host->placement->x, host->placement->y};
            const auto grabbed = host->VisualPose(NowMs());
            host->dragPoseStartXDip = grabbed.xDip;
            host->dragPoseStartYDip = grabbed.yDip;
            if (host->motion.DisplayBoundaryMode() && host->manualScreenXDip)
                host->dragPoseStartXDip = *host->manualScreenXDip;
            if (host->motion.DisplayBoundaryMode() && host->manualScreenYDip)
                host->dragPoseStartYDip = *host->manualScreenYDip;
            if (host->clickUntilMs <= NowMs())
                host->heldPose = grabbed;
            if (host->motion.DisplayBoundaryMode() && host->manualScreenXDip)
                host->heldPose.xDip = *host->manualScreenXDip;
            if (host->motion.DisplayBoundaryMode() && host->manualScreenYDip)
                host->heldPose.yDip = *host->manualScreenYDip;
            SetCapture(hwnd);
            // WM_TIMER has a 10 ms platform minimum; the high-resolution frame
            // scheduler below also polls the pointer on each active tick.
            SetTimer(hwnd, kPointerTimer, 10, nullptr);
            host->ScheduleFrameAt(Clock::now() + kActiveFrameInterval);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (host->pointerDown)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &point);
            host->MovePointer(point);
        }
        return 0;
    case WM_LBUTTONUP:
        if (host->pointerDown)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &point);
            host->FinishPointer(point);
        }
        return 0;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE: {
        const bool restoreHitRegion = host->dragHitRegionRelaxed;
        host->CancelPointer();
        if (restoreHitRegion && host->visible)
            host->Render();
    }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        if (!(host->pointerDown && host->dragging))
            host->Render();
        EndPaint(hwnd, &paint);
        return 0;
    }
    case kFrameMessage:
        if (host->scheduler.ConsumeMessage(wParam, lParam))
        {
            RecordFrameWake(host->frameDeadline.value_or(Clock::now()));
            if (host->pointerDown)
                host->PollPointer();
            host->Render();
        }
        return 0;
    case WM_TIMER:
        if (wParam == kPointerTimer)
        {
            host->PollPointer();
            return 0;
        }
        if (wParam == kFrameTimer)
        {
            KillTimer(hwnd, kFrameTimer);
            RecordFrameWake(host->frameDeadline.value_or(Clock::now()));
            host->Render();
        }
        return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
        if (!host->placing)
        {
            host->Place();
            host->Render();
        }
        return 0;
    case WM_DESTROY:
        host->CancelPointer();
        host->clickStartMs = 0;
        host->clickUntilMs = 0;
        if (host->hitRegion)
        {
            DeleteObject(host->hitRegion);
            host->hitRegion = nullptr;
        }
        host->scheduler.Bind(nullptr, 0);
        host->blendedPose.Reset();
        host->resources.DiscardTarget();
        host->placement.reset();
        host->pendingPlacement.reset();
        host->candidateFollowFrom.reset();
        host->candidateFollowTarget.reset();
        host->candidateFollowStartMs = 0.0;
        host->candidateFollowDurationMs = 0.0;
        host->smoothCandidatePlacementRequest = false;
        host->candidateContentResizedRequest = false;
        host->placementFade = Host::PlacementFade::None;
        host->transparentPlacementPresented = false;
        host->revealAfterPresent = false;
        host->renderScale = 0.0f;
        host->visible = false;
        host->window = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool Host::EnsureWindow()
{
    if (window)
        return true;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    // An alpha-derived window region limits both rendering and cross-process
    // input to the visible sprite. NOACTIVATE retains the editor's TSF focus.
    window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             kWindowClass, L"LiveSD", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!window)
        return false;
    // Visual opacity remains on the DComp surface.
    if (!SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA) || !resources.EnsureForComposition(window))
    {
        DestroyWindow(window);
        window = nullptr;
        return false;
    }
    scheduler.Bind(window, kFrameMessage);
    return true;
}
} // namespace

void PostActivity(const Activity &activity)
{
    const std::lock_guard lock(postMutex);
    if (!acceptingActivity)
        return;
    if (!::global_hwnd)
        return;
    auto *copy = new (std::nothrow) Activity(activity);
    if (copy && !PostMessageW(::global_hwnd, kActivityMessage, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

void PostInputModeState(std::uint64_t client, std::uint64_t epoch, bool englishInputMode)
{
    const std::lock_guard lock(postMutex);
    if (!acceptingActivity || !::global_hwnd || client == 0 || epoch == 0)
        return;
    auto *update = new (std::nothrow) InputModeUpdate{client, epoch, englishInputMode};
    if (update && !PostMessageW(::global_hwnd, kInputModeMessage, 0, reinterpret_cast<LPARAM>(update)))
        delete update;
}

bool HandleInputModeStateMessage(LPARAM payload)
{
    const std::unique_ptr<InputModeUpdate> update(reinterpret_cast<InputModeUpdate *>(payload));
    if (!update || !IsPipeActivationCurrent(update->client, update->epoch))
        return false;
    pendingInputModeState = *update;
    if (!host || !host->focused || host->client != update->client || host->epoch != update->epoch ||
        host->englishInputMode == update->englishInputMode)
        return true;
    host->CancelPointer();
    host->englishInputMode = update->englishInputMode;
    host->manualOrigin.reset();
    host->manualScreenXDip.reset();
    host->manualScreenYDip.reset();
    host->manualDisplayBoundaryStage = false;
    host->chineseAnchoredOnDisplay = false;
    host->chineseReturn.reset();
    host->manualPositionPinned = false;
    host->pendingFocusCaret.reset();
    host->awaitingCandidateAfterCaret = false;
    host->Place();
    host->Render();
    return true;
}

bool HandleActivityMessage(LPARAM payload)
{
    const std::unique_ptr<Activity> activity(reinterpret_cast<Activity *>(payload));
    if (!activity)
        return false;
    // Server lifecycle cleanup has no client id because routing is already
    // retired. Accept it only for the active host's current-or-newer epoch;
    // delayed cleanup from an older focus session must not hide a new owner.
    if (activity->kind == 2 && activity->client == 0)
    {
        if (!host || activity->epoch < host->epoch)
            return false;
        host->focused = false;
        host->hasCandidate = false;
        host->Hide();
        return true;
    }
    if (!IsPipeActivationCurrent(activity->client, activity->epoch))
        return false;
    if (activity->kind == FanyImeLiveSDActivity::ComboTyping || activity->kind == FanyImeLiveSDActivity::ComboReset)
    {
        // Ordered, caret-independent decoration events arrive before the
        // delayed movement batch. Retain the first character even when no
        // companion HWND exists, and reset regardless of Enter arbitration.
        if (activity->kind == FanyImeLiveSDActivity::ComboTyping)
            comboCounter.Increment(NowMs());
        else
            comboCounter.Reset(NowMs());
        if (host)
        {
            host->hitRegionNeedsRefresh = true;
            if (host->visible && !AnimationsEnabled())
                host->Render();
        }
        return true;
    }
    if (activity->kind == 2)
    {
        if (host)
        {
            host->focused = false;
            host->hasCandidate = false;
            host->Hide();
        }
        return true;
    }
    return ShowActivityOnUiThread(*activity);
}

void SetCandidateAnchor(const RECT &cardPixels, float scale)
{
    const auto owner = GetActivePipeClient();
    candidateAnchor = CandidateAnchor{cardPixels, scale > 0 ? scale : 1.0f, owner.client_id, owner.epoch};
    if (owner.client_id == 0 || owner.epoch == 0)
        return;
    const HWND foreground = GetForegroundWindow();
    if (!foreground || !IsPipeActivationCurrent(owner.client_id, owner.epoch))
        return;
    // This anchor is published only while presenting the active client's
    // candidate window. Bind that session to the foreground root even when its
    // TSF implementation runs in a proxy process.
    HWND contextRootWindow = foreground ? GetAncestor(foreground, GA_ROOTOWNER) : nullptr;
    if (!contextRootWindow)
        contextRootWindow = foreground;
    if (!host || !host->IsOwnerCurrent())
    {
        ShowActivityOnUiThread({owner.client_id,
                                owner.epoch,
                                0,
                                {cardPixels.left + (cardPixels.right - cardPixels.left) / 2, cardPixels.top},
                                contextRootWindow,
                                0});
        return;
    }
    const bool candidateAppeared = !host->hasCandidate;
    host->pendingFocusCaret.reset();
    if (candidateAppeared && host->awaitingCandidateAfterCaret)
    {
        // Some editors publish the first card after the caret grace period.
        // Replace an in-flight speculative relocation with continuous card
        // following instead of completing a stale fade-out/fade-in first.
        const double nowMs = NowMs();
        const float opacity = host->PlacementOpacity(nowMs);
        host->pendingPlacement.reset();
        host->placementFade = Host::PlacementFade::None;
        host->transparentPlacementPresented = false;
        if (AnimationsEnabled() && opacity < 1.0f)
            host->BeginFadeIn(nowMs, opacity);
    }
    host->awaitingCandidateAfterCaret = false;
    if (candidateAppeared)
        host->candidateStageLeftOffsetDip.reset();
    const int oldWidth = host->candidate.right - host->candidate.left;
    const int oldHeight = host->candidate.bottom - host->candidate.top;
    const int newWidth = cardPixels.right - cardPixels.left;
    const int newHeight = cardPixels.bottom - cardPixels.top;
    host->candidateContentResizedRequest = host->hasCandidate && (oldWidth != newWidth || oldHeight != newHeight);
    host->candidate = cardPixels;
    host->candidateScale = scale > 0 ? scale : 1.0f;
    host->hasCandidate = true;
    host->keepCandidateAnchor = false;
    host->motion.SetCandidateAnchorVisible(!host->englishInputMode);
    host->contextRootWindow = contextRootWindow;
    host->smoothCandidatePlacementRequest = true;
    host->ReturnChineseToAnchor();
    host->Place(true, kCandidateFollowMs, candidateAppeared);
    // Candidate layout can report several times while the first characters
    // resize the row. The existing frame clock already samples the newest
    // anchor; avoid an extra synchronous D2D present per layout notification.
    if (!host->visible || !IsWindowVisible(host->window) || !AnimationsEnabled())
        host->Render();
}

void ClearCandidateAnchor()
{
    candidateAnchor.reset();
    if (!host || !host->hasCandidate)
        return;
    host->hasCandidate = false;
    host->keepCandidateAnchor = true;
    host->motion.SetCandidateAnchorVisible(false);
    if (host->englishInputMode)
    {
        host->Place();
        host->Render();
    }
}

void SyncTopmost()
{
    if (host && host->window && ::global_hwnd && (GetWindowLongPtrW(::global_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        SetWindowPos(host->window, ::global_hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

FrameTimingTelemetry GetFrameTimingTelemetry()
{
    FrameTimingTelemetry result;
    result.scheduled = scheduledFrameWakes.load(std::memory_order_relaxed);
    result.rendered = renderedFrames.load(std::memory_order_relaxed);
    result.missed = missedFrameSlots.load(std::memory_order_relaxed);
    result.maxLatenessNs = maximumFrameLatenessNs.load(std::memory_order_relaxed);
    result.maxRenderDurationNs = maximumRenderDurationNs.load(std::memory_order_relaxed);
    result.highResolutionTimer = host && host->scheduler.IsHighResolution();
    result.timerResolutionRaised = false;
    result.waitableTimer = host && host->frameWaitableTimer;
    const auto intervalCount = frameWakeIntervalCount.load(std::memory_order_relaxed);
    result.intervalTotalCount = intervalCount;
    result.intervalSampleCount = (std::min)(intervalCount, static_cast<std::uint64_t>(result.intervalSamplesNs.size()));
    const auto firstInterval = intervalCount - result.intervalSampleCount;
    for (std::uint64_t index = 0; index < result.intervalSampleCount; ++index)
    {
        const auto slot = (firstInterval + index) % frameWakeIntervalsNs.size();
        result.intervalSamplesNs[index] = frameWakeIntervalsNs[slot].load(std::memory_order_relaxed);
    }
    const auto durationCount = renderDurationCount.load(std::memory_order_relaxed);
    result.renderDurationTotalCount = durationCount;
    result.renderDurationSampleCount =
        (std::min)(durationCount, static_cast<std::uint64_t>(result.renderDurationsNs.size()));
    const auto firstDuration = durationCount - result.renderDurationSampleCount;
    for (std::uint64_t index = 0; index < result.renderDurationSampleCount; ++index)
    {
        const auto slot = (firstDuration + index) % renderDurationsNs.size();
        result.renderDurationsNs[index] = renderDurationsNs[slot].load(std::memory_order_relaxed);
    }
    return result;
}

void Shutdown()
{
    const std::lock_guard lock(postMutex);
    acceptingActivity = false;
    candidateAnchor.reset();
    pendingInputModeState.reset();
    if (host)
    {
        host->Hide();
        if (host->window)
            DestroyWindow(host->window);
        host.reset();
    }
    MSG pending{};
    while (PeekMessageW(&pending, ::global_hwnd, kActivityMessage, kActivityMessage, PM_REMOVE))
        delete reinterpret_cast<Activity *>(pending.lParam);
    while (PeekMessageW(&pending, ::global_hwnd, kInputModeMessage, kInputModeMessage, PM_REMOVE))
        delete reinterpret_cast<InputModeUpdate *>(pending.lParam);
}
} // namespace LiveSDHost
