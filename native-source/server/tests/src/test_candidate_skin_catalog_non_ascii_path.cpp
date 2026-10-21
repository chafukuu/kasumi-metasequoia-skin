#include "tests/includes/test_framework.h"

#include "skin/candidate_skin_catalog.h"
#include "window/candidate_band_artwork_geometry.h"
#include "window/candidate_item_separators.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
constexpr const char *kManifest = R"(schema_version = 1
id = "demo"
name = "Demo"
version = "1.0.0"
author = "tester"
description = "fixture"
base = "fluent"

[supports]
layouts = ["horizontal", "vertical"]
themes = ["dark", "light"]

[candidate_window]
)";

// Writes a minimal but complete external skin under <root>/skins/demo.
void WriteSkin(const std::filesystem::path &skins_root, std::string_view window_fields = "min_width_dip = 320.0\n")
{
    std::error_code ec;
    std::filesystem::create_directories(skins_root / L"demo", ec);
    REQUIRE(!ec);
    std::ofstream manifest(skins_root / L"demo" / L"skin.toml", std::ios::binary | std::ios::trunc);
    REQUIRE(manifest.is_open());
    manifest << kManifest << window_fields;
}

void WriteBandArtworkFixtures(const std::filesystem::path &skinsRoot)
{
    std::error_code ec;
    std::filesystem::create_directories(skinsRoot / L"demo" / L"assets", ec);
    REQUIRE(!ec);
    // Manifest tests check resource paths and existence, not bitmap decoding.
    for (const wchar_t *name : {L"background.png", L"foreground.PNG"})
    {
        std::ofstream image(skinsRoot / L"demo" / L"assets" / name, std::ios::binary | std::ios::trunc);
        REQUIRE(image.is_open());
        image << "\x89PNG\r\n\x1a\n";
    }
}

void RemoveTestDirectory(const std::filesystem::path &directory, const std::wstring &expected_filename)
{
    namespace fs = std::filesystem;
    // Windows may keep a trailing separator in TEMP. Normalize its spelling
    // before the exact parent/name guard; MinGW's equivalent() rejects directories.
    auto temporary_root = fs::temp_directory_path().lexically_normal();
    if (temporary_root.filename().empty())
        temporary_root = temporary_root.parent_path();
    REQUIRE(directory.parent_path().lexically_normal() == temporary_root);
    REQUIRE(directory.filename() == expected_filename);
    std::error_code ec;
    fs::remove_all(directory, ec);
    REQUIRE(!ec);
}
} // namespace

// Skins live under the user profile, so the catalog must load them whatever the profile path looks
// like. Reading the manifest with toml::parse_file(manifest.string()) routed the path through the
// ANSI code page, which corrupts a non-ASCII (e.g. Chinese) path and throws on a code page that
// cannot represent the characters -- and that throw escapes the toml handlers around it.
TEST_CASE(candidate_skin_catalog_loads_under_non_ascii_path)
{
    namespace fs = std::filesystem;
    // The root stays ASCII so the ascii_root control below really is ASCII end to end: only the
    // non_ascii_root leg carries Chinese characters, which is what the assertions must isolate.
    const fs::path unique_root =
        fs::temp_directory_path() / (L"msime-skin-test-" + std::to_wstring(GetCurrentProcessId()));

    std::error_code ec;
    fs::remove_all(unique_root, ec);

    const fs::path ascii_root = unique_root / L"ascii" / L"skins";
    const fs::path non_ascii_root = unique_root / L"用户目录" / L"skins";
    WriteSkin(ascii_root);
    WriteSkin(non_ascii_root);

    std::string ascii_error;
    const auto ascii_package = CandidateSkinCatalog::Load(ascii_root, "demo", &ascii_error);
    REQUIRE(ascii_package.has_value());
    REQUIRE(!ascii_package->maxWidthDip.has_value());
    REQUIRE(!ascii_package->reserveMaxWidth);

    // The Chinese path must behave exactly like the ASCII one, not throw and not report an error.
    std::string non_ascii_error;
    const auto non_ascii_package = CandidateSkinCatalog::Load(non_ascii_root, "demo", &non_ascii_error);
    REQUIRE(non_ascii_error.empty());
    REQUIRE(non_ascii_package.has_value());
    REQUIRE_EQ(non_ascii_package->id, ascii_package->id);
    REQUIRE_EQ(non_ascii_package->name, ascii_package->name);
    REQUIRE_EQ(non_ascii_package->base, ascii_package->base);

    // Scan() walks the same manifests, so it must survive the Chinese root as well.
    const auto scan = CandidateSkinCatalog::Scan(non_ascii_root);
    REQUIRE_EQ(scan.packages.size(), std::size_t{1});
    REQUIRE_EQ(scan.packages.front().id, std::string("demo"));

    fs::remove_all(unique_root, ec);
}

