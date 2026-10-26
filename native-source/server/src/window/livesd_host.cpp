#include "window/livesd_host.h"

#include "config/ime_config.h"
#include "defines/globals.h"
#include "ipc/ipc.h"
#include "msimeui/DeviceResources.h"
#include "skin/candidate_skin_catalog.h"
#include "utils/common_utils.h"
#include "utils/window_utils.h"
#include "window/candidate_animation_scheduler.h"
#include "window/livesd_motion.h"
#include "window/small_windows.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
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
constexpr LONG kInvalidCaretY = -100000;
constexpr float kTeleportDistanceDip = 48.0f;
constexpr double kTeleportFadeOutMs = 100.0;
constexpr double kTeleportFadeInMs = 120.0;
constexpr double kClickFadeInMs = 180.0;
constexpr double kClickFadeOutMs = 180.0;
constexpr double kDragReturnMs = 120.0;
// Candidate cards are fit-to-content, so their centre can move by a few
// pixels on every edit (especially while undo is held).  Follow those small
// anchor changes over a short, frame-driven interval instead of moving the
// companion HWND synchronously on every layout notification.
constexpr double kCandidateFollowMs = 88.0;
constexpr wchar_t kWindowClass[] = L"MetasequoiaLiveSD";
std::mutex postMutex;
bool acceptingActivity = true;
struct CandidateAnchor
{
    RECT card{};
    float scale = 1.0f;
    std::uint64_t client = 0;
    std::uint64_t epoch = 0;
};
std::optional<CandidateAnchor> candidateAnchor;

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
    LiveSDMotion::Model motion;
    std::optional<CandidateSkinCatalog::LiveSD> config;
    std::array<std::wstring, 4> images;
    std::wstring clickImage;
    struct AlphaMask
    {
        UINT width = 0, height = 0;
        std::vector<BYTE> alpha;
    };
    std::array<AlphaMask, 4> poseMasks;
    AlphaMask clickMask;
    HRGN hitRegion = nullptr;
    bool pointerDown = false;
    bool dragging = false;
    POINT pointerStart{};
    POINT dragOrigin{};
    std::optional<POINT> manualOrigin;
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

    void UpdateHitRegion(const D2D1_MATRIX_3X2_F &poseTransform, const D2D1_RECT_F &poseDestination,
                         const D2D1_MATRIX_3X2_F &clickTransform, const D2D1_RECT_F &clickDestination,
                         const LiveSDMotion::Pose &pose, bool clicked, float clickOpacity, float opacity)
    {
        if (!placement)
            return;
        const auto makeInverse = [](const D2D1_MATRIX_3X2_F &transform) {
            auto inverse = D2D1::Matrix3x2F(transform._11, transform._12, transform._21, transform._22, transform._31,
                                            transform._32);
            return inverse;
        };
        auto poseInverse = makeInverse(poseTransform);
        const bool poseInvertible = poseInverse.Invert();
        auto clickInverse = makeInverse(clickTransform);
        const bool clickInvertible = clickInverse.Invert();
        std::vector<RECT> spans;
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
            if ((!clicked || (1.0f - clickOpacity) * opacity >= 0.05f))
                for (size_t i = 0; i < poseMasks.size(); ++i)
                    if (pose.weights[i] > 0.001 && sample(poseInverse, poseInvertible, poseDestination, poseMasks[i]))
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
        const auto poseCorners = bounds(poseTransform, poseDestination);
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
            std::vector<DWORD> data((bytes + sizeof(DWORD) - 1) / sizeof(DWORD));
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
            return;
        }
        if (hitRegion)
            DeleteObject(hitRegion);
        hitRegion = next;
        // Window regions participate in cross-process hit testing. The empty
        // stage is outside the HWND, rather than relying on HTTRANSPARENT.
        HRGN ownedByWindow = CreateRectRgn(0, 0, 0, 0);
        CombineRgn(ownedByWindow, hitRegion, nullptr, RGN_COPY);
        if (!SetWindowRgn(window, ownedByWindow, FALSE))
            DeleteObject(ownedByWindow);
    }

    void StartClick()
    {
        if (!config || clickImage.empty() || clickMask.alpha.empty())
            return;
        const double nowMs = NowMs();
        heldPose = motion.Sample(nowMs);
        clickStartMs = nowMs;
        clickUntilMs = clickStartMs + config->clickDurationMs;
        resources.SetAnimationEpoch(Clock::now());
        Render();
    }

    void CancelPointer()
    {
        pointerDown = false;
        dragging = false;
        if (window)
            KillTimer(window, kPointerTimer);
        if (GetCapture() == window)
            ReleaseCapture();
    }

    bool MovePointer(POINT point)
    {
        if (!pointerDown || !placement)
            return false;
        const int dx = point.x - pointerStart.x, dy = point.y - pointerStart.y;
        const UINT dpi = static_cast<UINT>(std::lround(placement->scale * 96.0f));
        dragging = dragging || std::abs(dx) >= GetSystemMetricsForDpi(SM_CXDRAG, dpi) ||
                   std::abs(dy) >= GetSystemMetricsForDpi(SM_CYDRAG, dpi);
        if (!dragging)
            return false;
        manualOrigin = POINT{dragOrigin.x + dx, dragOrigin.y + dy};
        Place();
        return true;
    }

    void FinishPointer(POINT point)
    {
        if (!pointerDown)
            return;
        MovePointer(point);
        const bool click = !dragging;
        CancelPointer();
        if (click)
            StartClick();
        else
        {
            // Preserve the dragged location until a candidate card appears.
            // That transition owns the return to the candidate anchor.
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
            CancelPointer();
            return;
        }
        // A no-activate background HWND cannot capture mouse messages outside
        // its visible region. Track only the active press, including release,
        // without taking focus or installing a global mouse hook.
        const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
        if (!(GetAsyncKeyState(button) & 0x8000))
            FinishPointer(point);
        else if (MovePointer(point))
            Render();
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
    bool keepCandidateAnchor = false;
    bool candidateContentResizedRequest = false;
    bool focused = false;
    bool visible = false;
    bool placing = false;
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
        if (window)
        {
            KillTimer(window, kFrameTimer);
            ShowWindow(window, SW_HIDE);
        }
        visible = false;
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
        config.reset();
        const auto root = std::filesystem::path(CommonUtils::get_ime_data_path_w()) / L"skins";
        const auto package = CandidateSkinCatalog::LoadSupported(root, id, layout, light ? "light" : "dark");
        if (!package || !package->liveSD)
            return true;
        config = package->liveSD;
        const auto folder = root / std::filesystem::u8path(id);
        clickImage = config->clickImage.empty() ? std::wstring()
                                                : (folder / std::filesystem::u8path(config->clickImage)).wstring();
        clickMask = LoadAlpha(clickImage);
        clickStartMs = 0.0;
        clickUntilMs = 0.0;
        const std::array<std::string, 4> relative = {config->idleImage, config->idle2Image, config->happyImage,
                                                     config->missImage};
        for (size_t i = 0; i < images.size(); ++i)
        {
            images[i] = (folder / std::filesystem::u8path(relative[i])).wstring();
            poseMasks[i] = LoadAlpha(images[i]);
        }
        motion = LiveSDMotion::Model(
            {config->stageWidthDip, config->widthDip, config->heightDip, config->stageHeightDip, config->footYDip});
        motion.Reset(NowMs());
        motion.SetCandidateAnchorVisible(hasCandidate);
        return true;
    }

    bool EnsureWindow();

    std::optional<Placement> CalculatePlacement()
    {
        if (!config || !window)
            return std::nullopt;
        // A visible candidate is the authoritative anchor. Activity messages
        // can carry a newer caret while the candidate card is still settling;
        // using that caret here would make the mascot jump with text insertion
        // instead of staying above the card.
        // Closing a candidate card must not pull the stage back to the caret.
        // Retain its last anchor until a new card or a substantial caret jump.
        const bool useCandidateAnchor = hasCandidate || keepCandidateAnchor;
        const POINT placementPoint =
            manualOrigin && placement
                ? POINT{manualOrigin->x + placement->width / 2, manualOrigin->y + placement->height / 2}
                : (useCandidateAnchor ? POINT{candidate.left, candidate.top} : caret);
        const float queriedScale = GetScaleForPoint(placementPoint);
        float scale =
            useCandidateAnchor && !manualOrigin ? candidateScale : (queriedScale > 0.0f ? queriedScale : 1.0f);
        MONITORINFO monitor{sizeof(monitor)};
        RECT work{};
        if (GetMonitorInfoW(MonitorFromPoint(placementPoint, MONITOR_DEFAULTTONEAREST), &monitor))
            work = monitor.rcWork;
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        if (work.right <= work.left || work.bottom <= work.top)
            return std::nullopt;
        // A small work area can be narrower than the configured DIP stage.
        // Fit the complete stage proportionally before clamping its origin.
        scale = (std::min)(scale, static_cast<float>((work.right - work.left) / config->stageWidthDip));
        scale = (std::min)(scale, static_cast<float>((work.bottom - work.top) / config->stageHeightDip));
        const int width = (std::max)(1, (std::min)(static_cast<int>(std::ceil(config->stageWidthDip * scale)),
                                                   static_cast<int>(work.right - work.left)));
        const int height = (std::max)(1, (std::min)(static_cast<int>(std::ceil(config->stageHeightDip * scale)),
                                                    static_cast<int>(work.bottom - work.top)));
        int x = caret.x - static_cast<int>(config->stageWidthDip * 0.25 * scale);
        if (useCandidateAnchor)
        {
            // Capture the initial centring offset for this candidate session.
            // The card grows to the right as candidates are appended; deriving
            // the offset from its live width would push the whole movement area
            // right by half of every width increase.
            const double initialOffsetDip =
                ((candidate.right - candidate.left) - width) / (2.0 * (std::max)(scale, 0.001f));
            if (!candidateStageLeftOffsetDip)
                candidateStageLeftOffsetDip = initialOffsetDip;
            x = candidate.left + static_cast<int>(std::lround(*candidateStageLeftOffsetDip * scale));
        }
        int y = (useCandidateAnchor ? candidate.top : caret.y) - height;
        if (useCandidateAnchor && y < work.top && candidate.bottom + height <= work.bottom)
            y = candidate.bottom;
        if (manualOrigin)
        {
            x = manualOrigin->x;
            y = manualOrigin->y;
        }
        x = (std::clamp)(x, static_cast<int>(work.left),
                         (std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - width));
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

    void ApplyPlacement(const Placement &target)
    {
        const bool moved = !placement || placement->x != target.x || placement->y != target.y;
        const bool resized = !placement || placement->width != target.width || placement->height != target.height;
        const bool targetChanged = resized || !placement || std::abs(renderScale - target.scale) > 0.001f;
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
        const auto target = CalculatePlacement();
        if (!target)
            return;
        if (manualOrigin)
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
        const bool animates = AnimationsEnabled();
        const double nowMs = NowMs();
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
        LiveSDMotion::Pose pose = pointerDown ? heldPose : motion.Sample(nowMs);
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
                                          static_cast<float>(config->footYDip - pose.jumpDip + centreY));
        const auto poseDestination =
            D2D1::RectF(static_cast<float>(-config->widthDip / 2), static_cast<float>(-config->heightDip),
                        static_cast<float>(config->widthDip / 2), 0.0f);
        const auto clickTransform =
            D2D1::Matrix3x2F::Translation(static_cast<float>(pose.xDip), static_cast<float>(config->footYDip));
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
        auto *clickBitmap = clicked ? resources.GetImageFrame(clickImage) : nullptr;
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
            target->DrawBitmap(clickBitmap, clickDestination, placementOpacity * clickBlend,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        if (context)
            context->SetPrimitiveBlend(previousTargetBlend);
        target->SetTransform(original);
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
        if (placementFade == PlacementFade::Out && nowMs - placementFadeStartMs >= placementFadeDurationMs)
            transparentPlacementPresented = true;
        // One outstanding wake-up, same scheduler primitive as candidates.
        // Reduced motion still checks focus; it never animates the sprite.
        const auto delay = animates ? std::chrono::milliseconds(motion.NeedsTurnFrameCadence() ? 12 : 16)
                                    : std::chrono::milliseconds(250);
        if (!scheduler.Schedule(Clock::now() + delay))
            SetTimer(window, kFrameTimer, static_cast<UINT>(delay.count()), nullptr);
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
    const bool actionWithoutCaret = (activity.kind == 1 || activity.kind == 3) && !hasCaret;
    if (actionWithoutCaret)
    {
        // Some TSF hosts expose a valid focus session and candidate window but
        // no readable collapsed caret. The pulse still belongs to the current
        // host; using its old anchor keeps the mascot above the candidate and
        // prevents an unresolved point from being clamped onto a monitor edge.
        if (!host->focused || host->client != activity.client || host->epoch != activity.epoch)
            return false;
        if (!host->IsOwnerCurrent())
        {
            host->focused = false;
            host->hasCandidate = false;
            host->motion.SetCandidateAnchorVisible(false);
            host->Hide();
            return false;
        }
        if (activity.kind == 1)
            host->motion.Pulse(NowMs(), activity.typingIntervalMs);
        else if (activity.kind == 3)
            host->motion.Enter(NowMs());
        if (!host->visible)
            return false;
        return host->Render();
    }
    const bool changed = !host->focused || host->client != activity.client || host->epoch != activity.epoch;
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
    if (changed)
    {
        host->Hide();
        host->manualOrigin.reset();
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
        host->motion.SetCandidateAnchorVisible(true);
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
        if (!host->hasCandidate &&
            (std::max)(std::abs(dx), std::abs(dy)) >= kTeleportDistanceDip * (scale > 0 ? scale : 1.0f))
        {
            host->caret = activity.caret;
            host->keepCandidateAnchor = false;
        }
    }
    const bool showing = !host->visible || !IsWindowVisible(host->window);
    host->Place();
    if (showing && AnimationsEnabled())
        host->BeginFadeIn(NowMs(), 0.0f);
    host->visible = true;
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
            host->dragOrigin = {host->placement->x, host->placement->y};
            if (host->clickUntilMs <= NowMs())
                host->heldPose = host->motion.Sample(NowMs());
            SetCapture(hwnd);
            SetTimer(hwnd, kPointerTimer, 16, nullptr);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (host->pointerDown)
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &point);
            if (host->MovePointer(point))
                host->Render();
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
    case WM_CANCELMODE:
        host->CancelPointer();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        host->Render();
        EndPaint(hwnd, &paint);
        return 0;
    }
    case kFrameMessage:
        if (host->scheduler.ConsumeMessage(wParam, lParam))
            host->Render();
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
    const bool returnDraggedToCandidate = candidateAppeared && host->manualOrigin.has_value();
    if (returnDraggedToCandidate)
        host->manualOrigin.reset();
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
    host->motion.SetCandidateAnchorVisible(true);
    host->contextRootWindow = contextRootWindow;
    host->smoothCandidatePlacementRequest = true;
    host->Place(true, returnDraggedToCandidate ? kDragReturnMs : kCandidateFollowMs, returnDraggedToCandidate);
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
}

void SyncTopmost()
{
    if (host && host->window && ::global_hwnd && (GetWindowLongPtrW(::global_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST))
        SetWindowPos(host->window, ::global_hwnd, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Shutdown()
{
    const std::lock_guard lock(postMutex);
    acceptingActivity = false;
    candidateAnchor.reset();
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
}
} // namespace LiveSDHost
