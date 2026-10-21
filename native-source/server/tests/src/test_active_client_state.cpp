#include "ipc/active_client_state.h"
#include "ipc/focus_session_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(active_client_stale_deactivation_cannot_clear_new_active_client)
{
    FanyImeIpc::ActiveClientState state;

    const auto first_a = state.activate(1001);
    REQUIRE(first_a.changed);
    const auto b = state.activate(2002);
    REQUIRE(b.changed);
    const auto second_a = state.activate(1001);
    REQUIRE(second_a.changed);

    REQUIRE_EQ(state.deactivate(2002), 0u);
    REQUIRE(state.matches(1001, second_a.epoch));
}

TEST_CASE(active_client_implicit_input_activation_is_idempotent)
{
    FanyImeIpc::ActiveClientState state;

    const auto first = state.activate(1001);
    REQUIRE(first.changed);
    const auto repeated = state.activate(1001);
    REQUIRE(!repeated.changed);
    REQUIRE_EQ(repeated.epoch, first.epoch);

    state.activate(2002);
    const auto recovered = state.activate(1001);
    REQUIRE(recovered.changed);
    REQUIRE(state.matches(1001, recovered.epoch));
}

TEST_CASE(active_client_old_epoch_tasks_are_rejected)
{
    FanyImeIpc::ActiveClientState state;

    const auto old_a = state.activate(1001);
    state.activate(2002);
    const auto current_a = state.activate(1001);

    REQUIRE(!state.matches(1001, old_a.epoch));
    REQUIRE(state.matches(1001, current_a.epoch));
}

TEST_CASE(active_client_same_client_reconnect_advances_epoch)
{
    FanyImeIpc::ActiveClientState state;

    const auto old_session = state.activate(1001);
    REQUIRE(state.deactivate(1001) != 0);
    const auto new_session = state.activate(1001);

    REQUIRE(new_session.changed);
    REQUIRE(old_session.epoch != new_session.epoch);
    REQUIRE(!state.matches(1001, old_session.epoch));
    REQUIRE(state.matches(1001, new_session.epoch));
}

TEST_CASE(active_client_transport_failure_invalidates_only_exact_epoch)
{
    FanyImeIpc::ActiveClientState state;

    const auto old_session = state.activate(1001);
    REQUIRE(state.invalidate(1001, old_session.epoch) != 0);
    REQUIRE(!state.is_current(1001));

    const auto new_session = state.activate(1001);
    REQUIRE_EQ(state.invalidate(1001, old_session.epoch), 0u);
    REQUIRE(state.matches(1001, new_session.epoch));
}

TEST_CASE(active_client_new_focus_session_renews_same_client_epoch)
{
    FanyImeIpc::ActiveClientState state;

    const auto first_focus = state.activate(1001);
    const auto second_focus = state.renew(1001);

    REQUIRE(second_focus.changed);
    REQUIRE(first_focus.epoch != second_focus.epoch);
    REQUIRE(!state.matches(1001, first_focus.epoch));
    REQUIRE(state.matches(1001, second_focus.epoch));
}

TEST_CASE(active_client_terminal_deactivation_reuses_only_current_inactive_epoch)
{
    FanyImeIpc::ActiveClientState state;

    state.activate(1001);
    const uint64_t suspended_epoch = state.deactivate(1001);
    REQUIRE(suspended_epoch != 0);
    REQUIRE_EQ(state.terminal_deactivation_epoch(1001, suspended_epoch), suspended_epoch);

    // A later real deactivation observes an already-suspended route. It must
    // still carry the exact inactive epoch to the UI worker.
    REQUIRE_EQ(state.deactivate(1001), 0u);
    REQUIRE_EQ(state.terminal_deactivation_epoch(1001), suspended_epoch);
    REQUIRE(state.matches(0, suspended_epoch));

    // Once another activation wins, the old terminal event cannot hide it.
    const auto active = state.activate(2002);
    REQUIRE_EQ(state.terminal_deactivation_epoch(1001), 0u);
    REQUIRE(!state.matches(0, suspended_epoch));
    REQUIRE(state.matches(2002, active.epoch));

    // Inactive state is not sufficient ownership: after B suspends, a delayed
    // terminal event or disconnect from A must not reuse B's zero-state epoch.
    const uint64_t second_suspended_epoch = state.deactivate(2002);
    REQUIRE(second_suspended_epoch != 0);
    REQUIRE_EQ(state.terminal_deactivation_epoch(1001), 0u);
    REQUIRE_EQ(state.terminal_deactivation_epoch(2002), second_suspended_epoch);
}

TEST_CASE(active_client_foreground_terminal_deactivation_retires_foreign_route)
{
    FanyImeIpc::ActiveClientState state;

    // The foreground app suspended, then another client (e.g. TextInputHost)
    // took routing and never gave it back. The app's terminal event is not
    // routed to it, so ownership alone rejects it.
    state.activate(1001);
    state.deactivate(1001);
    const auto foreign = state.activate(3003);
    REQUIRE_EQ(state.deactivate(1001), 0u);
    REQUIRE_EQ(state.terminal_deactivation_epoch(1001), 0u);

    // Once the caller proved 1001 owns the foreground thread, the stale route
    // is retired and the UI worker's inactive check accepts the new epoch.
    const uint64_t retired_epoch = state.retire_for_terminal_deactivation(1001);
    REQUIRE(retired_epoch != 0);
    REQUIRE(retired_epoch != foreign.epoch);
    REQUIRE(!state.matches(3003, foreign.epoch));
    REQUIRE(state.matches(0, retired_epoch));
    REQUIRE_EQ(state.snapshot().client_id, 0u);

    // The duplicate event from Deactivate() reuses the same epoch instead of
    // being dropped or advancing it again.
    REQUIRE_EQ(state.terminal_deactivation_epoch(1001), retired_epoch);
    REQUIRE_EQ(state.terminal_deactivation_epoch(3003), 0u);

    // A later activation supersedes the retirement.
    const auto next = state.activate(3003);
    REQUIRE(!state.matches(0, retired_epoch));
    REQUIRE(state.matches(3003, next.epoch));
    REQUIRE_EQ(state.retire_for_terminal_deactivation(0), 0u);
}

TEST_CASE(focus_session_ready_requires_nonzero_client_epoch_and_token)
{
    REQUIRE(FanyImeIpc::CanSendFocusSessionReady(1001, 22, 77));
    REQUIRE(!FanyImeIpc::CanSendFocusSessionReady(0, 22, 77));
    REQUIRE(!FanyImeIpc::CanSendFocusSessionReady(1001, 0, 77));
    REQUIRE(!FanyImeIpc::CanSendFocusSessionReady(1001, 22, 0));
}
