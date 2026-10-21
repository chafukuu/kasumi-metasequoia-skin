#pragma once

#include "local_query_result.h"
#include "../core/scheme_type.h"
#include "../shuangpin/shuangpin_profile.h"

#include <filesystem>
#include <string>

namespace metasequoia::local_modes
{
LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, int limit = 10,
                             const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                             int limit = 10, const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());

// emoji 混输：只认整串精确等于某个键，不做前缀匹配。双拼下只查展开后的全拼，原始码恰好等于
// 某个简拼或英文键（xl → 😀）不是用户要打的词。
LocalQueryResult query_mixed_emoji(const std::string &code, SchemeType scheme,
                                   const std::filesystem::path &database_path, int limit = 3,
                                   const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
} // namespace metasequoia::local_modes
