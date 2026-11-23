#include "tests/includes/test_framework.h"

#include "skin/candidate_skin_catalog.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
namespace fs = std::filesystem;

std::string ComboFields(const std::string &extra = "", int digits = 10,
                        const std::string &firstDigit = "assets/combo/0.png",
                        const std::string &label = "assets/combo/label.png")
{
    std::string fields = "[candidate_window.livesd.combo]\ndigit_images = [";
    for (int i = 0; i < digits; ++i)
    {
        if (i)
            fields += ", ";
        fields += "\"" + (i == 0 ? firstDigit : "assets/combo/" + std::to_string(i) + ".png") + "\"";
    }
    return fields + "]\nlabel_image = \"" + label + "\"\n" + extra;
}

class SkinFixture
{
  public:
    explicit SkinFixture(const std::string &combo)
    {
        static unsigned serial = 0;
        root = fs::current_path() /
               (L"msime-combo-catalog-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial)) /
               L"skins";
        fs::create_directories(root / L"art" / L"assets" / L"combo");
        for (const auto *frame : {L"idle.png", L"idle2.png", L"happy.png", L"miss.png"})
            WriteImage(root / L"art" / L"assets" / frame);
        for (unsigned i = 0; i < 10; ++i)
            WriteImage(root / L"art" / L"assets" / L"combo" / (std::to_wstring(i) + L".png"));
        WriteImage(root / L"art" / L"assets" / L"combo" / L"label.png");
        std::ofstream manifest(root / L"art" / L"skin.toml", std::ios::binary);
        REQUIRE(manifest.is_open());
        manifest << "schema_version = 2\nid = \"art\"\nname = \"Combo\"\nversion = \"1\"\n"
                    "base = \"canvas\"\n[supports]\nlayouts = [\"horizontal\"]\nthemes = [\"dark\"]\n"
                    "[candidate_window.livesd]\nidle_image = \"assets/idle.png\"\n"
                    "idle2_image = \"assets/idle2.png\"\nhappy_image = \"assets/happy.png\"\n"
                    "miss_image = \"assets/miss.png\"\n"
                 << combo;
    }

    ~SkinFixture()
    {
        std::error_code error;
        fs::remove_all(root.parent_path(), error);
    }

    std::optional<CandidateSkinCatalog::Package> Load(std::string *error = nullptr) const
    {
        return CandidateSkinCatalog::Load(root, "art", error);
    }

    fs::path root;

  private:
    static void WriteImage(const fs::path &path)
    {
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        // Parsing validates resource identity and containment, not image decoding.
        file << "\x89PNG\r\n\x1a\n";
    }
};

void RequireRejected(const std::string &fields)
{
    SkinFixture skin(fields);
    std::string error;
    REQUIRE(!skin.Load(&error));
    REQUIRE(error.find("livesd") != std::string::npos);
}
} // namespace

TEST_CASE(candidate_skin_catalog_combo_is_optional_and_disabled_skips_fields)
{
    SkinFixture absent("");
    const auto plain = absent.Load();
    REQUIRE(plain && plain->liveSD && !plain->liveSD->combo);

    SkinFixture disabled("[candidate_window.livesd.combo]\nenabled = false\n"
                         "digit_images = 42\nlabel_image = \"../outside.png\"\ndigit_height_dip = nan\n");
    const auto off = disabled.Load();
    REQUIRE(off && off->liveSD && !off->liveSD->combo);
    REQUIRE(off->notices.empty());
}

