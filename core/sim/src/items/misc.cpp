// One-body items with fixtures only: Bumper (33), PaperPlane (27), Pulley (21), Magnet (25),
// Helicopter (39) — st::<Item>Utils::CreatePhysics. Every expression keeps the original's shape and
// float/double mix (checked against the disassembly: PaperPlane's −0.3 factor, the magnet's pole offsets
// and the helicopter's rotor box are double multiplications).
#include "aa/sim/float_bits.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

#include <cmath>

namespace aa::sim::items {

namespace {
constexpr float kBumperOnTime = 0.18f;      // 0x3e3851ec
constexpr float kBumperForce = 300.0f;
constexpr float kLn2 = 0.6931472f;          // 0x3f317218
constexpr float kHeliHitForce = 80.0f;
constexpr int kHeliTailFixture = 2;         // Helicopter+0x30: the third fixture created
constexpr int kHeliRotorFixture = 4;        // Helicopter+0x2c: the fifth
constexpr float kNoseDownFactor = -0.2f;    // a nose-down plane (cos < 0) keeps a fifth of the lift, reversed
constexpr float kLiftY = 10.0f;
constexpr double kLiftX = 5.0;
constexpr float kDartMagneticFraction = 0.3f;
constexpr float kFarAway = 10000.0f;              // 0x461c4000
constexpr float kEpsilon = 0.0001f;               // st::Epsilon
constexpr float kMagnetRange2 = 1.0f;
constexpr float kMagnetConeCos = 0.17364845f;     // DAT_0028ee20 (0x3e31d0e6): sin 10°
constexpr float kMagnetForce = 300.0f;
constexpr float kMagnetMassUnit = 0.1f;
constexpr int kMagnetLastFrame = 5;
constexpr float kMagnetFrameTime = 0.033333335f;  // 0x3d088889
constexpr float kRotorSpinDown = 10.0f;
constexpr float kTailSpinDown = 20.0f;
constexpr float kRotorSpinUp = 10.0f;
constexpr float kTailSpinUp = 60.0f;
constexpr float kRotorMax = 15.0f;
constexpr float kTailMax = 100.0f;
constexpr float kRisingVelocity = 0.04f;
const float kLiftTorqueVelocity = floatFromBits(0x3ce56041u);   // one ulp below 0.028f
constexpr float kThrottleRamp = 10.0f;
constexpr float kThrottleMin = 50.0f;
constexpr float kThrottleMax = 100.0f;
constexpr float kTiltHover = 0.87266445f;         // DAT_0028e408 (0x3f5f66f0): 50°
constexpr float kTiltLevel = -0.17453289f;        // DAT_0028e40c: −10°
constexpr float kTiltMin = -1.0471973f;           // DAT_0028e410: −60°
constexpr float kTiltMax = 1.0471973f;            // DAT_0028e414: 60°
constexpr float kTiltGain = 10.0f;
constexpr float kTiltTorque = 100.0f;
constexpr float kLiftTorque = 0.8f;
}  // namespace

void createBumper(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const int body = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    const float factor = mode == PhysicsMode::SetUp ? 1.1f : 0.95f;
    world.addCircle(body, obj.halfSize * factor, Vec2(0.0f, 0.0f), fixtureDef(50.0f, 0.2f, 0.4f, filters::dynamicPlus()));
}

void updateBumpers(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // BumperUtils::Update [verified: decompile + disassembly].
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Bumper) continue;
        if (item.bumperOn == 1) {
            item.bumperTimer = item.bumperTimer - dt;
            if (!(item.bumperTimer > 0.0f)) {
                item.bumperTimer = 0.0f;
                item.bumperOn = 0;
            }
            continue;
        }
        const PhysicsObject& bumper = state.objects[static_cast<std::size_t>(item.objectIndex)];
        const b2Body* body = world.body(bumper.bodies[0]);
        for (const b2ContactEdge* ce = body->GetContactList(); ce; ce = ce->next) {
            b2Contact* contact = ce->contact;
            if (!contact->IsTouching() || contact->GetFixtureA()->IsSensor() || contact->GetFixtureB()->IsSensor()) continue;
            const int oi = PhysicsWorld::bodyObject(ce->other);
            const PhysicsObject& other = state.objects[static_cast<std::size_t>(oi)];
            int otherBody = -1;
            for (int k = 0; k < other.bodyCount; ++k) {
                if (world.body(other.bodies[static_cast<std::size_t>(k)]) == ce->other) {
                    otherBody = k;
                    break;
                }
            }
            b2WorldManifold wm;
            contact->GetWorldManifold(&wm);
            bumperHandleCollision(item, bumper, other, otherBody, wm.normal, queue, world);
        }
    }
}

