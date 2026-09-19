// Records every Box2D construction call the core makes, in the `.scene` text format of tools/uc_trace.py
// (one call per line, floats as exact float32 bit patterns). The conformance gate G3 diffs these lines
// against the ones recorded from the emulated original (docs/10-architecture.md §8).
#pragma once

#include "aa/sim/types.h"

#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2Fixture.h>
#include <Box2D/Dynamics/Joints/b2DistanceJoint.h>
#include <Box2D/Dynamics/Joints/b2PrismaticJoint.h>
#include <Box2D/Dynamics/Joints/b2RevoluteJoint.h>
#include <Box2D/Dynamics/Joints/b2WheelJoint.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aa::sim {

class SceneRecorder {
public:
    // "gravity gx gy"
    void gravity(Vec2 g);
    // "body <idx> <type> x y angle vx vy w linDamp angDamp allowSleep awake fixedRot bullet active"
    void body(int index, const b2BodyDef& def);
    // Fixture lines; `tail` = "density friction restitution sensor cat mask group" is appended.
    void circle(int body, float radius, Vec2 center, const b2FixtureDef& def);
    void box(int body, float hx, float hy, const b2FixtureDef& def);
    void boxAt(int body, float hx, float hy, Vec2 center, float angle, const b2FixtureDef& def);
    void polygon(int body, const Vec2* vertices, int count, const b2FixtureDef& def);
    // "mass <body> m cx cy I"
    void massData(int body, const b2MassData& md);
    // "transform <body> x y angle"
    void transform(int body, Vec2 position, float angle);
    // Joint lines with the field order of the uc_trace.py docstring; a / b are the body slots.
    // "revolute a b collide ax ay bx by refAngle enableLimit lower upper enableMotor speed maxTorque"
    void revolute(int a, int b, const b2RevoluteJointDef& def);
    // "prismatic a b collide ax ay bx by axX axY refAngle enableLimit lower upper enableMotor maxForce speed"
    void prismatic(int a, int b, const b2PrismaticJointDef& def);
    // "distance a b collide ax ay bx by length freq damp resistCompression"
    void distance(int a, int b, const b2DistanceJointDef& def);
    // "wheel a b collide ax ay bx by axX axY enableMotor maxTorque speed freq damp"
    void wheel(int a, int b, const b2WheelJointDef& def);
    // "polygonfix <body> <fixture> n x1 y1 ... nx1 ny1 ... cx cy" — the final shape of a fixture edited in
    // place. The harness detects these after construction, so the lines are held back and emitted at the
    // end of the scene (one per fixture, bodies and fixtures ascending), dropped when the body is destroyed.
    void polygonFix(int body, int fixture, const Vec2* vertices, const Vec2* normals, int count, Vec2 centroid);
    // "destroyjoint <joint>" / "destroybody <body>"
    void destroyJoint(int joint);
    void destroyBody(int body);
    // "# ..." — comments are ignored by the comparison
    void comment(const std::string& text);

    // The scene: the recorded lines followed by the pending polygonfix lines.
    std::vector<std::string> lines() const;
    std::string text() const;   // lines joined with '\n', trailing newline

    static std::string bits(float f);   // "0x%08x"

private:
    void fixtureTail(std::string& line, const b2FixtureDef& def);
    std::vector<std::string> lines_;
    std::map<std::pair<int, int>, std::string> polygonFixes_;   // (body, fixture) → line
};

}  // namespace aa::sim
