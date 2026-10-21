#pragma once

#include "ai_assistant.h"

#include <nlohmann/json.hpp>

namespace AiAssistant::detail
{
nlohmann::json BuildRequestBody(const Request &request);
// DeepSeek thinking needs far more time than a plain completion before the answer arrives.
long RequestTimeoutMs(const AiAssistantConfig &config);
} // namespace AiAssistant::detail