TEST_CASE(candidate_skin_catalog_combo_loads_all_ten_digits_and_default_dip_geometry)
{
    SkinFixture skin(ComboFields());
    std::string error;
    const auto package = skin.Load(&error);
    REQUIRE(error.empty());
    REQUIRE(package && package->liveSD && package->liveSD->combo);
    const auto &combo = *package->liveSD->combo;
    for (unsigned i = 0; i < 10; ++i)
        REQUIRE_EQ(combo.digitImages[i], "assets/combo/" + std::to_string(i) + ".png");
    REQUIRE_EQ(combo.labelImage, std::string("assets/combo/label.png"));
    REQUIRE_EQ(combo.digitHeightDip, 32.0);
    REQUIRE_EQ(combo.slotWidthDip, 26.0);
    REQUIRE_EQ(combo.labelHeightDip, 12.0);
    REQUIRE_EQ(combo.labelGapDip, 2.0);
    REQUIRE_EQ(combo.offsetXDip, 0.0);
    REQUIRE_EQ(combo.offsetYDip, -200.0);
    REQUIRE(package->notices.empty());
}

TEST_CASE(candidate_skin_catalog_combo_reads_explicit_dip_geometry)
{
    SkinFixture skin(ComboFields("enabled = true\ndigit_height_dip = 40\nslot_width_dip = 34\n"
                                 "label_height_dip = 16\nlabel_gap_dip = 0\n"
                                 "offset_x_dip = -25\noffset_y_dip = 12.5\n"));
    const auto package = skin.Load();
    REQUIRE(package && package->liveSD && package->liveSD->combo);
    const auto &combo = *package->liveSD->combo;
    REQUIRE_EQ(combo.digitHeightDip, 40.0);
    REQUIRE_EQ(combo.slotWidthDip, 34.0);
    REQUIRE_EQ(combo.labelHeightDip, 16.0);
    REQUIRE_EQ(combo.labelGapDip, 0.0);
    REQUIRE_EQ(combo.offsetXDip, -25.0);
    REQUIRE_EQ(combo.offsetYDip, 12.5);
}

TEST_CASE(candidate_skin_catalog_combo_rejects_incomplete_or_unsafe_resources)
{
    RequireRejected(ComboFields("", 9));
    RequireRejected(ComboFields("", 11));
    RequireRejected("[candidate_window.livesd.combo]\ndigit_images = [0,1,2,3,4,5,6,7,8,9]\n"
                    "label_image = \"assets/combo/label.png\"\n");
    RequireRejected("[candidate_window.livesd.combo]\nenabled = 1\n");
    for (const auto *path : {"../outside.png", "assets/../outside.png", "C:/outside.png", "/outside.png",
                             "assets/combo/0.gif", "assets/combo/./0.png", ""})
    {
        RequireRejected(ComboFields("", 10, path));
        RequireRejected(ComboFields("", 10, "assets/combo/0.png", path));
    }
}

TEST_CASE(candidate_skin_catalog_combo_rejects_invalid_dip_geometry)
{
    for (const auto *extra :
         {"digit_height_dip = 7.9\n", "digit_height_dip = 128.1\n", "slot_width_dip = 7.9\n",
          "slot_width_dip = 128.1\n", "label_height_dip = 3.9\n", "label_height_dip = 64.1\n", "label_gap_dip = -0.1\n",
          "label_gap_dip = 64.1\n", "offset_x_dip = -4096.1\n", "offset_y_dip = 4096.1\n", "digit_height_dip = nan\n",
          "slot_width_dip = inf\n", "label_height_dip = true\n", "label_gap_dip = \"2\"\n", "offset_x_dip = -inf\n",
          "offset_y_dip = []\n"})
        RequireRejected(ComboFields(extra));
}

TEST_CASE(candidate_skin_catalog_combo_missing_asset_disables_only_counter)
{
    for (unsigned i = 0; i <= 10; ++i)
    {
        SkinFixture skin(ComboFields());
        const auto name = i == 10 ? L"label.png" : std::to_wstring(i) + L".png";
        REQUIRE(fs::remove(skin.root / L"art" / L"assets" / L"combo" / name));
        std::string error;
        const auto package = skin.Load(&error);
        REQUIRE(error.empty());
        REQUIRE(package && package->liveSD && !package->liveSD->combo);
        REQUIRE_EQ(package->liveSD->idleImage, std::string("assets/idle.png"));
        REQUIRE_EQ(package->notices.size(), size_t{1});
        REQUIRE(package->notices.front().find("combo disabled") != std::string::npos);
    }
}