void createPaperPlane(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const int body = addBody(obj, world, itemBodyDef(obj, mode, b2_dynamicBody));
    const float a = r * 0.45f;
    const float tailY = dmul(a, -0.3);
    const float noseY = a * 0.9f;
    const float wing = r * -0.72f;
    const float wingFlipped = r * 0.72f;
    const float na = -a;
    // Two vertex lists in one table; the flipped plane starts four vertices later (a different winding).
    const Vec2 verts[8] = {Vec2(r, 0.0f), Vec2(-r, noseY), Vec2(wing, na), Vec2(r, tailY),
                           Vec2(-r, tailY), Vec2(wingFlipped, na), Vec2(r, noseY), Vec2(-r, 0.0f)};
    world.addPolygon(body, obj.flipped() ? verts + 4 : verts, 4, fixtureDef(0.5f, 0.8f, 0.0f, filters::kDynamic));
    if (mode == PhysicsMode::SetUp) {
        world.addBox(body, r, r * 0.3f, fixtureDef(0.0f, 0.8f, 0.0f, filters::kSelection));
    }
}

void updatePaperPlanes(float dt, WorldState& state, PhysicsWorld& world) {
    // PaperPlaneUtils::Update [verified: decompile + disassembly]. The x force is a double product chain
    // (vmul.f64 / vmla.f64, unfused) started from cos(angle) and the flip sign (scale.x); the y force is
    // single precision.
    const double zero = 0.0;
    for (const GameItem& item : state.items) {
        if (item.type != ItemType::PaperPlane) continue;
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        b2Body* body = world.body(obj.bodies[0]);
        const float sx = obj.scale.x;
        const b2Vec2 v = body->GetLinearVelocity();
        float c = cosF(obj.angle);
        if (c < 0.0f) c = c * kNoseDownFactor;
        const float ky = v.y < 0.0f ? kLiftY : -kLiftY;
        const double kx = v.y < 0.0f ? kLiftX : -kLiftX;
        if (body->GetType() != b2_dynamicBody) continue;
        double d = static_cast<double>(c) * kx;
        d = d * static_cast<double>(v.y);
        const float scaled = dt * sx;
        d = static_cast<double>(v.y) * d;
        double lift = zero;
        lift = lift + static_cast<double>(scaled) * d;
        const float fx = static_cast<float>(lift);
        const float cy = c * ky;
        const float vx2 = v.x * v.x;
        const float vyc = v.y * cy;
        float sum = vx2 + vx2;
        sum = sum + v.y * vyc;
        float fy = 0.0f;
        fy = fy + sum * dt;
        body->ApplyForce(b2Vec2(fx, fy), body->GetWorldCenter());
        body->ApplyTorque(-dt * body->GetAngularVelocity());
    }
}

void createPulley(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const int body = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    b2Filter filter = filters::kStatic;
    filter.categoryBits = static_cast<uint16>(filter.categoryBits | filters::kSelectionBit);
    filter.maskBits = static_cast<uint16>(filter.maskBits | filters::kRopeBit);
    world.addCircle(body, obj.halfSize, Vec2(0.0f, 0.0f), fixtureDef(5.0f, 0.4f, 0.5f, filter));
}

void createMagnet(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const int body = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    const double sx = static_cast<double>(obj.scale.x);
    const b2FixtureDef fd = fixtureDef(0.0f, 0.3f, 0.0f, filters::kStatic);
    world.addCircle(body, r * 0.85f, Vec2(static_cast<float>(sx * -0.015), 0.0f), fd);
    const double poleX = sx * 0.8;
    world.addBoxAt(body, 0.02f, 0.044f, Vec2(dmul(poleX, r), dmul(r, 0.43)), 0.0f, fd);
    world.addBoxAt(body, 0.02f, 0.044f, Vec2(dmul(poleX, r), dmul(r, -0.45)), 0.0f, fd);
    if (mode == PhysicsMode::SetUp) {
        world.addCircle(body, r * 1.3f, Vec2(0.0f, 0.0f), fixtureDef(0.0f, 0.3f, 0.0f, filters::kSelection));
    }
}

