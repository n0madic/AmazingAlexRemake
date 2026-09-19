// st::WorldContactListener::BeginContact / PreSolve / PostSolve [verified: decompile + disassembly].
#include "contact_listener.h"

#include "aa/sim/filters.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"

#include <cmath>

namespace aa::sim {

namespace {

constexpr float kSoundImpactThreshold = 0.5f;
constexpr float kPiggyBreakImpulse = 3.5f;

// PhysicsObjectUtils::GetBodyIndex: the index of `body` in the object's body list, -1 when absent.
int bodyIndexOf(const PhysicsObject& obj, const PhysicsWorld& world, const b2Body* body) {
    for (int k = 0; k < obj.bodyCount; ++k) {
        if (world.body(obj.bodies[static_cast<std::size_t>(k)]) == body) return k;
    }
    return -1;
}

// FUN_000fb998: the mass-weighted relative velocity of the two bodies at the contact point along the
// normal — queued as action 11 whenever its magnitude is ≥ 0 (every real contact; only a NaN fails).
void queueGoalImpact(const b2Body* target, const b2Body* other, const b2WorldManifold& wm, ActionQueue& queue) {
    const b2Vec2 p = wm.points[0];
    const b2Vec2 cA = target->GetWorldCenter();
    const b2Vec2 cB = other->GetWorldCenter();
    const b2Vec2 vA = target->GetLinearVelocity();
    const b2Vec2 vB = other->GetLinearVelocity();
    const float wA = target->GetAngularVelocity();
    const float wB = other->GetAngularVelocity();
    const float mA = target->GetMass();
    const float mB = other->GetMass();
    const float vy = mB * (vB.y + wB * (p.x - cB.x)) - mA * (vA.y + wA * (p.x - cA.x));
    const float vx = mB * (vB.x - wB * (p.y - cB.y)) - mA * (vA.x - wA * (p.y - cA.y));
    const float along = vy * wm.normal.y + vx * wm.normal.x;
    if (std::fabs(along) >= 0.0f) queue.add(Action(action::kGoalComplete, 0));
}

class WorldContactListener : public b2ContactListener {
public:
    explicit WorldContactListener(SimulationContext& ctx) : ctx_(ctx) {}

