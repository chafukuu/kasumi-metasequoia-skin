#include "tests/includes/test_framework.h"
#include "window/floating_toolbar_visibility_policy.h"

TEST_CASE(floating_toolbar_visibility_requires_active_ime_preference_and_nonfullscreen)
{
    REQUIRE(FanyImeUi::ShouldShowFloatingToolbar(true, false, true, false));
    REQUIRE(!FanyImeUi::ShouldShowFloatingToolbar(false, false, true, false));
    REQUIRE(!FanyImeUi::ShouldShowFloatingToolbar(true, true, true, false));
    REQUIRE(!FanyImeUi::ShouldShowFloatingToolbar(true, false, false, false));
    REQUIRE(!FanyImeUi::ShouldShowFloatingToolbar(false, true, false, false));
}

TEST_CASE(floating_toolbar_stays_hidden_once_auto_hidden)
{
    REQUIRE(!FanyImeUi::ShouldShowFloatingToolbar(true, false, true, true));
}
