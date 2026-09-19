#include "aa/sim/scene_recorder.h"

#include "aa/sim/float_bits.h"

#include <cstdio>
#include <iterator>

namespace aa::sim {

std::string SceneRecorder::bits(float f) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", static_cast<unsigned>(bitsFromFloat(f)));
    return buf;
}

namespace {

std::string vec(Vec2 v) { return SceneRecorder::bits(v.x) + " " + SceneRecorder::bits(v.y); }
std::string flag(bool b) { return b ? "1" : "0"; }

}  // namespace

void SceneRecorder::gravity(Vec2 g) { lines_.push_back("gravity " + vec(g)); }

void SceneRecorder::body(int index, const b2BodyDef& def) {
    lines_.push_back("body " + std::to_string(index) + " " + std::to_string(static_cast<int>(def.type)) + " " +
                     vec(def.position) + " " + bits(def.angle) + " " + vec(def.linearVelocity) + " " +
                     bits(def.angularVelocity) + " " + bits(def.linearDamping) + " " + bits(def.angularDamping) +
                     " " + flag(def.allowSleep) + " " + flag(def.awake) + " " + flag(def.fixedRotation) + " " +
                     flag(def.bullet) + " " + flag(def.active));
}

void SceneRecorder::fixtureTail(std::string& line, const b2FixtureDef& def) {
    line += " " + bits(def.density) + " " + bits(def.friction) + " " + bits(def.restitution) + " " +
            flag(def.isSensor) + " " + std::to_string(def.filter.categoryBits) + " " +
            std::to_string(def.filter.maskBits) + " " + std::to_string(def.filter.groupIndex);
}

void SceneRecorder::circle(int body, float radius, Vec2 center, const b2FixtureDef& def) {
    std::string line = "circle " + std::to_string(body) + " " + bits(radius) + " " + vec(center);
    fixtureTail(line, def);
    lines_.push_back(line);
}

void SceneRecorder::box(int body, float hx, float hy, const b2FixtureDef& def) {
    std::string line = "box " + std::to_string(body) + " " + bits(hx) + " " + bits(hy);
    fixtureTail(line, def);
    lines_.push_back(line);
}

void SceneRecorder::boxAt(int body, float hx, float hy, Vec2 center, float angle, const b2FixtureDef& def) {
    std::string line = "boxc " + std::to_string(body) + " " + bits(hx) + " " + bits(hy) + " " + vec(center) + " " +
                       bits(angle);
    fixtureTail(line, def);
    lines_.push_back(line);
}

void SceneRecorder::polygon(int body, const Vec2* vertices, int count, const b2FixtureDef& def) {
    std::string line = "polygon " + std::to_string(body) + " " + std::to_string(count);
    for (int i = 0; i < count; ++i) line += " " + vec(vertices[i]);
    fixtureTail(line, def);
    lines_.push_back(line);
}

void SceneRecorder::massData(int body, const b2MassData& md) {
    lines_.push_back("mass " + std::to_string(body) + " " + bits(md.mass) + " " + vec(md.center) + " " + bits(md.I));
}

void SceneRecorder::transform(int body, Vec2 position, float angle) {
    lines_.push_back("transform " + std::to_string(body) + " " + vec(position) + " " + bits(angle));
}

namespace {
std::string jointHead(const char* kind, int a, int b, const b2JointDef& def, Vec2 anchorA, Vec2 anchorB) {
    return std::string(kind) + " " + std::to_string(a) + " " + std::to_string(b) + " " + flag(def.collideConnected) +
           " " + vec(anchorA) + " " + vec(anchorB);
}
}  // namespace

void SceneRecorder::revolute(int a, int b, const b2RevoluteJointDef& def) {
    lines_.push_back(jointHead("revolute", a, b, def, def.localAnchorA, def.localAnchorB) + " " +
                     bits(def.referenceAngle) + " " + flag(def.enableLimit) + " " + bits(def.lowerAngle) + " " +
                     bits(def.upperAngle) + " " + flag(def.enableMotor) + " " + bits(def.motorSpeed) + " " +
                     bits(def.maxMotorTorque));
}

void SceneRecorder::prismatic(int a, int b, const b2PrismaticJointDef& def) {
    lines_.push_back(jointHead("prismatic", a, b, def, def.localAnchorA, def.localAnchorB) + " " +
                     vec(def.localAxisA) + " " + bits(def.referenceAngle) + " " + flag(def.enableLimit) + " " +
                     bits(def.lowerTranslation) + " " + bits(def.upperTranslation) + " " + flag(def.enableMotor) +
                     " " + bits(def.maxMotorForce) + " " + bits(def.motorSpeed));
}

void SceneRecorder::distance(int a, int b, const b2DistanceJointDef& def) {
    lines_.push_back(jointHead("distance", a, b, def, def.localAnchorA, def.localAnchorB) + " " + bits(def.length) +
                     " " + bits(def.frequencyHz) + " " + bits(def.dampingRatio) + " " + flag(def.resistCompression));
}

void SceneRecorder::wheel(int a, int b, const b2WheelJointDef& def) {
    lines_.push_back(jointHead("wheel", a, b, def, def.localAnchorA, def.localAnchorB) + " " + vec(def.localAxisA) +
                     " " + flag(def.enableMotor) + " " + bits(def.maxMotorTorque) + " " + bits(def.motorSpeed) + " " +
                     bits(def.frequencyHz) + " " + bits(def.dampingRatio));
}

void SceneRecorder::polygonFix(int body, int fixture, const Vec2* vertices, const Vec2* normals, int count,
                               Vec2 centroid) {
    std::string line = "polygonfix " + std::to_string(body) + " " + std::to_string(fixture) + " " + std::to_string(count);
    for (int i = 0; i < count; ++i) line += " " + vec(vertices[i]);
    for (int i = 0; i < count; ++i) line += " " + vec(normals[i]);
    line += " " + vec(centroid);
    polygonFixes_[{body, fixture}] = line;
}

void SceneRecorder::destroyJoint(int joint) { lines_.push_back("destroyjoint " + std::to_string(joint)); }

void SceneRecorder::destroyBody(int body) {
    lines_.push_back("destroybody " + std::to_string(body));
    for (auto it = polygonFixes_.begin(); it != polygonFixes_.end();) {
        it = it->first.first == body ? polygonFixes_.erase(it) : std::next(it);
    }
}

void SceneRecorder::comment(const std::string& text) { lines_.push_back("# " + text); }

std::vector<std::string> SceneRecorder::lines() const {
    std::vector<std::string> out = lines_;
    for (const auto& [key, line] : polygonFixes_) out.push_back(line);
    return out;
}

std::string SceneRecorder::text() const {
    std::string out;
    for (const std::string& l : lines()) {
        out += l;
        out += '\n';
    }
    return out;
}

}  // namespace aa::sim