    void BeginContact(b2Contact* contact) override {
        // A fixture of a sharp group (−2 scissors blades / helicopter rotor, −8 dart tip) pops the balloon
        // it touches.
        b2Fixture* fa = contact->GetFixtureA();
        b2Fixture* fb = contact->GetFixtureB();
        const b2Body* other;
        const b2Body* sharp;
        if (isSharp(fa)) {
            other = fb->GetBody();
            sharp = fa->GetBody();
        } else if (isSharp(fb)) {
            other = fa->GetBody();
            sharp = fb->GetBody();
        } else {
            return;
        }
        const int oi = PhysicsWorld::bodyObject(other);
        if (oi < 0 || PhysicsWorld::bodyObject(sharp) < 0) return;
        PhysicsObject& obj = ctx_.state->objects[static_cast<std::size_t>(oi)];
        if (obj.type != ItemType::Balloon) return;
        items::popBalloon(ctx_.state->itemOf(obj), obj, *ctx_.queue);
    }

    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override {
        WorldState& state = *ctx_.state;
        PhysicsWorld& world = *ctx_.world;
        ActionQueue& queue = *ctx_.queue;
        b2WorldManifold wm;
        contact->GetWorldManifold(&wm);
        b2Fixture* fa = contact->GetFixtureA();
        b2Fixture* fb = contact->GetFixtureB();
        b2Body* ba = fa->GetBody();
        b2Body* bb = fb->GetBody();
        const int ia = PhysicsWorld::bodyObject(ba);
        const int ib = PhysicsWorld::bodyObject(bb);
        if (ia < 0 || ib < 0) return;
        PhysicsObject& oa = state.objects[static_cast<std::size_t>(ia)];
        PhysicsObject& ob = state.objects[static_cast<std::size_t>(ib)];
        if (ia == ib) {
            // A doll's own parts never collide; a spring's base (fixture user data 2) ignores its own bodies.
            if (oa.type == ItemType::Doll) {
                contact->SetEnabled(false);
                return;
            }
            if (oa.type == ItemType::Spring &&
                (PhysicsWorld::fixtureBodyIndex(fa) == 2 || PhysicsWorld::fixtureBodyIndex(fb) == 2)) {
                contact->SetEnabled(false);
                return;
            }
        }
        GameItem& itemA = state.itemOf(oa);
        GameItem& itemB = state.itemOf(ob);
        if ((oa.type == ItemType::Slingshot && !items::slingshotShouldCollide(itemA, ob.handle)) ||
            (ob.type == ItemType::Slingshot && !items::slingshotShouldCollide(itemB, oa.handle))) {
            contact->SetEnabled(false);
            return;
        }
        b2PointState state1[b2_maxManifoldPoints];
        b2PointState state2[b2_maxManifoldPoints];
        b2GetPointStates(state1, state2, oldManifold, contact->GetManifold());
        if (state2[0] != b2_addState) return;
        const int bodyA = bodyIndexOf(oa, world, ba);
        const int bodyB = bodyIndexOf(ob, world, bb);
        const Vec2 point = wm.points[0];
        // The relative velocity of the two bodies at the contact point and its component along the normal
        // (vmla: products rounded before the adds).
        const b2Vec2 cA = ba->GetWorldCenter();
        const b2Vec2 cB = bb->GetWorldCenter();
        const b2Vec2 vA = ba->GetLinearVelocity();
        const b2Vec2 vB = bb->GetLinearVelocity();
        const float wA = ba->GetAngularVelocity();
        const float wB = bb->GetAngularVelocity();
        const float relY = (vA.y + wA * (point.x - cA.x)) - (vB.y + wB * (point.x - cB.x));
        const float relX = (vA.x - wA * (point.y - cA.y)) - (vB.x - wB * (point.y - cB.y));
        const float impact = relY * wm.normal.y + relX * wm.normal.x;
        const Vec2 relVel(relX, relY);
        const Vec2 normal = wm.normal;
        if (fa->GetFilterData().groupIndex == filters::kGroupDartTip && (ob.flags & object_flags::kStabbable)) {
            items::dartHandleStabCollision(itemA, oa, ob, bodyA, bodyB, fa, point, relVel, queue);
        } else if (fb->GetFilterData().groupIndex == filters::kGroupDartTip && (oa.flags & object_flags::kStabbable)) {
            items::dartHandleStabCollision(itemB, ob, oa, bodyB, bodyA, fb, point, Vec2(-relX, -relY), queue);
        }
        if (oa.type == ItemType::Scissors) {
            items::scissorsHandleCollision(itemA, oa, bb, impact, queue);
        } else if (ob.type == ItemType::Scissors) {
            items::scissorsHandleCollision(itemB, ob, ba, impact, queue);
        }
        if (oa.type == ItemType::Bumper) {
            items::bumperHandleCollision(itemA, oa, ob, bodyB, normal, queue, world);
        } else if (ob.type == ItemType::Bumper) {
            items::bumperHandleCollision(itemB, ob, oa, bodyA, Vec2(-normal.x, -normal.y), queue, world);
        }
        if (oa.type == ItemType::Helicopter) {
            items::helicopterHandleCollision(itemA, oa, fa, ob, bodyB, point, normal, queue, world);
        } else if (ob.type == ItemType::Helicopter) {
            items::helicopterHandleCollision(itemB, ob, fb, oa, bodyA, point, normal, queue, world);
        }
        if (fa->GetFilterData().groupIndex == filters::kGroupGloveTrigger) {
            items::gloveHandleCollision(itemA, oa, oa, ob, bodyB, impact, queue, world);
        } else if (fb->GetFilterData().groupIndex == filters::kGroupGloveTrigger) {
            // The original passes object A twice here: the sound plays at the other object [verified].
            items::gloveHandleCollision(itemB, ob, oa, oa, bodyA, impact, queue, world);
        }
        if (std::fabs(impact) > kSoundImpactThreshold) {
            items::handleCollisionSounds(itemA, oa, bodyA, impact, queue, world, ctx_.timeSeed);
            items::handleCollisionSounds(itemB, ob, bodyB, impact, queue, world, ctx_.timeSeed);
        }
        // The contact goals (types 1 and 2, docs/02 §5) on the first target's contacts.
        if (ctx_.goalState->reached || ctx_.goal == nullptr) return;
        const Goal& goal = *ctx_.goal;
        const int target = goal.itemHandles[0];
        if (target == oa.handle && fa->GetFilterData().categoryBits != filters::kDebris.categoryBits) {
            if (goal.type == 1) {
                if (fb->GetFilterData().categoryBits == filters::kStatic.categoryBits) return;
            } else if (goal.type != 2 || goal.itemHandles2[0] != ob.handle) {
                return;
            }
            queueGoalImpact(ba, bb, wm, queue);
            return;
        }
        if (target != ob.handle || fb->GetFilterData().categoryBits == filters::kDebris.categoryBits) return;
        if (goal.type == 1) {
            if (fa->GetFilterData().categoryBits == filters::kStatic.categoryBits) return;
        } else if (goal.type != 2 || goal.itemHandles2[0] != oa.handle) {
            return;
        }
        queueGoalImpact(bb, ba, wm, queue);
    }

    void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override {
        // A breakable object (the piggy bank) breaks when the normal impulses of one contact sum to more
        // than 3.5 N·s (FUN_000fb848); both sides are tested, fixture A first.
        const int ia = PhysicsWorld::bodyObject(contact->GetFixtureA()->GetBody());
        const int ib = PhysicsWorld::bodyObject(contact->GetFixtureB()->GetBody());
        if (ia < 0 || ib < 0) return;
        breakIfHit(ctx_.state->objects[static_cast<std::size_t>(ia)], contact, impulse);
        breakIfHit(ctx_.state->objects[static_cast<std::size_t>(ib)], contact, impulse);
    }

private:
    static bool isSharp(const b2Fixture* f) {
        const int16 g = f->GetFilterData().groupIndex;
        return g == filters::kGroupSharp || g == filters::kGroupDartTip;
    }

    void breakIfHit(const PhysicsObject& obj, b2Contact* contact, const b2ContactImpulse* impulse) {
        if ((obj.flags & object_flags::kBreakable) == 0 || (obj.state & object_state::kActivated) != 0) return;
        const int count = contact->GetManifold()->pointCount;
        if (count <= 0) return;
        float sum = 0.0f;
        for (int i = 0; i < count; ++i) sum = sum + impulse->normalImpulses[i];
        if (sum > kPiggyBreakImpulse) {
            b2WorldManifold wm;
            contact->GetWorldManifold(&wm);
            Action a(action::kBreak, obj.handle);
            a.setPayloadFloat(sum * 0.0f);
            a.value = sum;
            ctx_.queue->add(a);
        }
    }

    SimulationContext& ctx_;
};

}  // namespace

std::unique_ptr<b2ContactListener> makeWorldContactListener(SimulationContext& context) {
    return std::unique_ptr<b2ContactListener>(new WorldContactListener(context));
}

}  // namespace aa::sim
