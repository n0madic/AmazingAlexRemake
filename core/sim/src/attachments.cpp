#include "aa/sim/attachments.h"

#include "aa/sim/animations.h"
#include "aa/sim/filters.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"

#include <cmath>

namespace aa::sim {

Vec2 attachmentPosWS(const PhysicsObject& obj, const PhysicsWorld& world, int point) {
    const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(point)];
    const b2Body* body = world.body(obj.bodies[static_cast<std::size_t>(rec.point.body)]);
    const Vec2 local(obj.scale.x * rec.point.pos.x, obj.scale.y * rec.point.pos.y);
    const Vec2 r = rotate(body->GetAngle(), local);
    return Vec2(r.x + body->GetPosition().x, body->GetPosition().y + r.y);
}

void createAttachmentJoint(WorldState& state, PhysicsWorld& world, int object, int point) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(point)];
    const int otherObject = rec.otherObject;
    const int otherPoint = rec.otherPoint;
    PhysicsObject& other = state.objects[static_cast<std::size_t>(otherObject)];
    AttachmentRecord& orec = other.attachments[static_cast<std::size_t>(otherPoint)];
    b2RevoluteJointDef jd;
    jd.bodyA = world.body(obj.bodies[static_cast<std::size_t>(rec.point.body)]);
    jd.bodyB = world.body(other.bodies[static_cast<std::size_t>(orec.point.body)]);
    jd.collideConnected = false;
    jd.localAnchorA = Vec2(obj.scale.x * rec.point.pos.x, obj.scale.y * rec.point.pos.y);
    jd.localAnchorB = Vec2(other.scale.x * orec.point.pos.x, other.scale.y * orec.point.pos.y);
    jd.referenceAngle = 0.0f;
    jd.enableLimit = false;
    jd.enableMotor = true;
    jd.motorSpeed = 0.0f;
    jd.maxMotorTorque = 0.01f;
    const int slot = world.createRevolute(jd);
    rec.joint = slot;
    orec.joint = slot;
    attachmentChanged(state, world, object, point);
    attachmentChanged(state, world, otherObject, otherPoint);
}

void createAttachments(WorldState& state, PhysicsWorld& world) {
    for (std::size_t i = 0; i < state.objects.size(); ++i) {
        for (int k = 0; k < state.objects[i].attachmentCount; ++k) {
            const AttachmentRecord& rec = state.objects[i].attachments[static_cast<std::size_t>(k)];
            if (rec.state == attachment_state::kAttached && rec.joint < 0) createAttachmentJoint(state, world, static_cast<int>(i), k);
        }
    }
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Rope) continue;
        items::updateRopeFromAttachedObjects(state, world, item, state.objects[static_cast<std::size_t>(item.objectIndex)]);
    }
}

void attachmentChanged(WorldState& state, PhysicsWorld& world, int object, int point) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    if (obj.type != ItemType::Rope) return;
    items::ropeAttachmentChanged(state, world, state.itemOf(obj), obj, point);
}

namespace {

// The snap query callback (PTR_LAB_00278a08): collects up to 32 distinct objects with attachment points
// other than the dragged one, skipping objects whose points are all attached; returns false (stop) when
// the list is full.
struct SnapCandidates {
    static constexpr int kMax = 32;
    const WorldState* state = nullptr;
    int self = -1;
    int count = 0;
    int objects[kMax] = {};