TEST_CASE(candidate_skin_catalog_validates_horizontal_max_width)
{
    namespace fs = std::filesystem;
    const fs::path unique_root =
        fs::temp_directory_path() / (L"msime-skin-width-test-" + std::to_wstring(GetCurrentProcessId()));
    const fs::path skins_root = unique_root / L"skins";

    struct ValidMaximum
    {
        const char *text;
        double expected;
    };
    const ValidMaximum valid_values[] = {{"1", 1.0}, {"1032", 1032.0}, {"1032.0", 1032.0}, {"4096.0", 4096.0}};
    for (const auto &value : valid_values)
    {
        WriteSkin(skins_root, std::string("max_width_dip = ") + value.text + "\n");
        std::string error;
        const auto package = CandidateSkinCatalog::Load(skins_root, "demo", &error);
        REQUIRE(error.empty());
        REQUIRE(package.has_value());
        REQUIRE(package->maxWidthDip.has_value());
        REQUIRE_EQ(*package->maxWidthDip, value.expected);
    }

    const char *invalid_values[] = {"0",    "-1",  "0.5",      "4096.01", "4097", "inf",
                                    "-inf", "nan", "\"1032\"", "true",    "[]",   "{}"};
    for (const char *value : invalid_values)
    {
        WriteSkin(skins_root, std::string("max_width_dip = ") + value + "\n");
        std::string error;
        REQUIRE(!CandidateSkinCatalog::Load(skins_root, "demo", &error).has_value());
        REQUIRE(error.find("max_width_dip") != std::string::npos);
    }

    // Equality is valid; an explicit minimum above the maximum rejects the manifest.
    WriteSkin(skins_root, "min_width_dip = 320.0\nmax_width_dip = 320.0\n");
    std::string equal_error;
    REQUIRE(CandidateSkinCatalog::Load(skins_root, "demo", &equal_error).has_value());
    REQUIRE(equal_error.empty());

    WriteSkin(skins_root, "min_width_dip = 320.0\nmax_width_dip = 319.0\n");
    std::string conflict_error;
    REQUIRE(!CandidateSkinCatalog::Load(skins_root, "demo", &conflict_error).has_value());
    REQUIRE(conflict_error.find("min_width_dip") != std::string::npos);
    REQUIRE(conflict_error.find("max_width_dip") != std::string::npos);

    RemoveTestDirectory(unique_root, L"msime-skin-width-test-" + std::to_wstring(GetCurrentProcessId()));
}

