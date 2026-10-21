// 自定义双拼：扫描 <数据目录>/shuangpin/custom/*.toml，解析成引擎的 ShuangpinProfile 并登记。
// 文件格式见 docs/custom-shuangpin.example.toml。
#include "config/ime_config_internal.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include "engine/core/data_path.h"
#include "engine/shuangpin/shuangpin_profile.h"

using namespace ime_config_detail;

namespace
{
constexpr char kCustomShuangpinSchemaPrefix[] = "custom/";
constexpr char kCustomShuangpinExtension[] = ".toml";

std::filesystem::path CustomShuangpinDirectoryPath()
{
    return metasequoia::data_directory() / "shuangpin" / "custom";
}

// Empty when the schema is not a well-formed custom schema. The stem must stay a single file name
// inside the custom directory.
std::string CustomSchemaStem(const std::string &schema)
{
    const std::string prefix = kCustomShuangpinSchemaPrefix;
    if (schema.size() <= prefix.size() || schema.compare(0, prefix.size(), prefix) != 0)
        return {};
    std::string stem = schema.substr(prefix.size());
    if (stem.front() == '.' || stem.find_first_of("/\\:*?\"<>|") != std::string::npos)
        return {};
    return stem;
}

std::filesystem::path CustomSchemaFile(const std::string &stem)
{
    return CustomShuangpinDirectoryPath() / metasequoia::path_from_utf8((stem + kCustomShuangpinExtension).c_str());
}

std::string ReadString(const toml::table &root, const char *key, std::vector<std::string> &errors)
{
    const toml::node *node = root.get(key);
    if (!node)
        return {};
    if (const auto value = node->value<std::string>())
        return *value;
    errors.push_back(std::string(key) + " 必须是用引号括起来的字符串");
    return {};
}

void ReadStringTable(const toml::table &root, const char *section, std::unordered_map<std::string, std::string> &output,
                     std::vector<std::string> &errors)
{
    const toml::node *node = root.get(section);
    if (!node)
        return;
    const toml::table *table = node->as_table();
    if (!table)
    {
        errors.push_back(std::string("[") + section + "] 必须是一个表");
        return;
    }
    for (const auto &[key, value] : *table)
    {
        const std::string name(key.str());
        if (const auto text = value.value<std::string>())
            output[name] = *text;
        else
            errors.push_back(std::string("[") + section + "] 里 " + name + " 的值必须是用引号括起来的字符串");
    }
}

ParsedCustomShuangpin ReadCustomSchemaFile(const std::string &schema)
{
    const std::string stem = CustomSchemaStem(schema);
    std::error_code error;
    if (stem.empty() || !std::filesystem::is_regular_file(CustomSchemaFile(stem), error))
    {
        ParsedCustomShuangpin parsed;
        parsed.errors.push_back("找不到方案文件");
        return parsed;
    }
    return ParseCustomShuangpinSchema(ReadFileText(CustomSchemaFile(stem)), schema);
}
} // namespace

ParsedCustomShuangpin ParseCustomShuangpinSchema(const std::string &toml_text, const std::string &schema)
{
    ParsedCustomShuangpin parsed;
    toml::table root;
    try
    {
        root = toml::parse(toml_text);
    }
    catch (const toml::parse_error &error)
    {
        parsed.errors.push_back("第 " + std::to_string(error.source().begin.line) + " 行不是合法的 TOML：" +
                                std::string(error.description()));
        return parsed;
    }

    CustomShuangpinLayout layout;
    layout.name = schema;
    parsed.name = ReadString(root, "name", parsed.errors);
    parsed.name_en = ReadString(root, "name_en", parsed.errors);
    const std::string rule = ReadString(root, "zero_initial_rule", parsed.errors);
    if (rule.empty())
    {
        parsed.errors.push_back("缺少 zero_initial_rule，可选 \"o\"、\"a\"、\"first_letter\"、\"full_pinyin\"");
    }
    else if (const auto parsed_rule = ParseShuangpinZeroInitialRule(rule))
    {
        layout.zero_initial_rule = *parsed_rule;
    }
    else
    {
        parsed.errors.push_back("zero_initial_rule 只能是 \"o\"、\"a\"、\"first_letter\"、\"full_pinyin\"，现在是 \"" +
                                rule + "\"");
    }
    ReadStringTable(root, "initials", layout.initials, parsed.errors);
    ReadStringTable(root, "finals", layout.finals, parsed.errors);
    ReadStringTable(root, "zero_initials", layout.zero_initial_overrides, parsed.errors);
    if (!parsed.errors.empty())
        return parsed;

    parsed.errors = BuildCustomShuangpinProfile(layout, parsed.profile);
    return parsed;
}

std::vector<CustomShuangpinSchemaInfo> GetCustomShuangpinSchemas()
{
    std::vector<CustomShuangpinSchemaInfo> result;
    std::error_code error;
    for (std::filesystem::directory_iterator it(CustomShuangpinDirectoryPath(), error), end; !error && it != end;
         it.increment(error))
    {
        std::string extension = metasequoia::path_to_utf8(it->path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!it->is_regular_file(error) || extension != kCustomShuangpinExtension)
            continue;
        const std::string stem = metasequoia::path_to_utf8(it->path().stem());
        const std::string schema = kCustomShuangpinSchemaPrefix + stem;
        if (CustomSchemaStem(schema).empty())
            continue;
        const ParsedCustomShuangpin parsed = ParseCustomShuangpinSchema(ReadFileText(it->path()), schema);
        const std::string &name = parsed.name.empty() ? parsed.name_en : parsed.name;
        const std::string &name_en = parsed.name_en.empty() ? parsed.name : parsed.name_en;
        std::string message;
        for (const auto &line : parsed.errors)
            message += (message.empty() ? "" : "\n") + line;
        result.push_back({schema, name.empty() ? stem : name, name_en.empty() ? stem : name_en, message});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.schema < b.schema; });
    return result;
}

std::string GetCustomShuangpinDirectory()
{
    return metasequoia::path_to_utf8(CustomShuangpinDirectoryPath());
}

namespace ime_config_detail
{
bool LoadShuangpinSchema(const std::string &schema)
{
    // The resolver falls back to Xiaohe for unknown names, so a name round-trip means "built in".
    if (GetShuangpinProfile(schema).name == schema && CustomSchemaStem(schema).empty())
        return true;
    ParsedCustomShuangpin parsed = ReadCustomSchemaFile(schema);
    if (!parsed.errors.empty())
        return false;
    parsed.profile.name = schema;
    RegisterShuangpinProfile(std::move(parsed.profile));
    return true;
}
} // namespace ime_config_detail
