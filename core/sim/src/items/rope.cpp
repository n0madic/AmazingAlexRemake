// Rope (9): st::RopeUtils::CreatePhysics with its anonymous helpers (docs/04 §8) — the root body (in
// set-up with a 0.01×0.01 selection box that UpdateLinkPositionsFromExtremes re-shapes to the rope's
// extent), N link bodies (N = clamp(int(len / 0.0671 + 1), 2, 15); bodies 1 and 2 are the two ends,
// 3..N the middle), the 0.01 kg mass override on every body including the root, N − 1 distance joints
// along the chain order of RopeRenderUtils::CalculateBodyIndices (60 Hz / 0.95), the straight-line
// placement of the links, and in set-up the two end selection circles.
#include "aa/sim/attachments.h"
#include "aa/sim/filters.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

#include <cmath>
#include <cstdint>

namespace aa::sim::items {

namespace {
constexpr float kLinkSpacing = 0.0671f;
constexpr int kMinLinks = 2;
constexpr int kMaxLinks = 15;
constexpr float kLinkRadius = 0.03355f;
constexpr float kLinkMass = 0.01f;
constexpr float kLinkInertia = 0.1f;
constexpr float kJointFrequency = 60.0f;
constexpr float kJointDamping = 0.95f;
constexpr float kWobble = 0.0001f;

// (anon)::CreateRopeBodies: N dynamic link bodies at the origin with one Rope-filter circle each.
void createRopeBodies(PhysicsObject& obj, PhysicsWorld& world, int count) {
    const b2FixtureDef fd = fixtureDef(1.0f, 0.0f, 0.0f, filters::kRope);
    b2BodyDef def;
    def.type = b2_dynamicBody;
    for (int i = 0; i < count; ++i) {
        const int body = addBody(obj, world, def);
        world.addCircle(body, kLinkRadius, Vec2(0.0f, 0.0f), fd);
    }
}

// (anon)::CreateRopeJoints: `segments` distance joints between consecutive chain bodies.
void createRopeJoints(PhysicsObject& obj, PhysicsWorld& world, int segments, float ropeLength) {
    const float segment = ropeLength / static_cast<float>(segments);
    std::array<int, PhysicsObject::kMaxBodies> chain{};
    ropeBodyIndices(obj.bodyCount, chain.data());
    for (int i = 0; i < segments; ++i) {
        b2DistanceJointDef jd;
        jd.bodyA = world.body(obj.bodies[static_cast<std::size_t>(chain[static_cast<std::size_t>(i)])]);
        jd.bodyB = world.body(obj.bodies[static_cast<std::size_t>(chain[static_cast<std::size_t>(i + 1)])]);
        jd.collideConnected = false;
        jd.length = segment;
        jd.frequencyHz = kJointFrequency;
        jd.dampingRatio = kJointDamping;
        jd.resistCompression = true;
        obj.addJoint(world.createDistance(jd));
    }
}

}  // namespace

// (anon)::SetRopeMassData (FUN_000e2ca4): 0.01 kg per body, 0.001 kg when either end hangs on a balloon.
void setRopeMassData(const WorldState& state, PhysicsObject& obj, PhysicsWorld& world) {
    auto attachedToBalloon = [&](int k) {
        const int other = obj.attachments[static_cast<std::size_t>(k)].otherObject;
        return other != -1 && state.objects[static_cast<std::size_t>(other)].type == ItemType::Balloon;
    };
    const float mass = attachedToBalloon(0) || attachedToBalloon(1) ? 0.001f : kLinkMass;
    for (int i = 0; i < obj.bodyCount; ++i) {
        world.setMassData(obj.bodies[static_cast<std::size_t>(i)], mass, Vec2(0.0f, 0.0f), kLinkInertia);
    }
}

int ropeLinkCount(Vec2 endVector) {
    const float n = length(endVector) / kLinkSpacing + 1.0f;
    float clamped = n;
    if (static_cast<float>(kMaxLinks) - n < 0.0f) clamped = static_cast<float>(kMaxLinks);
    if (n - static_cast<float>(kMinLinks) < 0.0f) return kMinLinks;
    return static_cast<int>(clamped);
}

void ropeBodyIndices(int bodyCount, int* out) {
    // RopeRenderUtils::CalculateBodyIndices: end A, the middle links in order, end B.
    if (bodyCount == 3) {
        out[0] = 1;
        out[1] = 2;
        return;
    }
    out[0] = 1;
    for (int i = 3; i < bodyCount; ++i) out[i - 2] = i;
    out[bodyCount - 2] = 2;
}

void createRope(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    b2BodyDef rootDef;
    rootDef.type = b2_dynamicBody;
    const int root = addBody(obj, world, rootDef);
    if (mode == PhysicsMode::SetUp) world.addBox(root, 0.01f, 0.01f, selectionDef());
    const int links = ropeLinkCount(item.endVector);
    createRopeBodies(obj, world, links);
    for (int i = 0; i < obj.bodyCount; ++i) {
        world.setMassData(obj.bodies[static_cast<std::size_t>(i)], kLinkMass, Vec2(0.0f, 0.0f), kLinkInertia);
    }
    createRopeJoints(obj, world, links - 1, length(item.endVector));
    obj.attachments[0].point.body = 1;
    obj.attachments[1].point.body = 2;
    updateRopeLinksFromExtremes(obj, item, world);
    if (mode == PhysicsMode::SetUp) {
        // The end circles carry their body index + 1 as fixture user data: the pick query maps it to the
        // object's per-end selectable byte [verified: RopeUtils::CreatePhysics, FUN_000d49f8].
        b2FixtureDef fd = selectionDef();
        fd.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
        world.addCircle(obj.bodies[1], kMinSelectionRadius, Vec2(0.0f, 0.0f), fd);
        fd.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(2));
        world.addCircle(obj.bodies[2], kMinSelectionRadius, Vec2(0.0f, 0.0f), fd);
    }
}