TEST_CASE(candidate_skin_catalog_validates_reserved_horizontal_max_width)
{
    namespace fs = std::filesystem;
    const fs::path unique_root =
        fs::temp_directory_path() / (L"msime-skin-reserve-width-test-" + std::to_wstring(GetCurrentProcessId()));
    const fs::path skins_root = unique_root / L"skins";

    struct ValidReservation
    {
        const char *fields;
        bool expected;
    };
    const ValidReservation valid_values[] = {{"", false},
                                             {"reserve_max_width = false\n", false},
                                             {"max_width_dip = 1032.0\n", false},
                                             {"max_width_dip = 1032.0\nreserve_max_width = false\n", false},
                                             {"max_width_dip = 1032.0\nreserve_max_width = true\n", true}};
    for (const auto &value : valid_values)
    {
        WriteSkin(skins_root, value.fields);
        std::string error;
        const auto package = CandidateSkinCatalog::Load(skins_root, "demo", &error);
        REQUIRE(error.empty());
        REQUIRE(package.has_value());
        REQUIRE_EQ(package->reserveMaxWidth, value.expected);
    }

    const char *invalid_values[] = {"\"true\"", "\"false\"", "\"\"", "0", "1", "0.0", "1.0", "[]", "{}"};
    for (const char *value : invalid_values)
    {
        WriteSkin(skins_root, std::string("max_width_dip = 1032.0\nreserve_max_width = ") + value + "\n");
        std::string error;
        REQUIRE(!CandidateSkinCatalog::Load(skins_root, "demo", &error).has_value());
        REQUIRE(error.find("reserve_max_width") != std::string::npos);
    }

    WriteSkin(skins_root, "reserve_max_width = true\n");
    std::string missing_maximum_error;
    REQUIRE(!CandidateSkinCatalog::Load(skins_root, "demo", &missing_maximum_error).has_value());
    REQUIRE(missing_maximum_error.find("reserve_max_width") != std::string::npos);
    REQUIRE(missing_maximum_error.find("max_width_dip") != std::string::npos);

    RemoveTestDirectory(unique_root, L"msime-skin-reserve-width-test-" + std::to_wstring(GetCurrentProcessId()));
}

TEST_CASE(candidate_skin_catalog_loads_optional_item_separators)
{
    namespace fs = std::filesystem;
    const auto directoryName = L"msime-skin-separators-test-" + std::to_wstring(GetCurrentProcessId());
    const fs::path uniqueRoot = fs::temp_directory_path() / directoryName;
    const fs::path skinsRoot = uniqueRoot / L"skins";
    WriteSkin(skinsRoot);
    auto package = CandidateSkinCatalog::Load(skinsRoot, "demo");
    REQUIRE(package.has_value());
    REQUIRE(!package->itemSeparators.has_value());
    REQUIRE(!package->horizontalGeometry.singleLine.has_value());

    WriteSkin(skinsRoot, R"([candidate_window.item_separators]
color = "#ffad00"
top_dip = 6.75
bottom_dip = 38.25
line_width_dip = 0.4875
line_gap_dip = 1.05
)");
    std::string error;
    package = CandidateSkinCatalog::Load(skinsRoot, "demo", &error);
    REQUIRE(error.empty());
    REQUIRE(package.has_value());
    REQUIRE(package->itemSeparators.has_value());
    REQUIRE_EQ(package->itemSeparators->color, "#ffad00");
    REQUIRE_EQ(package->itemSeparators->topDip, 6.75);
    REQUIRE_EQ(package->itemSeparators->bottomDip, 38.25);
    REQUIRE_EQ(package->itemSeparators->lineWidthDip, 0.4875);
    REQUIRE_EQ(package->itemSeparators->lineGapDip, 1.05);

    // Integers and inclusive outer limits are valid; touching strokes are valid.
    WriteSkin(skinsRoot, R"toml([candidate_window.item_separators]
color = "rgba(255, 173, 0, 1)"
top_dip = 0
bottom_dip = 4096
line_width_dip = 4096
line_gap_dip = 4096
)toml");
    REQUIRE(CandidateSkinCatalog::Load(skinsRoot, "demo").has_value());
    RemoveTestDirectory(uniqueRoot, directoryName);
}