Vec2 magneticCenter(const PhysicsObject& obj, const PhysicsWorld& world) {
    // GameItemUtils::GetMagneticCenter / PaperPlaneUtils / DartUtils [verified].
    if (obj.type == ItemType::PaperPlane || obj.type == ItemType::Dart) {
        const Vec2 axis = rotate(obj.angle, Vec2(obj.scale.x, 0.0f));
        const float d = obj.type == ItemType::Dart ? obj.halfSize * kDartMagneticFraction : obj.halfSize;
        return Vec2(obj.position.x + d * axis.x, obj.position.y + d * axis.y);
    }
    return world.body(obj.bodies[0])->GetWorldCenter();
}

void updateMagnets(float dt, WorldState& state, PhysicsWorld& world) {
    // MagnetUtils::Update [verified: decompile + disassembly].
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Magnet) continue;
        const PhysicsObject& magnet = state.objects[static_cast<std::size_t>(item.objectIndex)];
        const float r = magnet.halfSize;
        const Vec2 dir = rotate(magnet.angle, Vec2(magnet.scale.x, 0.0f));
        item.magnetPulling = false;
        item.magnetMinDist2 = kFarAway;
        for (std::size_t j = 0; j < state.objects.size(); ++j) {
            const PhysicsObject& obj = state.objects[j];
            if ((obj.flags & object_flags::kMagnetic) == 0) continue;
            float poleY = magnet.position.y;
            poleY = poleY + r * dir.y;
            float poleX = magnet.position.x;
            poleX = poleX + dir.x * r;
            const float dy = obj.position.y - poleY;
            const float dx = obj.position.x - poleX;
            float d2 = dy * dy;
            d2 = d2 + dx * dx;
            if (d2 < kEpsilon || !(d2 <= kMagnetRange2)) continue;
            const float dist = std::sqrt(d2);
            const float ny = dy / dist;
            const float nx = dx / dist;
            float dot = ny * dir.y;
            dot = dot + dir.x * nx;
            if (dot < kMagnetConeCos) continue;
            b2Body* body = world.body(obj.bodies[0]);
            const float mass = body->GetMass();
            const Vec2 center = magneticCenter(obj, world);
            if (body->GetType() == b2_dynamicBody) {
                const float m = mass / kMagnetMassUnit;
                float pull = kMagnetForce;
                pull = pull - d2 * kMagnetForce;
                pull = -(pull * m);
                const float f = dt * pull;
                body->ApplyForce(b2Vec2(nx * f, ny * f), center);
            }
            item.magnetPulling = true;
            if (!(d2 - item.magnetMinDist2 >= 0.0f)) item.magnetMinDist2 = d2;
        }
        if (item.magnetPulling) {
            item.magnetTimer = item.magnetTimer - dt;
            if (!(item.magnetTimer > 0.0f)) {
                item.magnetFrame = item.magnetFrame + 1;
                if (item.magnetFrame > kMagnetLastFrame) item.magnetFrame = 0;
                item.magnetTimer = kMagnetFrameTime;
            }
        }
    }
}

