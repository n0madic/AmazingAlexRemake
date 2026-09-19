// Per-type item code (st::<Item>Utils, docs/03-game-items.md §2): one free function per type for what the
// original exposes. This stage ports CreatePhysics of the simple items; Update / collision handlers /
// set-up helpers follow in M3–M4 (docs/10-architecture.md §5.3).
#pragma once

#include "aa/sim/action.h"
#include "aa/sim/goal_state.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/types.h"
#include "aa/sim/world_state.h"

#include <stdexcept>

namespace aa::sim {

// Thrown by createPhysics for a type whose port does not exist yet, so the conformance driver can skip it.
class NotImplemented : public std::runtime_error {
public:
    explicit NotImplemented(ItemType type);
    ItemType type() const { return type_; }

private:
    ItemType type_;
};

// PhysicsObjectUtils::CreatePhysics: creates the bodies and fixtures of one object in the given mode
// (set-up: every body dynamic plus selection sensors; simulation: the real body types). The bodies are
// appended to obj.bodies in creation order; `item` supplies the per-type state (book colour, world-bound
// variant, end vectors).
void createPhysics(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
bool isItemImplemented(ItemType type);

namespace items {
void createWorldBound(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
void createShelf(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createBall(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);          // 2 3 4 16 26 41
void createBox(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);           // 10 11 12
void createBook(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
void createBucket(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createLaundryBasket(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createHangingLamp(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createHook(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createGoalStar(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// GoalStarUtils::Update (after the physics step): a star whose sensor touches any body starts collecting
// — sparkles, sound 0x3d + collected count, GoalState.collectedStars++ — shrinks along the 0.4 s curve
// (the object scale) and is removed (action 7) when the timer reaches 0.4 s [verified].
void updateGoalStars(float dt, WorldState& state, PhysicsWorld& world, GoalState& goal, ActionQueue& queue);
void createBillboard(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createBumper(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// BumperUtils::Update (before the step): a bumper that is on counts its 0.18 s down (and skips the scan
// on the substep it turns off); an idle one fires HandleCollision for every touching non-sensor contact
// of its body with the manifold's normal as it is (not turned toward the other body).
void updateBumpers(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
void createPaperPlane(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// PaperPlaneUtils::Update (before the step): the glide force — a lift of ±5·cos(angle)·vy² (flip-signed,
// evaluated in double) along x, 2·vx² ± 10·cos(angle)·vy² along y — and an angular damping torque −dt·ω.
void updatePaperPlanes(float dt, WorldState& state, PhysicsWorld& world);
void createPulley(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createMagnet(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// MagnetUtils::Update (before the step): every magnetic object within 1 m of the pole and inside the 80°
// cone in front of it is pulled toward the pole with (300 − 300·d²) · m / 0.1 · dt at its magnetic
// centre; the pulse frame advances every 1/30 s while something is pulled.
void updateMagnets(float dt, WorldState& state, PhysicsWorld& world);
// GameItemUtils::GetMagneticCenter: the paper plane's nose (r along its axis), the dart's tip side
// (0.3 r), every other object's body-0 world centre.
Vec2 magneticCenter(const PhysicsObject& obj, const PhysicsWorld& world);
void createHelicopter(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
// HelicopterUtils::Update (before the step): a running helicopter levels itself toward −10° / the tilt
// target (10° while rising, 55° otherwise, with the throttle ramping to 100 N), gets a lift torque and
// the thrust along its up axis; the rotor / tail phases spin up (10, 60 per s²) or down (10, 20).
void updateHelicopters(float dt, WorldState& state, PhysicsWorld& world);
void createPipe(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createPipe90(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createBalloon(PhysicsObject& obj, PhysicsWorld& world);
// BalloonUtils::Update (before the step): the lift / drag force at the knot of an intact balloon; a popped
// one loses its physics (activated for goal 5) and is removed when the 0.15 s pop timer runs out.
void updateBalloons(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
void createPiggyBank(PhysicsObject& obj, PhysicsWorld& world);
// PiggyBankUtils::Update (after the substep loop, with the accumulator as dt): a broken bank's POW timer
// counts up to 0.2 s.
void updatePiggyBanks(float dt, WorldState& state);
void createDart(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// DartUtils::UpdateSetUpMode: the idle wobble animation of every unstuck dart (set-up: every frame;
// simulation: after the substep loop with the accumulator as dt) — draws from the game's Random.
void updateDartsSetUpMode(float dt, WorldState& state, Random& random);
void createScissors(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
// ScissorsUtils::UpdateAngle: places the two halves about the item centre (cutAngle = Scissors+0xC).
void updateScissorsAngle(PhysicsObject& obj, PhysicsWorld& world, float cutAngle);
// ScissorsUtils::Update (after the step): closing scissors shrink the cut angle by 2 rad/s; at 0 the
// blade tip's half-circle (r/2 in front of the centre) is queried for rope links and every link found
// is cut from its neighbour nearest the tip (RopeUtils::Cut), with the Cut sound; then the halves are
// re-placed. Every scissors' idle snip animation advances afterwards (draws from the game's Random).
void updateScissors(float dt, WorldState& state, PhysicsWorld& world, Random& random, ActionQueue& queue);
// ScissorsUtils::UpdateSetUpMode: the idle snip animation only (GameScreen::UpdatePaused).
void updateScissorsSetUpMode(float dt, WorldState& state, Random& random);
// RopeUtils::Cut(rope, obj, i, j): the distance joint between links i and j and the rope's end-to-end
// joint are destroyed, every link but the root is nudged by ±0.001 m along x, the rope is activated.
void cutRope(WorldState& state, PhysicsWorld& world, PhysicsObject& rope, int linkA, int linkB);
void createSeesaw(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// SeesawUtils::Update (before the step): the SeesawMove sound when the pivot turns faster than 4 rad/s
// in a direction different from the last sound's.
void updateSeesaws(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
void createTrapdoorLever(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// TrapdoorLeverUtils::Update (before the step): a lever moving faster than 5 rad/s plays its sound once;
// past ±18° it unlocks the paired trapdoor (TrapdoorUtils::Unlock: the doors turn dynamic, sound 0x2f).
void updateTrapdoorLevers(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
void unlockTrapdoor(const PhysicsObject& trapdoor, PhysicsWorld& world, ActionQueue& queue);
void createRCController(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// RadioControllerUtils::Update (before the step): the button joint's translation below −0.03 m presses
// the button — the paired truck's wheel motors run at ±15 rad/s (the flip decides the direction) or the
// paired helicopter turns on, with the click sound; releasing stops the motors / turns the rotor off.
void updateRadioControllers(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
void createBoxingGlove(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// BoxingGloveUtils' UpdateArmGeometry (FUN_000a9578): re-shapes the stand's head box from the fist
// distance; returns the lattice hinge angle the renderer draws (BoxingGlove+0x24).
float updateGloveArmGeometry(PhysicsObject& obj, PhysicsWorld& world);
// BoxingGloveUtils::Update (before the step): a triggered glove's fist is driven along the arm by a
// spring (−5850 N/m toward 0.75 m while punching, −1170 toward 0.6 m once retracted) with −40 v damping;
// the button interpolator and the arm geometry advance every substep.
void updateBoxingGloves(float dt, WorldState& state, PhysicsWorld& world);
// The lattice hinge angle for a stand / fist pair (the asin part of UpdateArmGeometry), for the renderer.
float gloveLatticeAngle(Vec2 standPos, Vec2 fistPos);
void createSkateboard(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createRCTruck(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
void createSpring(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
// SpringUtils::Update (before the step): a compressed spring (plate–seat distance more than 0.01 below the
// joint length) that starts to expand switches the distance joint to 12 + (1 − t)·78 Hz over the 0.16 m
// travel with the Spring sound; back at full length the joint returns to 12 Hz / 0.1. The kinematic base
// body follows the middle of the plate–seat segment and its block box is re-sized to the gap.
void updateSprings(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
void createTrapdoor(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
void createSlingshot(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
// World position of the slingshot pouch (FUN_000ebe98).
Vec2 slingshotPouchPosition(const PhysicsObject& obj, const GameItem& item);
// SlingshotUtils::UpdatePos: body 0 moves the frame; the pouch body pulls the pouch vector (≤ 0.5 m,
// stretch / release sounds once the set-up timer passed 0.5 s); the pouch body follows.
void slingshotUpdatePos(GameItem& item, PhysicsObject& obj, PhysicsWorld& world, int bodyIndex, Vec2 target, ActionQueue& queue);
void createZipLine(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
// FUN_000fcb78 (= ZipLineUtils::ManipulationEnded): anchors, trolley and the set-up line body re-laid
// from the position and end vector; the line box re-shaped.
void layoutZipLine(const GameItem& item, const PhysicsObject& obj, PhysicsWorld& world);
// ZipLineUtils::UpdatePos: body 0 drags the anchor with snapping; any other body drags the far end.
void zipLineUpdatePos(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj, int bodyIndex, Vec2 target);
void createRope(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode);
// Number of link bodies for a rope with the given end vector (docs/04 §8).
int ropeLinkCount(Vec2 endVector);
// RopeRenderUtils::CalculateBodyIndices: the chain order of the link bodies (bodyCount − 1 entries).
void ropeBodyIndices(int bodyCount, int* out);
// RopeUtils::UpdateLinkPositionsFromExtremes: lays the links on the segment from the position to the end.
void updateRopeLinksFromExtremes(const PhysicsObject& obj, const GameItem& item, PhysicsWorld& world);
// RopeUtils::UpdatePosFromAttachedObjects: moves the ends onto the attached objects, rebuilds the links
// when their count changes and lays them out again.
void updateRopeFromAttachedObjects(const WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj);
// --- rope set-up interaction (docs/05 §5) [verified] ---------------------------------------------
// FUN_000e37b0: destroys the middle links (or the end-to-end joint of a two-link rope) and re-creates
// the chain for `links` bodies with the mass override and the distance joints.
void rebuildRopeLinks(const WorldState& state, PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, int links);
// (anon)::SetRopeMassData: 0.01 kg per body, 0.001 kg when an end hangs on a balloon.
void setRopeMassData(const WorldState& state, PhysicsObject& obj, PhysicsWorld& world);
// RopeUtils::AttachmentChanged: the end-to-end distance joint (FUN_000e2b48) exists while both ends
// are attached; the end's selectable byte is cleared while snapped or attached; masses refreshed.
void ropeAttachmentChanged(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj, int point);
// FUN_000e2b48: a distance joint of length 1.04·|end| between the two attached objects' bodies.
void createRopeEndJoint(const WorldState& state, PhysicsWorld& world, GameItem& item, const PhysicsObject& obj);
// RopeUtils::GetConstrainedPos: an end held while the other end is snapped/attached stays within 1.0065 m
// of that end (bodyIndex 1 = end A pulled towards end B, 2 = end B towards A, 0 = towards end A).
Vec2 ropeConstrainedPos(const GameItem& item, const PhysicsObject& obj, int bodyIndex, Vec2 pos);
// RopeUtils::SetEndPosition: end 0 = the object position, end 1 = the end vector.
void ropeSetEndPosition(GameItem& item, PhysicsObject& obj, int end, Vec2 pos);
// RopeUtils::UpdatePos: the drag of one rope body with velocity-gated snapping, then the chain re-laid.
void ropeUpdatePos(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj, int bodyIndex, Vec2 target,
                   Vec2 dragVelocity);
// RopeUtils::ManipulationStarted: detaches what the grabbed body must let go of.
void ropeManipulationStarted(WorldState& state, PhysicsWorld& world, PhysicsObject& obj, int bodyIndex);
// RopeUtils::ManipulationEnded: chain rebuilt and re-laid, the end joint re-created, AttachToNearbyItems.
void ropeManipulationEnded(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj);
// RopeUtils::SetSelectionCollisionFilters: the first fixture of bodies 0..2 back to the Selection filter.
void ropeSetSelectionCollisionFilters(const PhysicsObject& obj, PhysicsWorld& world);
void createDoll(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);
void createSelectionArea(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode);

// --- simulation: collision handlers (WorldContactListener, docs/03 §2) [verified] ------------------
// BalloonUtils::Pop: popped, the 0.15 s pop timer, sound 0x1f and the radial force action (0.5 m, 25).
void popBalloon(GameItem& item, const PhysicsObject& obj, ActionQueue& queue);
// SlingshotUtils::Update (before the step): the first substep fires — sound 0x26, the object found at the
// pouch (the pick query) is pushed toward the rest point with 0.6 · log2(1 + m) · 2500 · Δ and remembered
// as the launched object — then the pouch springs back to its rest point every substep.
void updateSlingshots(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue);
// SlingshotUtils::ShouldCollide: a fired slingshot ignores the object it launched (Slingshot+0x20).
bool slingshotShouldCollide(const GameItem& item, int otherHandle);
// BumperUtils::HandleCollision: the first contact turns the bumper on (0.18 s), sound 0x17, and pushes the
// other body along `normal` with log2(1 + mass) · 300 (action 18).
void bumperHandleCollision(GameItem& item, const PhysicsObject& bumper, const PhysicsObject& other, int otherBody,
                           Vec2 normal, ActionQueue& queue, PhysicsWorld& world);
// BoxingGloveUtils::HandleCollision: an impact of |v_n · m_other| ≥ 0.2 on the trigger button releases the
// holding motor, hides the button (NonCollidable) and starts the 36 → 8 px interpolation; sound 0x24 at
// `soundAt` (the original passes the *other* object's position when the glove is the contact's body B).
void gloveHandleCollision(GameItem& item, PhysicsObject& glove, const PhysicsObject& soundAt, const PhysicsObject& other,
                          int otherBody, float impact, ActionQueue& queue, PhysicsWorld& world);
// BoxingGloveUtils::HandleCollisionSounds: the punching fist (body 1) faster than 3 m/s hitting with
// impact > 3 → sound 0x25.
void gloveHandleCollisionSounds(const GameItem& item, const PhysicsObject& obj, int bodyIndex, float impact,
                                ActionQueue& queue, const PhysicsWorld& world);
// ScissorsUtils::HandleCollision: an impact |v_n · m_other| > 0.01 on open scissors starts the cut; sound 0x22.
void scissorsHandleCollision(GameItem& item, const PhysicsObject& obj, const b2Body* other, float impact, ActionQueue& queue);
// HelicopterUtils::HandleCollision: a running rotor (fixture 4) or tail (fixture 2) hitting something pushes
// both apart (actions 18, 80 N split by mass) with a hit sound.
void helicopterHandleCollision(const GameItem& item, const PhysicsObject& heli, const b2Fixture* fixture, const PhysicsObject& other,
                               int otherBody, Vec2 point, Vec2 normal, ActionQueue& queue, PhysicsWorld& world);
// HelicopterUtils::TurnOn / TurnOff: Helicopter+0x10 and the rotor / tail fixture filters (group −2 while on).
void helicopterTurnOn(GameItem& item, const PhysicsObject& obj, PhysicsWorld& world);
void helicopterTurnOff(GameItem& item, const PhysicsObject& obj, PhysicsWorld& world);
// DartUtils::HandleStabCollision: the tip (group −8) hitting a stabbable object along its direction
// (dot ≥ 0.65) queues action 17, makes the tip non-collidable and marks the dart stuck.
void dartHandleStabCollision(GameItem& item, const PhysicsObject& dart, const PhysicsObject& other, int dartBody, int otherBody,
                             b2Fixture* dartFixture, Vec2 point, Vec2 relativeVelocity, ActionQueue& queue);
// DollUtils::HandleCollisionSounds: bodies 1 / 2 / 5 → sound 0x1d; body 0 with impact > 3 → sound 0x1e.
void dollHandleCollisionSounds(const PhysicsObject& obj, int bodyIndex, float impact, ActionQueue& queue);
// GameItemUtils::HandleCollisionSounds: the per-type impact sound (volume from |impact| / 5) unless the
// queue already holds a sound for the object or ten of the same sound. `timeSeed` seeds the bouncy ball's
// random sound choice (TimeUtils::GetAbsoluteTime in the original).
void handleCollisionSounds(const GameItem& item, const PhysicsObject& obj, int bodyIndex, float impact, ActionQueue& queue,
                           const PhysicsWorld& world, int timeSeed);

// --- simulation: the world-side actions (GameScreen::ProcessSimulationAction) [verified] ------------
// GameItemUtils::Break → PiggyBankUtils::Break: activated, non-collidable, the debris bodies with the
// velocity table, sound 0x20. `breakVector` = action 12's (+0x10, +0x14).
void breakItem(WorldState& state, PhysicsWorld& world, int itemIndex, Vec2 breakVector, ActionQueue& queue);
// GameItemUtils::AttachSharpObject: the revolute joint (collideConnected, motor 4·v²) between the dart body
// and the stabbed body at `point`, sound 0x16.
void attachSharpObject(WorldState& state, PhysicsWorld& world, int dartHandle, int dartBody, int otherHandle, int otherBody,
                       Vec2 point, float speed, ActionQueue& queue);
// ApplyForcesUtils::ForceToItem: ApplyForce(force, point) on the item's body (dynamic bodies only).
void forceToItem(WorldState& state, PhysicsWorld& world, int handle, int bodyIndex, Vec2 force, Vec2 point);
// ApplyForcesUtils::ForceToRadius: every Dynamic-category fixture within `radius` of `center` pushes its
// body away with (1 − d / radius) · force · log2(1 + mass) (per fixture found).
void forceToRadius(WorldState& state, PhysicsWorld& world, Vec2 center, float radius, float force);
}  // namespace items

}  // namespace aa::sim
