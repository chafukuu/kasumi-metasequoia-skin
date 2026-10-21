#pragma once

#include <string>

namespace AiAssistant
{
// One user-managed provider entry, in the spirit of HanaAgent's provider registry: an arbitrary
// OpenAI-compatible service with its own name, endpoint and token. Kept dependency-free so both
// the config layer and the store can include it.
struct AiProvider
{
    std::string id;       // stable slug, also the value of ai_assistant.provider
    std::string name;     // shown in the settings UI
    std::string endpoint; // chat completions URL
    std::string token;
    std::string model;
};
} // namespace AiAssistant