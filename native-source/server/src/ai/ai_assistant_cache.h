#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

namespace AiAssistant::detail
{
class SuggestionCache
{
  public:
    static constexpr std::size_t kMaxEntries = 128;

    std::optional<std::string> Find(const std::string &key) const;
    void Store(std::string key, std::string candidate);
    void Clear();
    std::size_t Size() const;

  private:
    std::unordered_map<std::string, std::string> entries_;
};
} // namespace AiAssistant::detail
