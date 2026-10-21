#pragma once

#include <string>
#include <vector>

namespace NiuTransTranslation
{
struct Config
{
    std::string app_id;
    std::string apikey;
};

// Both credentials must be present and non-placeholder before requests are sent.
bool IsUsableConfig(const Config &config);

// MD5 permission string per the NiuTrans v2 spec: md5 of the request parameters
// (plus apikey) sorted by key and joined as "key=value&...". Exposed for tests.
std::string BuildAuthString(const Config &config, const std::string &from, const std::string &to,
                            const std::string &timestamp, const std::string &src_text);

// Extracts tgtText from a /v2/text/translate response. Empty on error responses
// (errorCode/errorMsg) or malformed JSON. Exposed for tests.
std::string ParseTranslationResponse(const std::string &response);

// Calls the NiuTrans /v2/text/translate endpoint once per source string. Empty
// entries mean that item failed. When the batch time budget runs out the result
// is shorter than texts: the missing tail was never sent.
std::vector<std::string> TextTranslateBatch(const Config &config, const std::vector<std::string> &texts,
                                            const std::string &source, const std::string &target);
} // namespace NiuTransTranslation
