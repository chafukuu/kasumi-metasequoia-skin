#include "ai/ai_assistant_cache.h"
#include "tests/includes/test_framework.h"

#include <string>

TEST_CASE(ai_suggestion_cache_retains_entries_below_the_limit)
{
    AiAssistant::detail::SuggestionCache cache;
    for (std::size_t index = 0; index < AiAssistant::detail::SuggestionCache::kMaxEntries; ++index)
        cache.Store("key-" + std::to_string(index), "candidate-" + std::to_string(index));

    REQUIRE(cache.Size() == AiAssistant::detail::SuggestionCache::kMaxEntries);
    REQUIRE(cache.Find("key-0") == "candidate-0");
    REQUIRE(cache.Find("key-127") == "candidate-127");
}

TEST_CASE(ai_suggestion_cache_clears_old_entries_at_the_limit)
{
    AiAssistant::detail::SuggestionCache cache;
    for (std::size_t index = 0; index < AiAssistant::detail::SuggestionCache::kMaxEntries; ++index)
        cache.Store("key-" + std::to_string(index), "candidate-" + std::to_string(index));

    cache.Store("new-key", "new-candidate");

    REQUIRE(cache.Size() == 1);
    REQUIRE(!cache.Find("key-0").has_value());
    REQUIRE(cache.Find("new-key") == "new-candidate");
}

TEST_CASE(ai_suggestion_cache_updates_an_existing_entry_without_clearing)
{
    AiAssistant::detail::SuggestionCache cache;
    for (std::size_t index = 0; index < AiAssistant::detail::SuggestionCache::kMaxEntries; ++index)
        cache.Store("key-" + std::to_string(index), "candidate-" + std::to_string(index));

    cache.Store("key-0", "updated-candidate");

    REQUIRE(cache.Size() == AiAssistant::detail::SuggestionCache::kMaxEntries);
    REQUIRE(cache.Find("key-0") == "updated-candidate");
    REQUIRE(cache.Find("key-127") == "candidate-127");
}
