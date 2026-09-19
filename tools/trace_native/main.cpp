// Replays a scene file produced by tools/uc_trace.py on the vendored Box2D 2.2.1 and writes the
// trajectory in the same format, so tools/trace_compare.py can diff it against the emulated original.
// Scene/trajectory formats are documented in tools/uc_trace.py.
#include <Box2D/Box2D.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

float bits(const std::string& tok) {
    // Floats are stored as exact float32 bit patterns ("0x3c088889").
    unsigned long v = std::strtoul(tok.c_str(), nullptr, 16);
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

struct Tokens {
    std::vector<std::string> t;
    size_t i = 0;
    float f() { return bits(t.at(i++)); }
    int n() { return std::atoi(t.at(i++).c_str()); }
    bool b() { return n() != 0; }
    b2Vec2 v() { float x = f(); float y = f(); return b2Vec2(x, y); }
};

struct Replayer {
    b2World* world = nullptr;
    std::vector<b2Body*> bodies;    // null once destroyed
    std::vector<std::vector<b2Fixture*>> fixtures;   // per body, creation order
    std::vector<b2Joint*> joints;   // creation order, null once destroyed
    int steps = 0;
    float dt = 0.0f;
    int velIters = 0, posIters = 0;

    b2Body* body(int idx) { return bodies.at(static_cast<size_t>(idx)); }

    void fixture(int bodyIdx, b2Shape& shape, Tokens& tk) {
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = tk.f();
        fd.friction = tk.f();
        fd.restitution = tk.f();
        fd.isSensor = tk.b();
        fd.filter.categoryBits = static_cast<uint16>(tk.n());
        fd.filter.maskBits = static_cast<uint16>(tk.n());
        fd.filter.groupIndex = static_cast<int16>(tk.n());
        fixtures.at(static_cast<size_t>(bodyIdx)).push_back(body(bodyIdx)->CreateFixture(&fd));
    }

    void jointHead(Tokens& tk, b2JointDef& jd, b2Vec2& anchorA, b2Vec2& anchorB) {
        jd.bodyA = body(tk.n());
        jd.bodyB = body(tk.n());
        jd.collideConnected = tk.b();
        anchorA = tk.v();
        anchorB = tk.v();
    }

    void line(const std::string& text) {
        if (text.rfind("# item ", 0) == 0) itemLines.push_back(text);
        if (text.empty() || text[0] == '#') return;
        Tokens tk;
        std::istringstream in(text);
        for (std::string s; in >> s;) tk.t.push_back(s);
        const std::string op = tk.t.at(tk.i++);
        if (op == "gravity") {
            b2Vec2 g = tk.v();
            world = new b2World(g);
            world->SetAllowSleeping(true);      // GamePhysicsUtils::CreateWorld: b2World(gravity, doSleep = true)
            world->SetAutoClearForces(false);   // and m_flags &= ~e_clearForces
        } else if (op == "body") {
            int idx = tk.n();
            b2BodyDef bd;
            bd.type = static_cast<b2BodyType>(tk.n());
            bd.position = tk.v();
            bd.angle = tk.f();
            bd.linearVelocity = tk.v();
            bd.angularVelocity = tk.f();
            bd.linearDamping = tk.f();
            bd.angularDamping = tk.f();
            bd.allowSleep = tk.b();
            bd.awake = tk.b();
            bd.fixedRotation = tk.b();
            bd.bullet = tk.b();
            bd.active = tk.b();
            if (static_cast<size_t>(idx) != bodies.size()) throw std::runtime_error("body index out of order");
            bodies.push_back(world->CreateBody(&bd));
            fixtures.emplace_back();
        } else if (op == "circle") {
            int b = tk.n();
            b2CircleShape c;
            c.m_radius = tk.f();
            c.m_p = tk.v();
            fixture(b, c, tk);
        } else if (op == "box") {
            int b = tk.n();
            b2PolygonShape p;
            float hx = tk.f(); float hy = tk.f();
            p.SetAsBox(hx, hy);
            fixture(b, p, tk);
        } else if (op == "boxc") {
            int b = tk.n();
            b2PolygonShape p;
            float hx = tk.f(); float hy = tk.f();
            b2Vec2 c = tk.v();
            float angle = tk.f();
            p.SetAsBox(hx, hy, c, angle);
            fixture(b, p, tk);
        } else if (op == "polygon") {
            int b = tk.n();
            int n = tk.n();
            b2Vec2 verts[b2_maxPolygonVertices];
            for (int i = 0; i < n; ++i) verts[i] = tk.v();
            b2PolygonShape p;
            p.Set(verts, n);
            fixture(b, p, tk);
        } else if (op == "mass") {
            int b = tk.n();
            b2MassData md;
            md.mass = tk.f();
            md.center = tk.v();
            md.I = tk.f();
            body(b)->SetMassData(&md);
        } else if (op == "transform") {
            int b = tk.n();
            b2Vec2 pos = tk.v();
            float angle = tk.f();
            body(b)->SetTransform(pos, angle);
        } else if (op == "revolute") {
            b2RevoluteJointDef jd;
            jointHead(tk, jd, jd.localAnchorA, jd.localAnchorB);
            jd.referenceAngle = tk.f();
            jd.enableLimit = tk.b();
            jd.lowerAngle = tk.f();
            jd.upperAngle = tk.f();
            jd.enableMotor = tk.b();
            jd.motorSpeed = tk.f();
            jd.maxMotorTorque = tk.f();
            joints.push_back(world->CreateJoint(&jd));
        } else if (op == "prismatic") {
            b2PrismaticJointDef jd;
            jointHead(tk, jd, jd.localAnchorA, jd.localAnchorB);
            jd.localAxisA = tk.v();
            jd.referenceAngle = tk.f();
            jd.enableLimit = tk.b();
            jd.lowerTranslation = tk.f();
            jd.upperTranslation = tk.f();
            jd.enableMotor = tk.b();
            jd.maxMotorForce = tk.f();
            jd.motorSpeed = tk.f();
            joints.push_back(world->CreateJoint(&jd));
        } else if (op == "distance") {
            b2DistanceJointDef jd;
            jointHead(tk, jd, jd.localAnchorA, jd.localAnchorB);
            jd.length = tk.f();
            jd.frequencyHz = tk.f();
            jd.dampingRatio = tk.f();
            if (tk.i < tk.t.size()) jd.resistCompression = tk.b();   // trunk-only flag (docs/04 §1)
            joints.push_back(world->CreateJoint(&jd));
        } else if (op == "wheel") {
            // The trunk's b2LineJoint is the 2.2.1 b2WheelJoint (docs/04-physics.md §1).
            b2WheelJointDef jd;
            jointHead(tk, jd, jd.localAnchorA, jd.localAnchorB);
            jd.localAxisA = tk.v();
            jd.enableMotor = tk.b();
            jd.maxMotorTorque = tk.f();
            jd.motorSpeed = tk.f();
            jd.frequencyHz = tk.f();
            jd.dampingRatio = tk.f();
            joints.push_back(world->CreateJoint(&jd));
        } else if (op == "polygonfix") {
            // The game edited this fixture's polygon in place after creation (no mass/AABB refresh).
            int b = tk.n();
            size_t k = static_cast<size_t>(tk.n());
            b2PolygonShape* p = static_cast<b2PolygonShape*>(fixtures.at(static_cast<size_t>(b)).at(k)->GetShape());
            if (p->GetType() != b2Shape::e_polygon) throw std::runtime_error("polygonfix on a non-polygon fixture");
            int n = tk.n();
            p->m_vertexCount = n;
            for (int i = 0; i < n; ++i) p->m_vertices[i] = tk.v();
            for (int i = 0; i < n; ++i) p->m_normals[i] = tk.v();
            p->m_centroid = tk.v();
        } else if (op == "destroyjoint") {
            size_t j = static_cast<size_t>(tk.n());
            if (joints.at(j)) { world->DestroyJoint(joints[j]); joints[j] = nullptr; }
        } else if (op == "destroybody") {
            size_t idx = static_cast<size_t>(tk.n());
            b2Body* b = bodies.at(idx);
            for (b2Joint*& j : joints)   // the world destroys the body's joints implicitly
                if (j && (j->GetBodyA() == b || j->GetBodyB() == b)) j = nullptr;
            world->DestroyBody(b);
            bodies[idx] = nullptr;
        } else if (op == "continuous") {
            world->SetContinuousPhysics(tk.b());
        } else if (op == "step") {
            steps = tk.n();
            dt = tk.f();
            velIters = tk.n();
            posIters = tk.n();
        } else {
            throw std::runtime_error("unknown scene op: " + op);
        }
    }

    std::vector<std::string> itemLines;   // "# item ..." comments copied through to the trajectory

    void run(FILE* out) {
        std::fprintf(out, "# amazing-alex trajectory v1 bodies=%zu steps=%d\n", bodies.size(), steps);
        for (const std::string& l : itemLines) std::fprintf(out, "%s\n", l.c_str());
        for (int s = 1; s <= steps; ++s) {
            world->Step(dt, velIters, posIters);
            world->ClearForces();   // GameScreen::UpdatePhysics does this after every Step
            for (size_t i = 0; i < bodies.size(); ++i) {
                const b2Body* b = bodies[i];
                if (!b) continue;
                const b2Vec2 p = b->GetPosition();
                const b2Vec2 v = b->GetLinearVelocity();
                std::fprintf(out, "%d %zu %.9g %.9g %.9g %.9g %.9g %.9g %d\n", s, i, p.x, p.y, b->GetAngle(),
                             v.x, v.y, b->GetAngularVelocity(), b->IsAwake() ? 1 : 0);
            }
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: trace_native <scene> <out.traj>\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    Replayer rp;
    int lineNo = 0;
    try {
        for (std::string text; std::getline(in, text);) { ++lineNo; rp.line(text); }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "scene error at line %d: %s\n", lineNo, ex.what());
        return 1;
    }
    FILE* out = std::fopen(argv[2], "w");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    rp.run(out);
    std::fclose(out);
    delete rp.world;
    return 0;
}
