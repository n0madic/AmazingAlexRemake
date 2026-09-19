// core/data UI loaders (docs/12 §4): the SPRT / COMP / FONT containers, ui/fonts.json, the scene trees,
// the text bundles and tips.json of the imported tree.
#include "aa/data/asset_root.h"
#include "aa/data/ui_loaders.h"
#include "test_support.h"

#include <doctest.h>

using namespace aa::data;

TEST_CASE("ui loaders: sprite sheet frames by name") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    const std::string profile = root.manifest().profile;
    const UiContainer c = loadUiContainerFile(root.path("ui/" + profile + "/MENU_MENU_INGAME.json"), "MENU_MENU_INGAME");
    REQUIRE(c.sheets.size() == 1);
    CHECK(c.compoSets.empty());
    CHECK(c.fonts.empty());
    const UiSpriteSheet& sheet = c.sheets[0];
    CHECK(sheet.texture == "MENU_MENU_INGAME.png");
    CHECK(sheet.sprites.size() == 24);
    const UiSprite* play = sheet.find("BUTTON_LARGE_PLAY");
    REQUIRE(play != nullptr);
    CHECK(play->w == 80);
    CHECK(play->h == 98);
    CHECK(sheet.find("NO_SUCH_SPRITE") == nullptr);
    const UiContainer common = loadUiContainerFile(root.path("ui/" + profile + "/MENU_MENU_COMMON.json"), "MENU_MENU_COMMON");
    const UiSprite* base = common.sheets[0].find("BUTTON_LARGE_BASE");
    REQUIRE(base != nullptr);
    CHECK(base->w == 200);
    CHECK(base->pivotX == 99);   // the KA3D pivot as stored, not w / 2
}

TEST_CASE("ui loaders: compo sprites") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    const std::string profile = root.manifest().profile;
    const UiContainer c = loadUiContainerFile(root.path("ui/" + profile + "/BOOKS_COMPOSPRITES.json"), "BOOKS_COMPOSPRITES");
    REQUIRE(c.compoSets.size() == 1);
    CHECK(c.compoSets[0].compos.size() == 35);
    const CompoSprite* book = c.compoSets[0].find("BOOK_CLASSROOM_EN");
    REQUIRE(book != nullptr);
    REQUIRE(book->parts.size() == 2);
    CHECK(book->parts[0].sprite == "BOOK_CLASSROOM");
    CHECK(book->parts[1].sprite == "TEXT_CLASSROOM_EN");
}

TEST_CASE("ui loaders: bitmap font metrics, signed tracking") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    const std::string profile = root.manifest().profile;
    const UiContainer f3 = loadUiContainerFile(root.path("ui/" + profile + "/FONT_3.json"), "FONT_3");
    REQUIRE(f3.fonts.size() == 1);
    const BitmapFontData& font = f3.fonts[0];
    CHECK(font.texture == "FONT_3.png");
    CHECK(font.glyphs.size() == 145);
    const Glyph* a = font.find('A');
    REQUIRE(a != nullptr);
    CHECK(a->w > 0);
    CHECK(font.maxAscending > 0);
    CHECK(font.maxDescending >= 0);
    CHECK(font.find(0x2603) == nullptr);
    // The outline font of FONT_3 carries a negative tracking (KA3D readShort; an old importer wrote 65517).
    const UiContainer o3 = loadUiContainerFile(root.path("ui/" + profile + "/FONT_3_OUTLINES.json"), "FONT_3_OUTLINES");
    REQUIRE(o3.fonts.size() == 1);
    CHECK(o3.fonts[0].tracking < 0);
    CHECK(o3.fonts[0].tracking == -19);
    const FontsConfig fonts = loadFontsConfigFile(root.path("ui/fonts.json"));
    REQUIRE(fonts.count("FONT_3") == 1);
    CHECK(fonts.at("FONT_3").outlineFont == "FONT_3_OUTLINES");
    CHECK(fonts.at("FONT_3").offsetX == 10);
}

TEST_CASE("ui loaders: scene tree attributes") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    const SceneTree scene = SceneTree::loadFile(root.path("ui/scenes/MainMenuScene.json"));
    const JsonNode menu = scene.view("MainMenuView");
    REQUIRE(menu.isObject());
    CHECK(menu.getString("Background") == "BACKGROUND_BLUE");
    const JsonNode play = menu.child("ButtonPlay");
    CHECK(play.child("Relative").getFloat("Y") == doctest::Approx(65.0f));
    CHECK_FALSE(play.child("Relative").has("W"));
    CHECK(play.child("Anchor").child("H").getString("Self") == "HCENTER");
    CHECK(play.getString("ImageBackground") == "BUTTON_TITLE_PLAY");
    CHECK(menu.child("PanelTop").child("SettingsSlider").getString("Direction") == "DOWN");
    CHECK(scene.view("NoSuchView").isNull());
}

TEST_CASE("ui loaders: texts and tips") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    const TextTable en = loadTextTableFile(root.textsPath("en_EN"));
    // The iOS bundle's 421 ids (the Android package lacks two credits lines and adds the lite edition's 16
    // level titles, which the importer's chapter skip leaves in the text table).
    if (root.manifest().sourceKind == "ipa") CHECK(en.size() == 421);
    CHECK(en.at("CHAPTER_NAME_CHAPTER1") == "The Classroom");
    const TextTable de = loadTextTableFile(root.textsPath("de_DE"));
    CHECK(de.at("CHAPTER_NAME_CHAPTER1") != en.at("CHAPTER_NAME_CHAPTER1"));
    CHECK(en.count("TEXT_LEVEL_NAME_00_01") == 1);
    const std::vector<Tip> tips = loadTipsFile(root.tipsPath());
    CHECK(tips.size() == 22);
    int generic = 0;
    int ios = 0;
    for (const Tip& t : tips) {
        if (t.objectType == 0) ++generic;
        if (t.platform == 1) ++ios;
        CHECK_FALSE(t.text.empty());
    }
    CHECK(generic == 14);
    CHECK(ios >= 1);
}