// FUN_000e37b0: re-create the middle links for a new link count. With only the two ends the joint between
// them is destroyed explicitly; otherwise the middle bodies go (and their joints with them).
void rebuildRopeLinks(const WorldState& state, PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, int links) {
    if (obj.bodyCount < 4) {
        const b2Body* endA = world.body(obj.bodies[1]);
        const b2Body* endB = world.body(obj.bodies[2]);
        for (const b2JointEdge* je = endA->GetJointList(); je; je = je->next) {
            if (je->other == endB) {
                world.destroyJoint(world.slotOf(je->joint));
                break;
            }
        }
    } else {
        for (int i = 3; i < obj.bodyCount; ++i) world.destroyBody(obj.bodies[static_cast<std::size_t>(i)]);
        obj.bodyCount = 3;
    }
    obj.jointCount = 0;
    createRopeBodies(obj, world, links - 2);
    setRopeMassData(state, obj, world);
    createRopeJoints(obj, world, links - 1, length(item.endVector));
}

void updateRopeFromAttachedObjects(const WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj) {
    const AttachmentRecord& a = obj.attachments[0];
    const AttachmentRecord& b = obj.attachments[1];
    if (a.state == attachment_state::kFree) {
        if (b.state == attachment_state::kFree) return;
    } else if (a.state == attachment_state::kAttached) {
        obj.position = attachmentPosWS(state.objects[static_cast<std::size_t>(a.otherObject)], world, a.otherPoint);
    }
    if (b.state == attachment_state::kAttached) {
        const Vec2 end = attachmentPosWS(state.objects[static_cast<std::size_t>(b.otherObject)], world, b.otherPoint);
        if (a.state == attachment_state::kAttached) {
            item.endVector = Vec2(end.x - obj.position.x, end.y - obj.position.y);
        } else {
            obj.position.x = obj.position.x + (end.x - (obj.position.x + item.endVector.x));
            obj.position.y = obj.position.y + (end.y - (obj.position.y + item.endVector.y));
        }
    }
    const int links = ropeLinkCount(item.endVector);
    if (obj.bodyCount != links + 1) rebuildRopeLinks(state, obj, item, world, links);
    updateRopeLinksFromExtremes(obj, item, world);
}

void updateRopeLinksFromExtremes(const PhysicsObject& obj, const GameItem& item, PhysicsWorld& world) {
    const Vec2 a = obj.position;
    const Vec2 b(a.x + item.endVector.x, a.y + item.endVector.y);
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    world.setTransform(obj.bodies[0], Vec2(a.x + dx * 0.5f, a.y + dy * 0.5f), 0.0f);
    world.setTransform(obj.bodies[1], a, 0.0f);
    world.setTransform(obj.bodies[2], b, 0.0f);
    if (obj.bodyCount > 3) {
        const float segments = static_cast<float>(obj.bodyCount - 2);
        float wobble = kWobble;
        for (int i = 3; i < obj.bodyCount; ++i) {
            const float t = static_cast<float>(i - 2) / segments;
            const float x = wobble + (a.x + t * dx);
            const float y = (a.y + t * dy) + 0.0f;
            world.setTransform(obj.bodies[static_cast<std::size_t>(i)], Vec2(x, y), 0.0f);
            wobble = -wobble;
        }
    }
    const float len = length(item.endVector);
    const float angle = atan2F(item.endVector.y, item.endVector.x);
    b2Body* root = world.body(obj.bodies[0]);
    b2Fixture* f = root->GetFixtureList();
    if (f) {
        while (f->GetFilterData().categoryBits != filters::kSelectionBit) f = f->GetNext();
        world.resetBoxAt(obj.bodies[0], f, len * 0.5f - kMinSelectionRadius, kMinSelectionRadius, Vec2(0.0f, 0.0f), angle);
        root->SetActive(false);
        root->SetActive(true);
    }
}

}  // namespace aa::sim::items

