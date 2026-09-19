// Scissors (6): st::ScissorsUtils::CreatePhysics — two half bodies (kinematic in simulation, dynamic in
// set-up) created at the origin, each with two blade polygons (pixel coordinates of the 115-px sprite
// divided by `115 / (2r)`), a sharp sensor circle (group −2) at the blade tip in simulation, the selection
// circle on the top half in set-up, then ScissorsUtils::UpdateAngle places both halves about the item
// centre (± r·0.12 rotated by the item angle, halves rotated by ± the cut angle Scissors+0xC, 15° by
// default).
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "aa/sim/animations.h"
#include "item_common.h"

#include <cmath>
#include <vector>

namespace aa::sim::items {

namespace {

constexpr int16 kGroupSharp = filters::kGroupSharp;
constexpr float kCutImpulse = 0.01f;   // |v_n · m_other| that closes the blades
constexpr float kTipWidth = 0.02f;      // the query rectangle's width across the tip
constexpr int kMaxCutLinks = 15;        // the callback stops after the 16th hit
constexpr float kCutNudge = 0.001f;
constexpr float kSnipSpin = -6.283184f;   // DAT_00291798 (−2·Pi of the game)
constexpr int kSnipLastStep = 4;
constexpr float kSnipStartPhase = 0.2f;
constexpr float kSnipDuration = 0.5f;
constexpr float kSnipStepTime = 0.04f;   // 0x3d23d70a

b2BodyDef halfBodyDef(PhysicsMode mode) {
    b2BodyDef def;
    def.type = mode == PhysicsMode::SetUp ? b2_dynamicBody : b2_kinematicBody;
    return def;
}

}  // namespace

void createScissors(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float k = 115.0f / (r + r);
    auto px = [k](float v) { return v / k; };
    const b2FixtureDef blade = fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kDynamic);
    b2FixtureDef tip = fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::withGroup(filters::kDynamic, kGroupSharp), true);
    const float tipX = r * 0.95f;

    // Top half: body 0.
    const int top = addBody(obj, world, halfBodyDef(mode));
    const Vec2 top1[5] = {Vec2(px(32.5f), px(20.0f)), Vec2(px(10.2f), px(20.0f)), Vec2(px(-11.7f), px(11.0f)),
                          Vec2(px(-5.7f), px(2.5f)), Vec2(px(56.2f), px(2.5f))};
    const Vec2 top2[6] = {Vec2(px(-15.7f), px(7.0f)), Vec2(px(-43.2f), px(7.7f)), Vec2(px(-56.2f), px(-6.0f)),
                          Vec2(px(-48.5f), px(-21.2f)), Vec2(px(-27.2f), px(-18.5f)), Vec2(px(-11.2f), px(-2.7f))};
    world.addPolygon(top, top1, 5, blade);
    world.addPolygon(top, top2, 6, blade);
    if (mode == PhysicsMode::Simulation) world.addCircle(top, 0.01f, Vec2(tipX, 0.01f), tip);

    // Bottom half: body 1.
    const int bottom = addBody(obj, world, halfBodyDef(mode));
    const Vec2 bottom1[5] = {Vec2(px(-5.7f), px(-0.2f)), Vec2(px(-13.7f), px(-8.5f)), Vec2(px(11.0f), px(-22.5f)),
                             Vec2(px(37.5f), px(-22.2f)), Vec2(px(55.0f), px(-11.2f))};
    const Vec2 bottom2[6] = {Vec2(px(-9.0f), px(3.7f)), Vec2(px(-20.7f), px(16.2f)), Vec2(px(-43.5f), px(22.5f)),
                             Vec2(px(-54.2f), px(9.5f)), Vec2(px(-40.5f), px(-7.0f)), Vec2(px(-16.5f), px(-8.0f))};
    world.addPolygon(bottom, bottom1, 5, blade);
    world.addPolygon(bottom, bottom2, 6, blade);
    if (mode == PhysicsMode::Simulation) world.addCircle(bottom, 0.01f, Vec2(tipX, -0.05f), tip);

    if (mode == PhysicsMode::SetUp) world.addCircle(top, r, Vec2(0.0f, 0.0f), selectionDef());
    updateScissorsAngle(obj, world, item.cutAngle);
}