    bool report(const b2Fixture* fixture) {
        const int idx = PhysicsWorld::bodyObject(fixture->GetBody());
        if (idx < 0) return true;
        const PhysicsObject& obj = state->objects[static_cast<std::size_t>(idx)];
        if (obj.attachmentCount <= 0) return true;
        if (obj.index == self) return true;
        for (int i = 0; i < count; ++i) {
            if (objects[i] == idx) return true;
        }
        bool allAttached = true;
        for (int k = 0; k < obj.attachmentCount; ++k) {
            if (obj.attachments[static_cast<std::size_t>(k)].state != attachment_state::kAttached) {
                allAttached = false;
                break;
            }
        }
        if (allAttached) return true;
        objects[count++] = idx;
        return count != kMax;
    }
};

// FUN_000a7080: the first candidate point (object order, point order) of a compatible kind that is not
// attached, within `radius` of `target`, and either free or snapped to exactly (self, selfPoint). The
// callers pass the radius in s0, a literal that is not the AABB query radius: 0.08 m from CalculateSnap,
// 0.03 m from AttachToNearbyItems [verified: `vldr s0` before each `bl FUN_000a7080`; G5a oracle].
constexpr float kSnapPointRadius = 0.08f;
constexpr float kNearbyPointRadius = 0.03f;

bool findSnapTarget(const WorldState& state, const PhysicsWorld& world, int self, int selfPoint, const SnapCandidates& c,
                    Vec2 target, int mask, float radius, int& outObject, int& outPoint) {
    const float r2 = radius * radius;
    for (int i = 0; i < c.count; ++i) {
        const PhysicsObject& other = state.objects[static_cast<std::size_t>(c.objects[i])];
        for (int j = 0; j < other.attachmentCount; ++j) {
            const AttachmentRecord& rec = other.attachments[static_cast<std::size_t>(j)];
            if (rec.state == attachment_state::kAttached) continue;
            if ((mask & rec.point.kind) == 0) continue;
            const Vec2 p = attachmentPosWS(other, world, j);
            const float d2 = (p.y - target.y) * (p.y - target.y) + (p.x - target.x) * (p.x - target.x);
            if (d2 < r2 && (rec.state != attachment_state::kSnapped || (rec.otherObject == self && rec.otherPoint == selfPoint))) {
                outObject = other.index;
                outPoint = j;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

SnapResult calculateSnap(const WorldState& state, const PhysicsWorld& world, const PhysicsObject& obj, Vec2 position,
                         Vec2 queryCenter, float radius) {
    SnapResult result;
    result.position = position;
    if (obj.attachmentCount == 0) return result;
    SnapCandidates candidates;
    candidates.state = &state;
    candidates.self = obj.index;
    b2AABB aabb;
    aabb.lowerBound = Vec2(queryCenter.x - radius, queryCenter.y - radius);
    aabb.upperBound = Vec2(radius + queryCenter.x, radius + queryCenter.y);
    world.queryAABB(aabb, [&](b2Fixture* f) { return candidates.report(f); });
    if (candidates.count == 0) return result;
    int freePoints[PhysicsObject::kMaxAttachments];
    int freeCount = 0;
    for (int k = 0; k < obj.attachmentCount; ++k) {
        if (obj.attachments[static_cast<std::size_t>(k)].state < attachment_state::kAttached) freePoints[freeCount++] = k;
    }
    const b2Body* body0 = world.body(obj.bodies[0]);
    for (int n = 0; n < freeCount; ++n) {
        const int k = freePoints[n];
        const Vec2 posWS = attachmentPosWS(obj, world, k);
        const Vec2 d(posWS.x - obj.position.x, posWS.y - obj.position.y);
        const Vec2 r = rotate(-(body0->GetAngle() - obj.angle), d);
        const Vec2 target(position.x + r.x, position.y + r.y);
        int otherObject = -1;
        int otherPoint = -1;
        const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
        if (!findSnapTarget(state, world, obj.index, k, candidates, target, rec.point.mask, kSnapPointRadius, otherObject, otherPoint)) continue;
        const PhysicsObject& other = state.objects[static_cast<std::size_t>(otherObject)];
        if (other.isGhost()) continue;
        const Vec2 otherPos = attachmentPosWS(other, world, otherPoint);
        if (rec.point.positionOnly) {
            result.found = true;
            result.point = k;
            result.otherObject = otherObject;
            result.otherPoint = otherPoint;
            result.position = Vec2(otherPos.x - d.x, otherPos.y - d.y);
            return result;
        }
        const Vec2 dir = normalize(rotate(obj.angle, rec.point.dir));
        const AttachmentPoint& op = other.attachments[static_cast<std::size_t>(otherPoint)].point;
        const Vec2 otherDir = normalize(rotate(other.angle, Vec2(-op.dir.x, -op.dir.y)));
        const float sign = (dir.x * otherDir.y - dir.y * otherDir.x) >= 0.0f ? 1.0f : -1.0f;
        const float a = acosF(dir.y * otherDir.y + dir.x * otherDir.x) * sign;
        if (std::fabs(a) < kGamePi * 0.25f) {   // the global Pi × 0.25 (0.785398)
            result.angle = a;
            // The original rotates the point's *local* position by the new angle (0x974d4 passes the
            // record base to Rotate), not the world-space offset `d` — the two agree only at angle 0.
            const Vec2 local(obj.scale.x * rec.point.pos.x, obj.scale.y * rec.point.pos.y);
            const Vec2 rd = rotate(a + obj.angle, local);
            result.found = true;
            result.point = k;
            result.otherObject = otherObject;
            result.otherPoint = otherPoint;
            result.position = Vec2(otherPos.x - rd.x, otherPos.y - rd.y);
            return result;
        }
    }
    return result;
}

void snap(WorldState& state, int object, int point, int otherObject, int otherPoint) {
    AttachmentRecord& a = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(point)];
    AttachmentRecord& b = state.objects[static_cast<std::size_t>(otherObject)].attachments[static_cast<std::size_t>(otherPoint)];
    if (a.state == attachment_state::kSnapped || b.state == attachment_state::kSnapped) return;
    a.state = attachment_state::kSnapped;
    a.otherObject = otherObject;
    a.otherPoint = otherPoint;
    b.state = attachment_state::kSnapped;
    b.otherObject = object;
    b.otherPoint = point;
}

void unsnap(WorldState& state, int object, int point) {
    AttachmentRecord& a = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(point)];
    if (a.state == attachment_state::kFree) return;
    const int otherObject = a.otherObject;
    const int otherPoint = a.otherPoint;
    a.otherObject = -1;
    a.state = attachment_state::kFree;
    a.otherPoint = -1;
    AttachmentRecord& b = state.objects[static_cast<std::size_t>(otherObject)].attachments[static_cast<std::size_t>(otherPoint)];
    b.state = attachment_state::kFree;
    b.otherObject = -1;
    b.otherPoint = -1;
}

void unsnapAllNotAttached(WorldState& state, int object) {
    const int n = state.objects[static_cast<std::size_t>(object)].attachmentCount;
    for (int k = 0; k < n; ++k) {
        if (state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)].state != attachment_state::kAttached) {
            unsnap(state, object, k);
        }
    }
}

void attach(WorldState& state, PhysicsWorld& world, int object, int point, int otherObject, int otherPoint) {
    unsnap(state, object, point);
    AttachmentRecord& a = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(point)];
    AttachmentRecord& b = state.objects[static_cast<std::size_t>(otherObject)].attachments[static_cast<std::size_t>(otherPoint)];
    a.otherObject = otherObject;
    a.otherPoint = otherPoint;
    a.state = attachment_state::kAttached;
    b.otherObject = object;
    b.otherPoint = point;
    b.state = attachment_state::kAttached;
    createAttachmentJoint(state, world, object, point);
}