namespace aa::sim::items {

namespace {
constexpr float kEndJointStretch = 1.04f;
constexpr float kMaxReach = 1.0065f;                 // GetConstrainedPos: the end stays this close
constexpr float kMaxReachSquared = 1.0130422f;       // 1.0065² as the binary stores it
constexpr float kSlowDragSquared = 2.5e-05f;         // UpdatePos: snapping only below this speed
constexpr float kSnappedDragSquared = 0.00019600001f; // … or below this while already snapped
}  // namespace

void createRopeEndJoint(const WorldState& state, PhysicsWorld& world, GameItem& item, const PhysicsObject& obj) {
    // FUN_000e2b48: bodyA / bodyB are the bodies owning attachment point 0 of the two attached objects
    // (the original reads record 0 of both, not the record the rope end is attached to), anchors at
    // their scaled point-0 positions, length 1.04·|end|, no spring, collideConnected on.
    const PhysicsObject& a = state.objects[static_cast<std::size_t>(obj.attachments[0].otherObject)];
    const PhysicsObject& b = state.objects[static_cast<std::size_t>(obj.attachments[1].otherObject)];
    b2DistanceJointDef jd;
    jd.bodyA = world.body(a.bodies[static_cast<std::size_t>(a.attachments[0].point.body)]);
    jd.bodyB = world.body(b.bodies[static_cast<std::size_t>(b.attachments[0].point.body)]);
    jd.collideConnected = true;
    jd.localAnchorA = Vec2(a.scale.x * a.attachments[0].point.pos.x, a.scale.y * a.attachments[0].point.pos.y);
    jd.localAnchorB = Vec2(b.scale.x * b.attachments[0].point.pos.x, b.scale.y * b.attachments[0].point.pos.y);
    jd.length = length(item.endVector) * kEndJointStretch;
    jd.frequencyHz = 0.0f;
    jd.dampingRatio = 0.0f;
    jd.resistCompression = false;
    item.ropeJoint = world.createDistance(jd);
}

void ropeAttachmentChanged(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj, int point) {
    if (obj.attachments[0].state == attachment_state::kFree || obj.attachments[1].state == attachment_state::kFree) {
        if (item.ropeJoint >= 0) {
            world.destroyJoint(item.ropeJoint);
            item.ropeJoint = -1;
        }
    } else if (item.ropeJoint < 0) {
        createRopeEndJoint(state, world, item, obj);
    }
    const int s = obj.attachments[static_cast<std::size_t>(point)].state;
    const int selectable = s > 1 ? 0 : 1 - s;
    std::uint8_t& byte = obj.selectable[static_cast<std::size_t>(point)];
    byte = static_cast<std::uint8_t>((byte & ~body_flags::kSelectable) | (selectable & 1));
    setRopeMassData(state, obj, world);
}

Vec2 ropeConstrainedPos(const GameItem& item, const PhysicsObject& obj, int bodyIndex, Vec2 pos) {
    const int stateA = obj.attachments[0].state;
    const int stateB = obj.attachments[1].state;
    if (stateA == attachment_state::kFree && stateB == attachment_state::kFree) return pos;
    if (bodyIndex == 1) {
        if (stateB == attachment_state::kFree) return pos;
    } else if (bodyIndex == 2) {
        if (stateA == attachment_state::kFree) return pos;
    }
    float ax = obj.position.x;
    float ay = obj.position.y;
    if (bodyIndex == 1) {
        ax = obj.position.x + item.endVector.x;
        ay = obj.position.y + item.endVector.y;
    }
    const float dy = pos.y - ay;
    const float dx = pos.x - ax;
    const float d2 = dy * dy + dx * dx;
    if (kMaxReachSquared < d2) {
        const float d = std::sqrt(d2);
        return Vec2(ax + (dx / d) * kMaxReach, ay + (dy / d) * kMaxReach);
    }
    return pos;
}

void ropeSetEndPosition(GameItem& item, PhysicsObject& obj, int end, Vec2 pos) {
    if (end == 0) {
        obj.position = pos;
    } else if (end == 1) {
        item.endVector = Vec2(pos.x - obj.position.x, pos.y - obj.position.y);
    }
}

void ropeUpdatePos(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj, int bodyIndex, Vec2 target,
                   Vec2 dragVelocity) {
    const Vec2 constrained = ropeConstrainedPos(item, obj, bodyIndex, target);
    const Vec2 old = obj.position;
    const b2Body* grabbed = world.body(obj.bodies[static_cast<std::size_t>(bodyIndex)]);
    const Vec2 newPos((constrained.x - grabbed->GetPosition().x) + old.x, (constrained.y - grabbed->GetPosition().y) + old.y);
    const float v2 = dragVelocity.y * dragVelocity.y + dragVelocity.x * dragVelocity.x;
    bool anySnapped = false;
    for (int k = 0; k < obj.attachmentCount; ++k) {
        if (obj.attachments[static_cast<std::size_t>(k)].state == attachment_state::kSnapped) anySnapped = true;
    }
    SnapResult r;
    bool alreadySnapped = false;   // a snapped end found its own partner again: keep the records
    if ((v2 < kSlowDragSquared && 0.0f < v2) || (v2 < kSnappedDragSquared && anySnapped)) {
        r = calculateSnap(state, world, obj, newPos, constrained, length(item.endVector) * 1.1f);
        for (int k = 0; k < obj.attachmentCount; ++k) {
            const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
            if (rec.state != attachment_state::kSnapped) continue;
            if (r.otherObject >= 0 && r.otherObject == rec.otherObject) {
                r.found = false;
                alreadySnapped = true;
                break;
            }
            unsnap(state, obj.index, k);
        }
        if (!alreadySnapped && r.found) {
            const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(r.point)];
            if ((r.point == 0 || bodyIndex != 1) && (r.point == 1 || bodyIndex != 2) && r.otherObject != rec.otherObject) {
                if (rec.state == attachment_state::kFree) {
                    if (state.objects[static_cast<std::size_t>(r.otherObject)].attachments[static_cast<std::size_t>(r.otherPoint)].state !=
                        attachment_state::kFree) {
                        unsnap(state, r.otherObject, r.otherPoint);
                    }
                } else {
                    unsnap(state, item.objectIndex, r.point);
                }
                snap(state, obj.index, r.point, r.otherObject, r.otherPoint);
            } else {
                r.position = newPos;
                r.found = false;
            }
        }
    } else if (0.0f < v2) {
        r.position = newPos;
    }
    {
        const Vec2 before = obj.position;
        if (bodyIndex == 1) {
            obj.position = r.position;
            if (obj.attachments[1].state != attachment_state::kFree) {
                item.endVector.x = item.endVector.x - (r.position.x - before.x);
                item.endVector.y = item.endVector.y - (r.position.y - before.y);
            }
        } else if (bodyIndex == 2) {
            if (obj.attachments[0].state != attachment_state::kFree) {
                item.endVector.x = item.endVector.x + (r.position.x - before.x);
                item.endVector.y = item.endVector.y + (r.position.y - before.y);
            } else {
                obj.position = r.position;
            }
        } else if (bodyIndex == 0) {
            obj.position = r.position;
        }
    }
    const int links = ropeLinkCount(item.endVector);
    if (obj.bodyCount != links + 1) rebuildRopeLinks(state, obj, item, world, links);
    updateRopeLinksFromExtremes(obj, item, world);
}

