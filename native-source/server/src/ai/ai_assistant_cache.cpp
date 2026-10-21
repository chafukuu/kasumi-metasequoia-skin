#include "ai_assistant_cache.h"

#include <utility>

namespace AiAssistant::detail
{
std::optional<std::string> SuggestionCache::Find(const std::string &key) const
{
    const auto entry = entries_.find(key);
    if (entry == entries_.end())
        return std::nullopt;
    return entry->second;
}

void SuggestionCache::Store(std::string key, std::string candidate)
{
    const auto entry = entries_.find(key);
    if (entry != entries_.end())
    {
        entry->second = std::move(candidate);
        return;
    }
    if (entries_.size() >= kMaxEntries)
        entries_.clear();
    entries_.emplace(std::move(key), std::move(candidate));
}

void SuggestionCache::Clear()
{
    entries_.clear();
}

std::size_t SuggestionCache::Size() const
{
    return entries_.size();
}
} // namespace AiAssistant::detail