void updateScissorsAngle(PhysicsObject& obj, PhysicsWorld& world, float cutAngle) {
    const Vec2 v = rotate(obj.angle, Vec2(0.0f, obj.halfSize * 0.12f));
    const Vec2 p = obj.position;
    world.setTransform(obj.bodies[0], Vec2(p.x - v.x, p.y - v.y), obj.angle + cutAngle);
    world.setTransform(obj.bodies[1], Vec2(v.x + p.x, v.y + p.y), obj.angle - cutAngle);
}

void scissorsHandleCollision(GameItem& item, const PhysicsObject& obj, const b2Body* other, float impact, ActionQueue& queue) {
    // ScissorsUtils::HandleCollision [verified].
    if (item.scissorsState != 0) return;
    if (!(std::fabs(impact * other->GetMass()) > kCutImpulse)) return;
    item.scissorsState = 1;
    queue.add(Action::sound(sound::kScissorsCut, obj.position, 1.0f));
}

// FUN_000e4314: the idle snip animation shared by the two Update entry points [verified: disassembly].
void updateSnip(float dt, GameItem& item, Random& random) {
    item.snipTimer = item.snipTimer - dt;
    if (item.snipping) {
        float step = (dt + dt) * item.snipDirection;
        item.snipAngle = item.snipAngle + dt * kSnipSpin;
        step = step + step;
        item.snipPhase = step + item.snipPhase;
        if (item.snipDirection > 0.0f && item.snipPhase >= 1.0f) item.snipDirection = -1.0f;
    }
    if (item.snipTimer > 0.0f) return;
    if (item.snipping) {
        item.snipping = false;
        item.snipStep = -1;
        item.snipTimer = random.getFloat(GameItem::kIdleTimerMin, GameItem::kIdleTimerMax);
        return;
    }
    if (item.snipStep == kSnipLastStep) {
        item.snipping = true;
        item.snipAngle = random.getFloat(-kPi, kPi);
        item.snipPhase = kSnipStartPhase;
        item.snipDirection = 1.0f;
        item.snipTimer = kSnipDuration;
        return;
    }
    item.snipStep = item.snipStep + 1;
    item.snipTimer = kSnipStepTime;
}

void cutRope(WorldState& state, PhysicsWorld& world, PhysicsObject& rope, int linkA, int linkB) {
    // RopeUtils::Cut [verified: decompile + disassembly].
    b2Body* a = world.body(rope.bodies[static_cast<std::size_t>(linkA)]);
    const b2Body* b = world.body(rope.bodies[static_cast<std::size_t>(linkB)]);
    b2Joint* joint = nullptr;
    for (const b2JointEdge* je = a->GetJointList(); je; je = je->next) {
        if (je->other == b) {
            joint = je->joint;
            break;
        }
    }
    if (joint == nullptr) return;
    world.destroyJoint(world.slotOf(joint));
    GameItem& item = state.itemOf(rope);
    if (item.ropeJoint >= 0) {
        world.destroyJoint(item.ropeJoint);
        item.ropeJoint = -1;
    }
    float nudge = kCutNudge;
    for (int k = 1; k < rope.bodyCount; ++k) {
        const int slot = rope.bodies[static_cast<std::size_t>(k)];
        const b2Body* link = world.body(slot);
        world.setTransform(slot, Vec2(link->GetPosition().x + nudge, link->GetPosition().y), link->GetAngle());
        nudge = -nudge;
    }
    rope.state = static_cast<std::uint8_t>(rope.state | object_state::kActivated);
}