void createHelicopter(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float h = r * 0.25f;
    const float s = obj.flipSign();
    b2BodyDef def;
    def.type = b2_dynamicBody;   // dynamic in both modes
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);
    world.addBoxAt(body, r * 0.65f, h, Vec2(r * -0.4f * s, 0.0f), 0.0f, fixtureDef(150.0f, 0.6f, 0.4f, filters::kDynamic));
    world.addBoxAt(body, r * 0.32f, h * 0.3f, Vec2(r * 0.57f * s, h * 0.28f), 0.0f,
                   fixtureDef(5.0f, 0.6f, 0.4f, filters::kDynamic));
    const float h04 = h * 0.4f;
    world.addBoxAt(body, r * 0.12f, h * 1.2f, Vec2(r * s, h04), 0.0f, fixtureDef(5.0f, 0.6f, 0.4f, filters::kDynamic));
    world.addBoxAt(body, r * 0.53f, h04, Vec2(r * -0.43f * s, h * -1.4f), 0.0f,
                   fixtureDef(0.0f, 1.0f, 0.4f, filters::kDynamic));
    const double hd = static_cast<double>(h);
    const float rotorX = static_cast<float>(static_cast<double>(r) * -0.36 * static_cast<double>(s));
    world.addBoxAt(body, r * 1.1f, static_cast<float>(hd * 0.2), Vec2(rotorX, h * 1.9f), 0.0f,
                   fixtureDef(0.0f, 0.1f, 0.4f, filters::kDynamic));
    if (mode == PhysicsMode::SetUp) {
        world.addBox(body, r * 1.2f, h + h, fixtureDef(0.0f, 0.2f, 0.0f, filters::kSelection));
    }
    if (item.builtInController) {
        // Legacy layouts (version <= 6) carried the helicopter's controller inside the helicopter item.
        b2BodyDef cdef;
        cdef.type = b2_dynamicBody;
        cdef.position = Vec2(obj.position.x + -0.5f, static_cast<float>(hd * -0.8) + obj.position.y);
        const int controller = addBody(obj, world, cdef);
        const b2FixtureDef cfd = fixtureDef(0.0f, 0.7f, 0.4f, filters::kDynamic);
        world.addBox(controller, 0.1f, 0.04f, cfd);
        world.addBox(controller, 0.05f, 0.038f, cfd);
    }
}

void bumperHandleCollision(GameItem& item, const PhysicsObject& bumper, const PhysicsObject& other, int otherBody,
                           Vec2 normal, ActionQueue& queue, PhysicsWorld& world) {
    // BumperUtils::HandleCollision [verified: decompile + disassembly].
    if (item.bumperOn == 1) return;
    item.bumperOn = 1;
    item.bumperTimer = kBumperOnTime;
    queue.add(Action::sound(sound::kBumperImpact, bumper.position, 1.0f));
    const float f = (logF(objectMass(other, world) + 1.0f) / kLn2) * kBumperForce;
    const b2Body* body = world.body(other.bodies[static_cast<std::size_t>(otherBody)]);
    Action a(action::kForceToItem, other.handle, body->GetPosition());
    a.setPayloadFloat(normal.x * f);
    a.value = normal.y * f;
    a.extra = otherBody;
    queue.add(a);
}

void helicopterHandleCollision(const GameItem& item, const PhysicsObject& heli, const b2Fixture* fixture, const PhysicsObject& other,
                               int otherBody, Vec2 point, Vec2 normal, ActionQueue& queue, PhysicsWorld& world) {
    // HelicopterUtils::HandleCollision [verified]: the rotor (fixture 4) or the tail (fixture 2) of a
    // running helicopter; 80 N split by mass (a massless other counts 100 kg).
    if (!item.heliOn) return;
    const int body = heli.bodies[0];
    if (fixture != world.fixtureAt(body, kHeliRotorFixture) && fixture != world.fixtureAt(body, kHeliTailFixture)) return;
    float mOther = objectMass(other, world);
    if (mOther <= 0.0f) mOther = 100.0f;
    const float mHeli = objectMass(heli, world);
    const float fOther = (mHeli * kHeliHitForce) / (mOther + mHeli);
    const float fHeli = (mOther * kHeliHitForce) / (mOther + mHeli);
    Action a(action::kForceToItem, other.handle, point);
    a.setPayloadFloat(normal.x * fOther);
    a.value = normal.y * fOther;
    a.extra = otherBody;
    queue.add(a);
    const float back = -fHeli;
    Action b(action::kForceToItem, heli.handle, point);
    b.setPayloadFloat(normal.x * back);
    b.value = normal.y * back;
    b.extra = 0;
    queue.add(b);
    queue.add(Action::soundOf(heli.handle, sound::kHelicopterHit, heli.position, 0.1f));
}