TEST_CASE(candidate_skin_catalog_rejects_invalid_item_separator_styles)
{
    namespace fs = std::filesystem;
    const auto directoryName = L"msime-skin-invalid-separators-test-" + std::to_wstring(GetCurrentProcessId());
    const fs::path uniqueRoot = fs::temp_directory_path() / directoryName;
    const fs::path skinsRoot = uniqueRoot / L"skins";
    const char *keys[] = {"color", "top_dip", "bottom_dip", "line_width_dip", "line_gap_dip"};
    const char *validValues[] = {"\"#ffad00\"", "6.75", "38.25", "0.4875", "1.05"};
    const auto writeStyle = [&](int replacedIndex, const char *replacement, bool omit) {
        std::string fields = "[candidate_window.item_separators]\n";
        for (int index = 0; index < 5; ++index)
        {
            if (index == replacedIndex && omit)
                continue;
            fields +=
                std::string(keys[index]) + " = " + (index == replacedIndex ? replacement : validValues[index]) + "\n";
        }
        WriteSkin(skinsRoot, fields);
    };
    const auto requireInvalid = [&]() {
        std::string error;
        REQUIRE(!CandidateSkinCatalog::Load(skinsRoot, "demo", &error).has_value());
        REQUIRE(error.find("item_separators") != std::string::npos);
    };
    for (int index = 0; index < 5; ++index)
    {
        writeStyle(index, "", true);
        requireInvalid();
    }
    for (const char *value : {"true", "[]", "\"invalid;css\"", "\"\""})
    {
        writeStyle(0, value, false);
        requireInvalid();
    }
    for (int index = 1; index < 5; ++index)
    {
        for (const char *value : {"-1", "4096.01", "inf", "-inf", "nan", "true", "\"1.0\"", "[]"})
        {
            writeStyle(index, value, false);
            requireInvalid();
        }
    }
    for (const auto &invalid : {std::pair{2, "6.75"}, std::pair{3, "0"}, std::pair{4, "0.48"}})
    {
        writeStyle(invalid.first, invalid.second, false);
        requireInvalid();
    }
    for (const char *value : {"true", "[]", "\"style\""})
    {
        WriteSkin(skinsRoot, std::string("item_separators = ") + value + "\n");
        requireInvalid();
    }
    RemoveTestDirectory(uniqueRoot, directoryName);
}

TEST_CASE(candidate_skin_catalog_validates_horizontal_single_line_boolean)
{
    namespace fs = std::filesystem;
    const auto directoryName = L"msime-skin-single-line-test-" + std::to_wstring(GetCurrentProcessId());
    const fs::path uniqueRoot = fs::temp_directory_path() / directoryName;
    const fs::path skinsRoot = uniqueRoot / L"skins";
    for (const bool enabled : {false, true})
    {
        WriteSkin(skinsRoot,
                  std::string("[candidate_window.horizontal]\nsingle_line = ") + (enabled ? "true\n" : "false\n"));
        const auto package = CandidateSkinCatalog::Load(skinsRoot, "demo");
        REQUIRE(package.has_value());
        REQUIRE(package->horizontalGeometry.singleLine.has_value());
        REQUIRE_EQ(*package->horizontalGeometry.singleLine, enabled);
    }
    for (const char *value : {"0", "1", "\"true\"", "\"false\"", "0.0", "[]", "{}"})
    {
        WriteSkin(skinsRoot, std::string("[candidate_window.horizontal]\nsingle_line = ") + value + "\n");
        REQUIRE(!CandidateSkinCatalog::Load(skinsRoot, "demo").has_value());
    }
    RemoveTestDirectory(uniqueRoot, directoryName);
}

