// The level writer (the sandbox editor's save path): every shipped level survives write → load with every
// field bit-identical, and a synthetic user level with attachments, a rope and the Treehouse background
// round-trips as well.
#include "aa/data/level_loader.h"
#include "aa/data/level_writer.h"
#include "aa/sim/float_bits.h"
#include "aa/sim/world_state.h"
#include "test_support.h"

#include <doctest.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

using aa::sim::bitsFromFloat;
using aa::sim::Level;

void checkSameVec(aa::sim::Vec2 a, aa::sim::Vec2 b) {
    CHECK(bitsFromFloat(a.x) == bitsFromFloat(b.x));
    CHECK(bitsFromFloat(a.y) == bitsFromFloat(b.y));
}

void checkSameLevel(const Level& a, const Level& b) {
    CHECK(a.version == b.version);
    CHECK(a.title == b.title);
    CHECK(a.description == b.description);
    CHECK(a.authorName == b.authorName);
    CHECK(a.backgroundIndex == b.backgroundIndex);
    CHECK(a.rewardId == b.rewardId);
    CHECK(a.tested == b.tested);
    REQUIRE(a.toolbox.size() == b.toolbox.size());
    for (std::size_t i = 0; i < a.toolbox.size(); ++i) {
        CHECK(a.toolbox[i].type == b.toolbox[i].type);
        CHECK(a.toolbox[i].amount == b.toolbox[i].amount);
    }
    REQUIRE(a.items.size() == b.items.size());
    for (std::size_t i = 0; i < a.items.size(); ++i) {
        INFO("item " << i);
        const aa::sim::LevelItem& x = a.items[i];
        const aa::sim::LevelItem& y = b.items[i];
        CHECK(x.type == y.type);
        CHECK(x.handle == y.handle);
        checkSameVec(x.center, y.center);
        CHECK(bitsFromFloat(x.angle) == bitsFromFloat(y.angle));
        CHECK(x.flags == y.flags);
        checkSameVec(x.ropeEnd, y.ropeEnd);
        CHECK(x.itemData == y.itemData);
        REQUIRE(x.attachmentCount == y.attachmentCount);
        for (int k = 0; k < x.attachmentCount; ++k) {
            CHECK(x.attachments[static_cast<std::size_t>(k)].state == y.attachments[static_cast<std::size_t>(k)].state);
            CHECK(x.attachments[static_cast<std::size_t>(k)].objectIndex == y.attachments[static_cast<std::size_t>(k)].objectIndex);
            CHECK(x.attachments[static_cast<std::size_t>(k)].index == y.attachments[static_cast<std::size_t>(k)].index);
        }
    }
    CHECK(a.goal.type == b.goal.type);
    CHECK(a.goal.itemCount == b.goal.itemCount);
    CHECK(a.goal.itemHandles == b.goal.itemHandles);
    CHECK(a.goal.itemHandles2 == b.goal.itemHandles2);
    CHECK(a.goal.timeLimit == b.goal.timeLimit);
    CHECK(bitsFromFloat(a.goal.height) == bitsFromFloat(b.goal.height));
    CHECK(bitsFromFloat(a.goal.width) == bitsFromFloat(b.goal.width));
    CHECK(bitsFromFloat(a.goal.angle) == bitsFromFloat(b.goal.angle));
    CHECK(a.goal.negated == b.goal.negated);
}

std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

TEST_CASE("level writer: every shipped level round-trips bit for bit") {
    AA_REQUIRE_ASSETS();
    const std::string out = tempPath("aa_level_writer_roundtrip.json");
    int count = 0;
    for (const std::filesystem::directory_entry& chapter : std::filesystem::directory_iterator(assetsDir() + "/levels")) {
        if (!chapter.is_directory()) continue;
        for (const std::filesystem::directory_entry& f : std::filesystem::directory_iterator(chapter.path())) {
            if (f.path().extension() != ".json" || f.path().filename() == "index.json") continue;
            INFO(f.path().string());
            const Level original = aa::data::loadLevelFile(f.path().string());
            aa::data::writeLevelFile(out, original);
            const Level again = aa::data::loadLevelFile(out);
            checkSameLevel(original, again);
            ++count;
        }
    }
    CHECK(count >= 116);
    std::remove(out.c_str());
}

TEST_CASE("level writer: a user level with attachments, a rope and the Treehouse background") {
    Level level;
    level.title = "My contraption";
    level.description = "";
    level.authorName = "Player";
    level.backgroundIndex = 3;
    level.tested = true;
    level.toolbox.push_back({aa::sim::ItemType::Shelf, 2});
    aa::sim::LevelItem bound;
    bound.type = aa::sim::ItemType::WorldBound;
    bound.handle = aa::sim::Handle::make(aa::sim::ItemType::WorldBound, 0, 0);
    level.items.push_back(bound);
    aa::sim::LevelItem shelf;
    shelf.type = aa::sim::ItemType::Shelf;
    shelf.handle = aa::sim::Handle::make(aa::sim::ItemType::Shelf, 0, 1);
    shelf.center = aa::sim::Vec2(1.23456789f, -0.000123f);
    shelf.angle = 0.7853982f;
    shelf.flags = 3;
    level.items.push_back(shelf);
    aa::sim::LevelItem rope;
    rope.type = aa::sim::ItemType::Rope;
    rope.handle = aa::sim::Handle::make(aa::sim::ItemType::Rope, 0, 2);
    rope.center = aa::sim::Vec2(2.0f, 1.5f);
    rope.ropeEnd = aa::sim::Vec2(0.3f, -0.4f);
    rope.attachmentCount = 2;
    rope.attachments[0] = {aa::sim::attachment_state::kAttached, 1, 0};
    rope.attachments[1] = {aa::sim::attachment_state::kFree, -1, -1};
    level.items.push_back(rope);
    aa::sim::LevelItem book;
    book.type = aa::sim::ItemType::Book;
    book.handle = aa::sim::Handle::make(aa::sim::ItemType::Book, 0, 3);
    book.itemData = 2;
    level.items.push_back(book);
    level.goal.type = 7;
    level.goal.itemCount = 1;
    level.goal.itemHandles[0] = book.handle;
    level.goal.itemHandles2[0] = shelf.handle;
    level.goal.height = 0.9f;
    level.goal.width = 1.2f;
    level.goal.angle = 230.1f;

    const std::string out = tempPath("aa_level_writer_user.json");
    aa::data::writeLevelFile(out, level);
    const Level again = aa::data::loadLevelFile(out);
    checkSameLevel(level, again);
    CHECK(again.items[2].attachments[0].objectIndex == 1);
    std::remove(out.c_str());
}