void updateHelicopters(float dt, WorldState& state, PhysicsWorld& world) {
    // HelicopterUtils::Update [verified: decompile + disassembly]. Every torque is added to m_torque in
    // the original's order (ApplyTorque = the same rounded product + add).
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Helicopter) continue;
        if (!item.heliOn) {
            float rotor = item.rotorSpeed - dt * kRotorSpinDown;
            float tail = item.tailSpeed - dt * kTailSpinDown;
            if (rotor < 0.0f) rotor = 0.0f;
            if (tail < 0.0f) tail = 0.0f;
            item.rotorSpeed = rotor;
            item.tailSpeed = tail;
            item.rotorPhase = item.rotorPhase + dt * rotor;
            item.tailPhase = item.tailPhase + dt * tail;
            continue;
        }
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        const float s = obj.scale.x < 0.0f ? -1.0f : 1.0f;
        b2Body* body = world.body(obj.bodies[0]);
        const float twoPi = kPi + kPi;
        float a = s * body->GetAngle();
        while (kPi * -2.0f > a) a = a + twoPi;
        while (a > twoPi) a = a - twoPi;
        const b2Vec2 v = body->GetLinearVelocity();
        float tilt;
        if (v.y < kRisingVelocity) {
            float throttle = item.heliThrottle;
            throttle = throttle + dt * kThrottleRamp;
            if (kThrottleMax - throttle < 0.0f) throttle = kThrottleMax;
            if (throttle - kThrottleMin < 0.0f) throttle = kThrottleMin;
            item.heliThrottle = throttle;
            tilt = kTiltHover * 1.1f;
        } else {
            tilt = kDegToRad * 10.0f;
        }
        const float sw = s * body->GetAngularVelocity();
        const bool dynamic = body->GetType() == b2_dynamicBody;
        if (!(kTiltLevel <= a)) {
            if (!(kTiltMin > a)) {
                float factor = 1.0f;
                if (!(sw > 0.0f)) factor = (1.0f - sw) * kTiltGain;
                if (dynamic) {
                    const float ds = dt * s;
                    const float k = ds * kTiltTorque;
                    const float t = k * (kTiltLevel - a);
                    body->ApplyTorque(factor * t);
                }
            }
        } else if (!(tilt >= a) && !(kTiltMax < a)) {
            float factor = 1.0f;
            if (sw >= 0.0f) factor = (sw + 1.0f) * kTiltGain;
            if (dynamic) {
                const float ds = -(s * dt);
                const float k = ds * kTiltTorque;
                const float t = k * (a - tilt);
                body->ApplyTorque(factor * t);
            }
        }
        if (v.y >= kLiftTorqueVelocity && !(kTiltHover < a) && dynamic) {
            body->ApplyTorque((dt * s) * kLiftTorque);
        }
        const Vec2 up = rotate(s * a, Vec2(0.0f, 1.0f));
        if (dynamic) body->ApplyForce(b2Vec2(item.heliThrottle * up.x, item.heliThrottle * up.y), body->GetWorldCenter());
        float rotor = item.rotorSpeed + dt * kRotorSpinUp;
        float tail = item.tailSpeed + dt * kTailSpinUp;
        if (rotor - kRotorMax >= 0.0f) rotor = kRotorMax;
        if (tail - kTailMax >= 0.0f) tail = kTailMax;
        item.rotorSpeed = rotor;
        item.tailSpeed = tail;
        item.rotorPhase = item.rotorPhase + dt * rotor;
        item.tailPhase = item.tailPhase + dt * tail;
    }
}

void helicopterTurnOn(GameItem& item, const PhysicsObject& obj, PhysicsWorld& world) {
    // HelicopterUtils::TurnOn: the rotor and the tail become sharp (Dynamic, group −2) [verified].
    item.heliOn = true;
    const b2Filter sharp = filters::withGroup(filters::kDynamic, filters::kGroupSharp);
    if (b2Fixture* f = world.fixtureAt(obj.bodies[0], kHeliRotorFixture)) f->SetFilterData(sharp);
    if (b2Fixture* f = world.fixtureAt(obj.bodies[0], kHeliTailFixture)) f->SetFilterData(sharp);
}

void helicopterTurnOff(GameItem& item, const PhysicsObject& obj, PhysicsWorld& world) {
    // HelicopterUtils::TurnOff: only the rotor goes back to Dynamic [verified].
    item.heliOn = false;
    if (b2Fixture* f = world.fixtureAt(obj.bodies[0], kHeliRotorFixture)) f->SetFilterData(filters::kDynamic);
}

}  // namespace aa::sim::items