void updateScissors(float dt, WorldState& state, PhysicsWorld& world, Random& random, ActionQueue& queue) {
    // ScissorsUtils::Update [verified: decompile + disassembly].
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Scissors) continue;
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if (item.scissorsState == 1) {
            item.cutAngle = item.cutAngle - (dt + dt);
            if (item.cutAngle <= 0.0f) {
                const float r = obj.halfSize;
                const Vec2 n = normalize(rotate(obj.angle, Vec2(r, 0.0f)));
                const Vec2 pos = obj.position;
                const float half = r * 0.5f;
                const float tipX = (pos.x + r * n.x) - n.y * kTipWidth;
                const float tipY = pos.y + r * n.y + n.x * kTipWidth;
                const Vec2 center(pos.x + n.x * half, pos.y + n.y * half);
                const float radius2 = half * half;
                b2AABB aabb;
                aabb.lowerBound.x = 0.0f <= pos.x - tipX ? tipX : pos.x;
                aabb.lowerBound.y = 0.0f <= pos.y - tipY ? tipY : pos.y;
                aabb.upperBound.x = 0.0f <= pos.x - tipX ? pos.x : tipX;
                aabb.upperBound.y = 0.0f <= pos.y - tipY ? pos.y : tipY;
                // The query keeps up to 16 rope link fixtures whose body lies within the half-circle.
                struct Hit {
                    int object;
                    b2Body* body;
                };
                std::vector<Hit> hits;
                world.queryAABB(aabb, [&](b2Fixture* f) {
                    if (f->GetFilterData().categoryBits != filters::kRopeBit) return true;
                    b2Body* body = f->GetBody();
                    const int oi = PhysicsWorld::bodyObject(body);
                    if (oi < 0 || state.objects[static_cast<std::size_t>(oi)].type != ItemType::Rope) return true;
                    float d2 = body->GetPosition().y - center.y;
                    d2 = d2 * d2;
                    const float dx = body->GetPosition().x - center.x;
                    d2 = d2 + dx * dx;
                    if (d2 > radius2) return true;
                    hits.push_back({oi, body});
                    return static_cast<int>(hits.size()) <= kMaxCutLinks;
                });
                for (const Hit& h : hits) {
                    PhysicsObject& rope = state.objects[static_cast<std::size_t>(h.object)];
                    int i = -1;
                    for (int k = 0; k < rope.bodyCount; ++k) {
                        if (world.body(rope.bodies[static_cast<std::size_t>(k)]) == h.body) {
                            i = k;
                            break;
                        }
                    }
                    int j = 1;
                    if (i != 0) {
                        j = i - 1;
                        if (i != rope.bodyCount - 1) {
                            const b2Body* prev = world.body(rope.bodies[static_cast<std::size_t>(i - 1)]);
                            const b2Body* next = world.body(rope.bodies[static_cast<std::size_t>(i + 1)]);
                            const float py = prev->GetPosition().y - center.y;
                            const float px = prev->GetPosition().x - center.x;
                            const float ny = next->GetPosition().y - center.y;
                            const float nx = next->GetPosition().x - center.x;
                            j = i + 1;
                            if (std::sqrt(py * py + px * px) <= std::sqrt(ny * ny + nx * nx)) j = i - 1;
                        }
                    }
                    cutRope(state, world, rope, i, j);
                }
                if (!hits.empty()) queue.add(Action::sound(sound::kRopeCut, obj.position, 1.0f));
                item.cutAngle = 0.0f;
                item.scissorsState = 2;
            }
            updateScissorsAngle(obj, world, item.cutAngle);
        }
        updateSnip(dt, item, random);
    }
}

void updateScissorsSetUpMode(float dt, WorldState& state, Random& random) {
    for (GameItem& item : state.items) {
        if (item.type == ItemType::Scissors) updateSnip(dt, item, random);
    }
}

}  // namespace aa::sim::items
