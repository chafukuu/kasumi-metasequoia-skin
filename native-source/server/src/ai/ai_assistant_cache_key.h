#pragma once

#include "ai_assistant.h"

#include <string>

namespace AiAssistant::detail
{
std::string BuildSuggestionCacheKey(const Request &request);
} // namespace AiAssistant::detail
