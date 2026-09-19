// The template table computed from the imported frames equals the original's (docs/08-physics-dump.json,
// values rounded to 6 decimals there — bit exactness is the conformance gate's job) for all 42 types.
#include "aa/data/frame_table_loader.h"
#include "aa/data/json.h"
#include "aa/sim/templates.h"
#include "test_support.h"

#include <doctest.h>

#include <cmath>
#include <string>

namespace {
constexpr double kDumpTolerance = 5e-7;   // the dump stores round(x, 6)
}

TEST_CASE("templates: flags, sizes and attachment points match the physics dump for every type") {
    AA_REQUIRE_ASSETS();
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(assetsDir() + "/atlases/GameItems.json");
    const aa::sim::TemplateTable table = aa::sim::initTemplates(frames);
    const aa::data::JsonDoc dump = aa::data::JsonDoc::parseFile(sourceDir() + "/docs/08-physics-dump.json");
    const aa::data::JsonNode items = dump.root().child("items");
    for (int t = aa::sim::kFirstItemType; t <= aa::sim::kLastItemType; ++t) {
        const aa::sim::PhysicsObjectTemplate& tmpl = table[static_cast<std::size_t>(t)];
        const aa::data::JsonNode entry = items.child(std::to_string(t).c_str());
        INFO("type " << t << " " << entry.getString("name"));
        CHECK(static_cast<int>(tmpl.type) == t);
        // Any variant / mode carries the same template-derived object fields; take the first variant.
        const aa::data::JsonNode object = entry.child("variants").members().front().second.child("simulation").child("object");
        CHECK(static_cast<int>(tmpl.flags) == object.getInt("flags"));
        CHECK(std::fabs(static_cast<double>(tmpl.halfSize) - object.child("halfSize").getDouble()) <= kDumpTolerance);
        const std::vector<aa::data::JsonNode> atts = object.child("attachments").array();
        REQUIRE(static_cast<int>(atts.size()) == tmpl.attachmentCount);
        for (std::size_t i = 0; i < atts.size(); ++i) {
            INFO("attachment " << i);
            const aa::sim::AttachmentPoint& p = tmpl.attachments[i];
            CHECK(std::fabs(static_cast<double>(p.pos.x) - atts[i].child("pos").at(0).getDouble()) <= kDumpTolerance);
            CHECK(std::fabs(static_cast<double>(p.pos.y) - atts[i].child("pos").at(1).getDouble()) <= kDumpTolerance);
            CHECK(std::fabs(static_cast<double>(p.dir.x) - atts[i].child("dir").at(0).getDouble()) <= kDumpTolerance);
            CHECK(std::fabs(static_cast<double>(p.dir.y) - atts[i].child("dir").at(1).getDouble()) <= kDumpTolerance);
            CHECK(p.kind == atts[i].getInt("kind"));
            CHECK(p.mask == atts[i].getInt("mask"));
            // The rope's end points are moved onto the extreme link bodies by RopeUtils::CreatePhysics (a
            // run-time edit of the record, docs/04 §8, checked by test_physics_world.cpp); the template
            // itself says body 0.
            if (t != static_cast<int>(aa::sim::ItemType::Rope)) CHECK(p.body == atts[i].getInt("body"));
            CHECK(static_cast<int>(p.positionOnly) == atts[i].getInt("positionOnly"));
        }
    }
}

TEST_CASE("templates: the uninitialised slot keeps the static-initialised defaults") {
    aa::sim::FrameTable frames;
    frames.frames.resize(aa::sim::kTemplateFrameCount);
    const aa::sim::TemplateTable table = aa::sim::initTemplates(frames);
    CHECK(table[0].type == aa::sim::ItemType::None);
    CHECK(table[0].flags == aa::sim::object_flags::kTemplateDefault);
    CHECK(table[0].halfSize == 1.0f);
    // Frames of zero size give the (−2 px) trim → negative half-sizes; only the fixed radii survive.
    CHECK(table[static_cast<std::size_t>(aa::sim::ItemType::BouncyBall)].halfSize == 0.04f);
    CHECK(table[static_cast<std::size_t>(aa::sim::ItemType::ZipLine)].halfSize == 1.0f);
}