void detach(WorldState& state, PhysicsWorld& world, int object, int point) {
    AttachmentRecord& a = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(point)];
    const int otherObject = a.otherObject;
    const int otherPoint = a.otherPoint;
    if (a.joint >= 0) world.destroyJoint(a.joint);
    a.joint = -1;
    a.state = attachment_state::kFree;
    a.otherObject = -1;
    a.otherPoint = -1;
    AttachmentRecord& b = state.objects[static_cast<std::size_t>(otherObject)].attachments[static_cast<std::size_t>(otherPoint)];
    b.joint = -1;
    b.state = attachment_state::kFree;
    b.otherObject = -1;
    b.otherPoint = -1;
    attachmentChanged(state, world, object, point);
    attachmentChanged(state, world, otherObject, otherPoint);
}

void removeAllAttachments(WorldState& state, PhysicsWorld& world, int object) {
    const int n = state.objects[static_cast<std::size_t>(object)].attachmentCount;
    for (int k = 0; k < n; ++k) {
        const int s = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)].state;
        if (s == attachment_state::kAttached) detach(state, world, object, k);
        else if (s == attachment_state::kSnapped) unsnap(state, object, k);
    }
}

void attachToNearbyItems(WorldState& state, PhysicsWorld& world, int object) {
    constexpr float kRadius = 0.07f;   // the AABB half-size; the point test uses kNearbyPointRadius
    const int n = state.objects[static_cast<std::size_t>(object)].attachmentCount;
    for (int k = 0; k < n; ++k) {
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
        const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
        if (rec.state == attachment_state::kAttached || !rec.point.positionOnly) continue;
        unsnap(state, object, k);
        const Vec2 p = attachmentPosWS(obj, world, k);
        SnapCandidates candidates;
        candidates.state = &state;
        candidates.self = obj.index;
        b2AABB aabb;
        aabb.lowerBound = Vec2(p.x - kRadius, p.y - kRadius);
        aabb.upperBound = Vec2(p.x + kRadius, p.y + kRadius);
        world.queryAABB(aabb, [&](b2Fixture* f) { return candidates.report(f); });
        if (candidates.count == 0) continue;
        int otherObject = -1;
        int otherPoint = -1;
        if (findSnapTarget(state, world, obj.index, k, candidates, p, rec.point.mask, kNearbyPointRadius, otherObject, otherPoint)) {
            attach(state, world, object, k, otherObject, otherPoint);
            return;
        }
    }
}

void playAttachmentSounds(const PhysicsObject& now, const PhysicsObject& before, ActionQueue& queue) {
    auto queueSound = [&](bool detached, int kind) {
        // FUN_000a6f2c: kinds 1, 2 and 4 share the hook sounds; kind 8 the pipe sounds; nothing else.
        int id = -1;
        if (kind == 1 || kind == 2 || kind == 4) id = detached ? sound::kDetachHook : sound::kAttachHook;
        else if (kind == 8) id = detached ? sound::kDetachPipe : sound::kAttachPipe;
        if (id >= 0) queue.add(Action::sound(id, now.position, 0.2f));
    };
    for (int k = 0; k < now.attachmentCount; ++k) {
        const int s = now.attachments[static_cast<std::size_t>(k)].state;
        const int b = before.attachments[static_cast<std::size_t>(k)].state;
        if (s == attachment_state::kFree) {
            if (b != attachment_state::kFree) queueSound(true, now.attachments[static_cast<std::size_t>(k)].point.kind);
        } else if (b == attachment_state::kFree) {
            queueSound(false, now.attachments[static_cast<std::size_t>(k)].point.kind);
        }
    }
}

void invalidateItem(WorldState& state, PhysicsWorld& world, int itemIndex) {
    const int object = state.items[static_cast<std::size_t>(itemIndex)].objectIndex;
    removeAllAttachments(state, world, object);
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    world.destroyPhysics(obj);
    obj.flags = static_cast<std::uint8_t>(obj.flags & ~object_flags::kBase);
}

}  // namespace aa::sim
