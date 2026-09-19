// The GameItems frame table keeps the atlas file order (docs/01 §3, docs/03 §4).
#include "aa/data/frame_table_loader.h"
#include "test_support.h"

#include <doctest.h>

TEST_CASE("frame table: GameItems has 150 frames in file order") {
    AA_REQUIRE_ASSETS();
    aa::data::AtlasInfo info;
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(assetsDir() + "/atlases/GameItems.json", &info);
    CHECK(info.texture == "GameItems.png");
    CHECK(info.width == 1024);
    CHECK(info.height == 1024);
    REQUIRE(frames.size() == 150);
    CHECK(frames.find("8ball.png") == 0);
    CHECK(frames.find("TennisBall.png") == 138);
    CHECK(frames.find("ZipLineTrolley.png") == 149);
    CHECK(frames.find("nothing.png") == -1);
    const aa::sim::Frame& ball = frames.at(138);
    CHECK(ball.width() > 0.0f);
    CHECK(ball.x1 == ball.x0 + ball.width());
    CHECK(ball.y1 == ball.y0 + ball.height());
}

TEST_CASE("frame table: inline document") {
    const aa::data::JsonDoc doc(
        "{\"texture\":\"t.png\",\"size\":[8,4],\"frames\":[{\"name\":\"a\",\"x\":1,\"y\":2,\"w\":3,\"h\":4}]}", "inline");
    const aa::sim::FrameTable frames = aa::data::loadFrameTable(doc.root());
    REQUIRE(frames.size() == 1);
    CHECK(frames.at(0).x0 == 1.0f);
    CHECK(frames.at(0).y0 == 2.0f);
    CHECK(frames.at(0).x1 == 4.0f);
    CHECK(frames.at(0).y1 == 6.0f);
    CHECK(frames.find("a") == 0);
}