TEST_CASE(candidate_item_separators_follow_actual_nonuniform_bounds_in_current_frame)
{
    const CandidateItemRectDip card{100.0f, 200.0f, 500.0f, 100.0f};
    std::vector<CandidateItemRectDip> items{
        {150.0f, 210.0f, 80.0f, 20.0f}, {240.0f, 210.0f, 190.0f, 20.0f}, {440.0f, 210.0f, 40.0f, 20.0f}};
    const auto first = CandidateItemSeparatorLinesFromBounds(card, items, 6.75f, 38.25f, 0.4875f, 1.05f);
    REQUIRE_EQ(first.size(), size_t{4});
    REQUIRE(std::abs(first[0].x - 134.475f) < 0.0001f);
    REQUIRE(std::abs(first[1].x - 135.525f) < 0.0001f);
    REQUIRE(std::abs(first[2].x - 334.475f) < 0.0001f);
    REQUIRE_EQ(first[0].top, 6.75f);
    REQUIRE_EQ(first[0].bottom, 38.25f);
    REQUIRE_EQ(first[0].width, 0.4875f);

    // The card width stays the same, but redistributing text changes the lines.
    items[0].width += 20.0f;
    items[1].x += 20.0f;
    items[1].width -= 20.0f;
    const auto second = CandidateItemSeparatorLinesFromBounds(card, items, 6.75f, 38.25f, 0.4875f, 1.05f);
    REQUIRE_EQ(second.size(), first.size());
    REQUIRE(std::abs(second[0].x - first[0].x - 20.0f) < 0.0001f);
    REQUIRE_EQ(second[2].x, first[2].x);
}

TEST_CASE(candidate_item_separators_only_draw_first_row_internal_boundaries)
{
    const CandidateItemRectDip card{100.0f, 200.0f, 500.0f, 100.0f};
    const std::vector<CandidateItemRectDip> items{{150.0f, 210.0f, 80.0f, 20.0f},
                                                  {240.0f, 210.0f, 190.0f, 20.0f},
                                                  {150.0f, 240.0f, 80.0f, 20.0f},
                                                  {240.0f, 240.0f, 50.0f, 20.0f}};
    const auto lines = CandidateItemSeparatorLinesFromBounds(card, items, 6.75f, 38.25f, 0.4875f, 1.05f);
    REQUIRE_EQ(lines.size(), size_t{2});
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, {items.front()}, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, {}, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
}

