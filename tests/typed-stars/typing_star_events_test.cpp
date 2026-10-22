#include "window/candidate_typing_star_events.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Check(bool result, const char *message) {
  if (!result)
    throw std::runtime_error(message);
}
} // namespace

int main() {
  CandidateTypingStars::EventBridge events;
  Check(events.ConsumeSnapshot().epoch == 1,
        "initial generation starts at one");
  Check(events.ConsumeBursts() == 0, "initial state is empty");
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 0, "first character waits for its pair");
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 1, "second character emits one pair");
  Check(events.ConsumeBursts() == 0, "consuming cannot emit twice");

  for (int index = 0; index < 4; ++index)
    events.NotifyInserted();
  Check(events.ConsumeBursts() == 2, "four characters accumulate two pairs");

  events.NotifyInserted();
  events.Reset();
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 0, "reset discards an incomplete pair");
  events.NotifyInserted();
  events.Reset();
  Check(events.ConsumeBursts() == 0, "reset discards queued bursts");

  for (int index = 0; index < 5; ++index)
    events.NotifyInserted();
  Check(events.ConsumeBursts() == 2, "batch consumption preserves both pairs");
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 1,
        "consumption preserves pending odd character");

  // Deleting or moving the caret interrupts consecutive typing without
  // ending the composition or discarding an already completed pair. The UI
  // uses epoch changes as its authoritative signal to clear live particles.
  const auto continuity_snapshot = events.ConsumeSnapshot();
  events.NotifyInserted();
  events.BreakSequence();
  events.NotifyInserted();
  const auto after_first_resumed = events.ConsumeSnapshot();
  Check(after_first_resumed.bursts == 0,
        "a character after deletion cannot pair with the preceding character");
  Check(after_first_resumed.epoch == continuity_snapshot.epoch,
        "deletion preserves the live-particle generation");
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 1,
        "two new characters after deletion create a fresh pair");

  events.NotifyInserted();
  events.NotifyInserted();
  events.NotifyInserted();
  events.BreakSequence();
  events.BreakSequence();
  const auto after_queued_break = events.ConsumeSnapshot();
  Check(after_queued_break.bursts == 1,
        "deletion preserves a completed burst awaiting delayed UI consumption");
  Check(after_queued_break.epoch == continuity_snapshot.epoch,
        "repeated deletion and navigation cannot clear live particles");
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 0,
        "preserving a queued burst still clears its odd character remainder");
  events.NotifyInserted();
  events.NotifyInserted();
  events.Reset();
  const auto after_composition_end = events.ConsumeSnapshot();
  Check(after_composition_end.bursts == 0,
        "a true composition end discards queued bursts");
  Check(after_composition_end.epoch == continuity_snapshot.epoch + 1,
        "a true composition end still clears the live-particle generation");
  events.NotifyInserted();
  Check(events.ConsumeBursts() == 0,
        "a true composition end also clears the odd character remainder");
  events.Reset();

  // Old Hide reaches the UI after the worker has reset and accepted the first
  // character of a new composition. Keep the new composition's pending input.
  const auto old_snapshot = events.ConsumeSnapshot();
  events.Reset();
  events.NotifyInserted();
  Check(!events.ResetIfEpoch(old_snapshot.epoch),
        "stale Hide cannot reset a newer composition");
  events.NotifyInserted();
  const auto new_snapshot = events.ConsumeSnapshot();
  Check(new_snapshot.epoch == old_snapshot.epoch + 1,
        "authoritative Reset advances generation");
  Check(new_snapshot.bursts == 1,
        "stale Hide preserves the new composition's first character");

  events.NotifyInserted();
  Check(events.ResetIfEpoch(new_snapshot.epoch),
        "matching generation permits UI Reset");
  events.NotifyInserted();
  const auto after_ui_reset = events.ConsumeSnapshot();
  Check(after_ui_reset.epoch == new_snapshot.epoch + 1,
        "conditional UI Reset advances generation");
  Check(after_ui_reset.bursts == 0,
        "matching UI Reset clears pending character");
  Check(!events.ResetIfEpoch(new_snapshot.epoch),
        "repeated old Hide cannot reset twice");
  events.Reset();

  // Multiple producers and a simultaneous UI consumer share one short lock.
  // This uses accepted insertion events only: no key or input text is stored.
  std::atomic<bool> producers_done{false};
  std::uint64_t consumed = 0;
  std::thread consumer([&]() {
    while (!producers_done.load(std::memory_order_acquire)) {
      consumed += events.ConsumeBursts();
      std::this_thread::yield();
    }
    consumed += events.ConsumeBursts();
  });
  constexpr std::uint64_t producer_count = 4;
  constexpr std::uint64_t insertions_per_producer = 50001;
  std::vector<std::thread> producers;
  for (std::uint64_t producer = 0; producer < producer_count; ++producer)
    producers.emplace_back([&]() {
      for (std::uint64_t index = 0; index < insertions_per_producer; ++index)
        events.NotifyInserted();
    });
  for (auto &producer : producers)
    producer.join();
  producers_done.store(true, std::memory_order_release);
  consumer.join();
  Check(consumed == producer_count * insertions_per_producer / 2,
        "concurrent consumption loses no pair");
  Check(events.ConsumeBursts() == 0, "consumer drained all concurrent events");

  // Production wrappers use the same process-wide instance.
  CandidateTypingStars::Reset();
  CandidateTypingStars::NotifyInserted();
  CandidateTypingStars::NotifyInserted();
  Check(CandidateTypingStars::ConsumeBursts() == 1,
        "production singleton forwards events");
  const auto singleton_before_break = CandidateTypingStars::ConsumeSnapshot();
  CandidateTypingStars::NotifyInserted();
  CandidateTypingStars::NotifyInserted();
  CandidateTypingStars::NotifyInserted();
  CandidateTypingStars::BreakSequence();
  const auto singleton_after_break = CandidateTypingStars::ConsumeSnapshot();
  Check(singleton_after_break.bursts == 1 &&
            singleton_after_break.epoch == singleton_before_break.epoch,
        "production singleton preserves queued bursts and epoch on editing");
  CandidateTypingStars::NotifyInserted();
  Check(CandidateTypingStars::ConsumeBursts() == 0,
        "production singleton breaks its pending insertion pair");
  CandidateTypingStars::Reset();
  std::cout << "PASS: 2/4 insertions, reset, batch, odd remainder, 200004 "
               "concurrent insertions\n";
  const auto singleton_snapshot = CandidateTypingStars::ConsumeSnapshot();
  Check(CandidateTypingStars::ResetIfEpoch(singleton_snapshot.epoch),
        "singleton conditionally resets its epoch");
  std::cout
      << "PASS: stale Hide race, snapshot epoch, matching Reset, repeated "
         "stale Reset\n";
  std::cout
      << "PASS: deletion/navigation break pairing, preserve queued bursts "
         "and live-particle epoch; composition end resets all state\n";
}
