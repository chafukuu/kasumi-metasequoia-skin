#include "ai_assistant_cache_key.h"

#include <nlohmann/json.hpp>

namespace AiAssistant::detail
{
std::string BuildSuggestionCacheKey(const Request &request)
{
    return nlohmann::json{{"provider", request.config.provider}, {"endpoint", request.config.endpoint},
                          {"model", request.config.model},       {"thinking", request.config.thinking},
                          {"prompt", request.config.prompt},     {"pinyin_segments", request.pinyin_segments},
                          {"context", request.context},          {"candidate_limit", request.config.candidate_limit},
                          {"candidates", request.candidates}}
        // context 与 prompt 是自由文本；键在无 try 的工作线程里构建，非法 UTF-8 不能抛出
        .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
} // namespace AiAssistant::detail