TEST_CASE(candidate_item_separators_reject_invalid_geometry_and_bound_strokes_to_card)
{
    const CandidateItemRectDip card{100.0f, 200.0f, 500.0f, 30.0f};
    const std::vector<CandidateItemRectDip> items{{150.0f, 210.0f, 80.0f, 20.0f}, {240.0f, 210.0f, 190.0f, 20.0f}};
    const auto lines = CandidateItemSeparatorLinesFromBounds(card, items, 6.75f, 38.25f, 0.4875f, 1.05f);
    REQUIRE_EQ(lines.size(), size_t{2});
    REQUIRE_EQ(lines.front().bottom, card.height);
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, items, 31.0f, 38.25f, 0.4875f, 1.05f).empty());
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, items, 6.75f, 38.25f, 0.4875f, 0.4f).empty());
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, items, 6.75f, 38.25f, 0.0f, 1.05f).empty());
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        auto invalidCard = card;
        invalidCard.height = invalid;
        REQUIRE(CandidateItemSeparatorLinesFromBounds(invalidCard, items, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
        auto invalidItems = items;
        invalidItems[1].x = invalid;
        REQUIRE(CandidateItemSeparatorLinesFromBounds(card, invalidItems, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
        REQUIRE(CandidateItemSeparatorLinesFromBounds(card, items, invalid, 38.25f, 0.4875f, 1.05f).empty());
    }
    auto overlapping = items;
    overlapping[1].x = 229.0f;
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, overlapping, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
    auto outside = items;
    outside[1].width = 600.0f;
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, outside, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
    const std::vector<CandidateItemRectDip> nearEdge{{100.0f, 210.0f, 0.1f, 20.0f}, {100.1f, 210.0f, 20.0f, 20.0f}};
    REQUIRE(CandidateItemSeparatorLinesFromBounds(card, nearEdge, 6.75f, 38.25f, 0.4875f, 1.05f).empty());
}

TEST_CASE(candidate_skin_catalog_loads_optional_band_artwork)
{
    namespace fs = std::filesystem;
    const auto directoryName = L"msime-skin-band-artwork-test-" + std::to_wstring(GetCurrentProcessId());
    const fs::path uniqueRoot = fs::temp_directory_path() / directoryName;
    const fs::path skinsRoot = uniqueRoot / L"skins";
    WriteSkin(skinsRoot);
    auto package = CandidateSkinCatalog::Load(skinsRoot, "demo");
    REQUIRE(package.has_value());
    REQUIRE(!package->bandArtwork.has_value());
    WriteBandArtworkFixtures(skinsRoot);
    const char *fields = R"([candidate_window.band_artwork]
background = "assets/background.png"
foreground = "assets/foreground.PNG"
width_dip = 1032
height_dip = 344
offset_y_dip = -219.75
foreground_right_clip_dip = 137.695027624309
)";
    for (const std::string offsetX : {std::string(), std::string("offset_x_dip = 0.0\n")})
    {
        WriteSkin(skinsRoot, std::string(fields) + offsetX);
        std::string error;
        package = CandidateSkinCatalog::Load(skinsRoot, "demo", &error);
        REQUIRE(error.empty());
        REQUIRE(package.has_value());
        REQUIRE(package->bandArtwork.has_value());
        REQUIRE_EQ(package->bandArtwork->background, "assets/background.png");
        REQUIRE_EQ(package->bandArtwork->foreground, "assets/foreground.PNG");
        REQUIRE_EQ(package->bandArtwork->widthDip, 1032.0);
        REQUIRE_EQ(package->bandArtwork->heightDip, 344.0);
        REQUIRE_EQ(package->bandArtwork->offsetYDip, -219.75);
        REQUIRE_EQ(package->bandArtwork->foregroundRightClipDip, 137.695027624309);
    }
    RemoveTestDirectory(uniqueRoot, directoryName);
}

TEST_CASE(candidate_skin_catalog_rejects_invalid_band_artwork)
{
    namespace fs = std::filesystem;
    const auto directoryName = L"msime-skin-invalid-band-artwork-test-" + std::to_wstring(GetCurrentProcessId());
    const fs::path uniqueRoot = fs::temp_directory_path() / directoryName;
    const fs::path skinsRoot = uniqueRoot / L"skins";
    WriteBandArtworkFixtures(skinsRoot);
    const char *keys[] = {"background", "foreground",   "width_dip",
                          "height_dip", "offset_y_dip", "foreground_right_clip_dip"};
    const char *validValues[] = {"\"assets/background.png\"", "\"assets/foreground.PNG\"", "1032", "344", "-219.75",
                                 "137.695027624309"};
    const auto writeStyle = [&](int replacedIndex, const char *replacement, bool omit) {
        std::string fields = "[candidate_window.band_artwork]\n";
        for (int index = 0; index < 6; ++index)
        {
            if (index == replacedIndex && omit)
                continue;
            fields +=
                std::string(keys[index]) + " = " + (index == replacedIndex ? replacement : validValues[index]) + "\n";
        }
        WriteSkin(skinsRoot, fields);
    };
    const auto requireInvalid = [&]() {
        std::string error;
        REQUIRE(!CandidateSkinCatalog::Load(skinsRoot, "demo", &error).has_value());
        REQUIRE(error.find("band_artwork") != std::string::npos);
    };
    for (int index = 0; index < 6; ++index)
    {
        writeStyle(index, "", true);
        requireInvalid();
    }
    for (int index : {0, 1})
    {
        for (const char *value : {"true", "\"\"", "\"../background.png\"", "\"assets/background.jpg\"",
                                  "\"C:/background.png\"", "\"assets/missing.png\""})
        {
            writeStyle(index, value, false);
            requireInvalid();
        }
    }
    for (int index : {2, 3})
    {
        for (const char *value : {"0", "-1", "4096.01", "inf", "nan", "true", "\"1032\""})
        {
            writeStyle(index, value, false);
            requireInvalid();
        }
    }
    for (const char *value : {"-4096.01", "4096.01", "inf", "-inf", "nan", "true", "\"-219.75\""})
    {
        writeStyle(4, value, false);
        requireInvalid();
    }
    for (const char *value : {"-1", "1032.01", "inf", "nan", "true", "\"137\""})
    {
        writeStyle(5, value, false);
        requireInvalid();
    }
    for (const char *value : {"0.1", "-1", "nan", "false", "\"0\""})
    {
        writeStyle(-1, "", false);
        const fs::path path = skinsRoot / L"demo" / L"skin.toml";
        {
            std::ofstream manifest(path, std::ios::binary | std::ios::app);
            manifest << "offset_x_dip = " << value << "\n";
        }
        requireInvalid();
    }
    for (const char *value : {"true", "[]", "\"artwork\""})
    {
        WriteSkin(skinsRoot, std::string("band_artwork = ") + value + "\n");
        requireInvalid();
    }
    RemoveTestDirectory(uniqueRoot, directoryName);
}

TEST_CASE(candidate_band_artwork_keeps_left_texture_fixed_and_moves_right_foreground)
{
    constexpr float rightClip = static_cast<float>((2172.0 - 1850.0) * 0.9 * (1032.0 / 2172.0));
    constexpr float x = 32.0f;
    constexpr float y = 166.25f;
    for (const float cardWidth : {693.0293f, 900.0f, 1032.0f})
    {
        const auto geometry =
            CandidateBandArtworkGeometryFromBounds({x, y, cardWidth, 96.5f}, 1032.0f, 344.0f, -219.75f, rightClip);
        REQUIRE(geometry.has_value());
        REQUIRE_EQ(geometry->backgroundDestination.x, x);
        REQUIRE_EQ(geometry->backgroundDestination.y, -53.5f);
        REQUIRE_EQ(geometry->backgroundDestination.width, 1032.0f);
        REQUIRE_EQ(geometry->backgroundDestination.height, 344.0f);
        REQUIRE_EQ(geometry->backgroundClip.x, x);
        REQUIRE(std::abs(geometry->backgroundClip.width - (cardWidth - rightClip)) < 0.0001f);
        REQUIRE(std::abs(geometry->foregroundDestination.x - (x + cardWidth - 1032.0f)) < 0.0001f);
        REQUIRE_EQ(geometry->foregroundDestination.y, geometry->backgroundDestination.y);
        REQUIRE_EQ(geometry->foregroundDestination.width, 1032.0f);
        REQUIRE_EQ(geometry->foregroundDestination.height, 344.0f);
    }
}

TEST_CASE(candidate_band_artwork_clips_narrow_background_and_rejects_invalid_geometry)
{
    const CandidateItemRectDip card{32.0f, 166.25f, 100.0f, 96.5f};
    const auto narrow = CandidateBandArtworkGeometryFromBounds(card, 1032.0f, 344.0f, -219.75f, 137.695027624309f);
    REQUIRE(narrow.has_value());
    REQUIRE_EQ(narrow->backgroundClip.width, 0.0f);
    REQUIRE_EQ(narrow->foregroundDestination.x, -900.0f);
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        auto invalidCard = card;
        invalidCard.width = invalid;
        REQUIRE(!CandidateBandArtworkGeometryFromBounds(invalidCard, 1032.0f, 344.0f, -219.75f, 137.695027624309f));
        REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, invalid, 344.0f, -219.75f, 137.695027624309f));
        REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, invalid, -219.75f, 137.695027624309f));
        REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, 344.0f, invalid, 137.695027624309f));
        REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, 344.0f, -219.75f, invalid));
    }
    REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 0.0f, 344.0f, -219.75f, 137.695027624309f));
    REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, -1.0f, -219.75f, 137.695027624309f));
    REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, 344.0f, -4096.01f, 137.695027624309f));
    REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, 344.0f, -219.75f, 1032.01f));
    REQUIRE(!CandidateBandArtworkGeometryFromBounds(card, 1032.0f, 344.0f, -219.75f, -1.0f));
}