void ropeManipulationStarted(WorldState& state, PhysicsWorld& world, PhysicsObject& obj, int bodyIndex) {
    auto detachAll = [&]() {
        for (int k = 0; k < obj.attachmentCount; ++k) {
            if (obj.attachments[static_cast<std::size_t>(k)].state == attachment_state::kAttached) detach(state, world, obj.index, k);
        }
    };
    if (bodyIndex == 0) {
        detachAll();
        return;
    }
    if (bodyIndex == 1) {
        if (obj.attachments[1].state != attachment_state::kFree) {
            if (obj.attachments[0].state == attachment_state::kAttached) detach(state, world, obj.index, 0);
            return;
        }
    } else if (bodyIndex == 2) {
        if (obj.attachments[0].state != attachment_state::kFree) {
            if (obj.attachments[1].state == attachment_state::kAttached) detach(state, world, obj.index, 1);
            return;
        }
    } else {
        return;
    }
    detachAll();
}

void ropeManipulationEnded(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj) {
    rebuildRopeLinks(state, obj, item, world, ropeLinkCount(item.endVector));
    updateRopeLinksFromExtremes(obj, item, world);
    if (item.ropeJoint >= 0) {
        world.destroyJoint(item.ropeJoint);
        item.ropeJoint = -1;
        createRopeEndJoint(state, world, item, obj);
    }
    attachToNearbyItems(state, world, obj.index);
}

void ropeSetSelectionCollisionFilters(const PhysicsObject& obj, PhysicsWorld& world) {
    for (int k = 0; k < 3 && k < obj.bodyCount; ++k) {
        b2Fixture* f = world.body(obj.bodies[static_cast<std::size_t>(k)])->GetFixtureList();
        if (f) f->SetFilterData(filters::kSelection);
    }
}

}  // namespace aa::sim::items
