#include "tests/includes/test_framework.h"

#include "msimeui/Animation.h"
#include "msimeui/DeviceResources.h"

#include <cmath>

TEST_CASE(easing_curves_start_and_end_at_their_endpoints)
{
    for (const msimeui::Easing &easing :
         {msimeui::Easing::Linear(), msimeui::Easing::Ease(), msimeui::Easing::EaseOut(), msimeui::Easing::EaseInOut(),
          msimeui::Easing{0.22f, 1.0f, 0.36f, 1.0f}})
    {
        REQUIRE_NEAR(easing(0.0f), 0.0f);
        REQUIRE_NEAR(easing(1.0f), 1.0f);
        // Progress outside [0, 1] is clamped rather than extrapolated.
        REQUIRE_NEAR(easing(-0.5f), 0.0f);
        REQUIRE_NEAR(easing(1.5f), 1.0f);
    }
}

TEST_CASE(easing_matches_css_reference_values)
{
    // Values of the CSS timing functions sampled by a browser.
    REQUIRE_NEAR(msimeui::Easing::Linear()(0.3f), 0.3f);
    REQUIRE(std::fabs(msimeui::Easing::Ease()(0.5f) - 0.8024f) < 0.002f);
    REQUIRE(std::fabs(msimeui::Easing::EaseInOut()(0.5f) - 0.5f) < 0.002f);
    // The sidebar indicator's strong ease-out is most of the way there at a quarter of the time.
    const msimeui::Easing indicator = {0.22f, 1.0f, 0.36f, 1.0f};
    REQUIRE(indicator(0.25f) > 0.6f);
}

TEST_CASE(easing_is_monotonic_for_monotonic_curves)
{
    const msimeui::Easing easing = msimeui::Easing::Ease();
    float previous = 0.0f;
    for (int i = 1; i <= 100; ++i)
    {
        const float value = easing(static_cast<float>(i) / 100.0f);
        REQUIRE(value + 1e-4f >= previous);
        previous = value;
    }
}

TEST_CASE(tween_samples_from_start_to_target)
{
    msimeui::Tween tween(10.0f);
    REQUIRE_NEAR(tween.Value(0.0), 10.0f);
    tween.Start(10.0f, 30.0f, 100.0, msimeui::Easing::Linear(), 1000.0);
    REQUIRE_NEAR(tween.Value(1000.0), 10.0f);
    REQUIRE_NEAR(tween.Value(1050.0), 20.0f);
    REQUIRE_NEAR(tween.Value(1100.0), 30.0f);
    REQUIRE_NEAR(tween.Value(5000.0), 30.0f);
    REQUIRE(tween.IsRunning(1050.0));
    REQUIRE(!tween.IsRunning(1100.0));
}

TEST_CASE(tween_retargets_from_the_value_on_screen)
{
    msimeui::Tween tween(0.0f);
    tween.Start(0.0f, 100.0f, 100.0, msimeui::Easing::Linear(), 0.0);
    // Interrupted halfway: the new animation starts at 50, not at 0 or 100.
    tween.AnimateTo(0.0f, 100.0, msimeui::Easing::Linear(), 50.0);
    REQUIRE_NEAR(tween.Value(50.0), 50.0f);
    REQUIRE_NEAR(tween.Value(100.0), 25.0f);
    REQUIRE_NEAR(tween.Target(), 0.0f);
    // Asking for the target it is already heading to does not restart it.
    tween.AnimateTo(0.0f, 100.0, msimeui::Easing::Linear(), 120.0);
    REQUIRE_NEAR(tween.Value(150.0), 0.0f);
}

TEST_CASE(tween_with_zero_duration_snaps)
{
    msimeui::Tween tween(1.0f);
    tween.AnimateTo(5.0f, 0.0, msimeui::Easing::Ease(), 10.0);
    REQUIRE_NEAR(tween.Value(10.0), 5.0f);
    REQUIRE(!tween.IsRunning(10.0));
}

TEST_CASE(lerp_color_blends_every_channel)
{
    const D2D1_COLOR_F from = D2D1::ColorF(0.0f, 0.2f, 0.4f, 1.0f);
    const D2D1_COLOR_F to = D2D1::ColorF(1.0f, 0.4f, 0.0f, 0.0f);
    const D2D1_COLOR_F mid = msimeui::LerpColor(from, to, 0.5f);
    REQUIRE_NEAR(mid.r, 0.5f);
    REQUIRE_NEAR(mid.g, 0.3f);
    REQUIRE_NEAR(mid.b, 0.2f);
    REQUIRE_NEAR(mid.a, 0.5f);
}

TEST_CASE(clip_stack_culls_rects_outside_the_visible_area)
{
    // No render target is needed: the stack also feeds culling, which containers
    // query before rendering a child.
    msimeui::DeviceResources resources;
    REQUIRE(resources.IsVisible({-1000.0f, -1000.0f, 1.0f, 1.0f}));
    resources.PushClip({0.0f, 0.0f, 100.0f, 100.0f});
    REQUIRE(resources.IsVisible({50.0f, 50.0f, 10.0f, 10.0f}));
    REQUIRE(resources.IsVisible({90.0f, 90.0f, 50.0f, 50.0f}));
    REQUIRE(!resources.IsVisible({100.0f, 0.0f, 10.0f, 10.0f}));
    REQUIRE(!resources.IsVisible({0.0f, -20.0f, 10.0f, 10.0f}));
    // Nested clips intersect.
    resources.PushClip({50.0f, 50.0f, 100.0f, 100.0f});
    REQUIRE(!resources.IsVisible({10.0f, 10.0f, 20.0f, 20.0f}));
    REQUIRE(resources.IsVisible({60.0f, 60.0f, 10.0f, 10.0f}));
    resources.PopClip();
    REQUIRE(resources.IsVisible({10.0f, 10.0f, 20.0f, 20.0f}));
    resources.PopClip();
    // A new frame starts with nothing clipped even if a pop went missing.
    resources.PushClip({0.0f, 0.0f, 1.0f, 1.0f});
    resources.BeginFrame();
    REQUIRE(resources.IsVisible({500.0f, 500.0f, 1.0f, 1.0f}));
}
