// PiggyBank (13): st::PiggyBankUtils::CreatePhysics — the body box and the feet box, both offset by
// ±0.02·s along x (double multiplications), then SetMassData(1, 0, 0.001). Identical in both modes.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kBreakDivisor = 9.0f;
constexpr float kBreakBase = 2.0f;
constexpr float kPowTime = 0.2f;
// The debris force table (x, y pairs) and the shard triangle of PiggyBankUtils::Break.
constexpr float kDebrisForces[6] = {-1.0f, 1.0f, 0.1f, 2.0f, 0.8f, 1.3f};
const Vec2 kShard[3] = {Vec2(0.0f, -0.075f), Vec2(0.1f, 0.075f), Vec2(-0.1f, 0.075f)};

b2BodyDef debrisBodyDef(const PhysicsObject& obj) {
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    return def;
}

b2FixtureDef debrisFixtureDef() {
    b2FixtureDef fd = fixtureDef(1.0f, 0.6f, 0.0f, filters::kDebris);
    fd.userData = reinterpret_cast<void*>(1);
    return fd;
}
}  // namespace

void createPiggyBank(PhysicsObject& obj, PhysicsWorld& world) {
    const float r = obj.halfSize;
    const double s = static_cast<double>(obj.flipSign());
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);
    const b2FixtureDef fd = fixtureDef(1.0f, 0.6f, 0.0f, filters::dynamicPlus());
    world.addBoxAt(body, r * 0.9f, r * 0.6f, Vec2(static_cast<float>(s * -0.02), 0.02f), 0.0f, fd);
    world.addBoxAt(body, r * 0.6f, r * 0.1f, Vec2(static_cast<float>(s * 0.02), -0.09f), 0.0f, fd);
    world.setMassData(body, 1.0f, Vec2(0.0f, 0.0f), 0.001f);
}

void updatePiggyBanks(float dt, WorldState& state) {
    // PiggyBankUtils::Update [verified].
    for (GameItem& item : state.items) {
        if (item.type != ItemType::PiggyBank) continue;
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if ((obj.state & object_state::kActivated) == 0) continue;
        if (item.piggyTimer <= kPowTime) item.piggyTimer = item.piggyTimer + dt;
    }
}

void breakItem(WorldState& state, PhysicsWorld& world, int itemIndex, Vec2 breakVector, ActionQueue& queue) {
    // GameItemUtils::Break: an unactivated object becomes activated; only the piggy bank does more.
    GameItem& item = state.items[static_cast<std::size_t>(itemIndex)];
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
    if (obj.state & object_state::kActivated) return;
    obj.state = static_cast<std::uint8_t>(obj.state | object_state::kActivated);
    if (obj.type != ItemType::PiggyBank) return;
    // PiggyBankUtils::Break [verified: decompile + disassembly]: the object stops following its body
    // (angle 0, dynamic flag cleared), the bank becomes non-collidable, a debris box and three shards are
    // pushed with the force table scaled by 2 + min(|breakVector| / 9, 2).
    obj.angle = 0.0f;
    obj.flags = static_cast<std::uint8_t>(obj.flags & ~object_flags::kDynamic);
    world.setCollisionFilter(obj, filters::kNonCollidable);
    const float r = obj.halfSize;
    const int base = addBody(obj, world, debrisBodyDef(obj));
    world.addBox(base, r * 0.5f, r * 0.2f, debrisFixtureDef());
    float f = length(breakVector) / kBreakDivisor;
    float clamped = f;
    if (kBreakBase - f < 0.0f) clamped = kBreakBase;
    float factor = kBreakBase;
    if (0.0f <= f) factor = clamped + kBreakBase;
    for (int i = 0; i < 3; ++i) {
        const int shard = addBody(obj, world, debrisBodyDef(obj));
        world.addPolygon(shard, kShard, 3, debrisFixtureDef());
        b2Body* body = world.body(shard);
        if (body->GetType() == b2_dynamicBody) {
            const float fx = kDebrisForces[i * 2] * factor;
            const float fy = kDebrisForces[i * 2 + 1] * factor;
            body->ApplyForce(b2Vec2(fx, fy), body->GetPosition());
        }
    }
    queue.add(Action::sound(sound::kPiggyBreak, obj.position, 0.5f));
}

}  // namespace aa::sim::items
