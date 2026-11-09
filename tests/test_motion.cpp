// The motion suite: the data types (11a), the deterministic sampler
// (11b) and the state machine layer with the controllers (11c). Every
// check runs on the deterministic CPU evaluation, which is reproducible
// bit-for-bit.
#include "contract_checks.hpp"
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "grace/motion/motion_controllers.hpp"
#include "grace/motion/motion_machine.hpp"
#include "grace/motion/motion_sampler.hpp"
namespace {
using grace::core::quat;
using grace::core::vec3;
using grace::motion::BlendShapeDesc;
using grace::motion::BoneTrack;
using grace::motion::BoneDesc;
using grace::motion::ClipDesc;
using grace::motion::ClipEvent;
using grace::motion::MachineDesc;
using grace::motion::MotionError;
using grace::motion::Pose;
using grace::motion::QuatKey;
using grace::motion::Sampler;
using grace::motion::SkeletonDesc;
using grace::motion::StateDesc;
using grace::motion::TransitionDesc;
using grace::motion::VecKey;

/// The quaternions' dot product (the core math module declares it
/// for the vector types only).
float quatDot(const quat& a, const quat& b)
{
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}
/// A three-bone skeleton: the root, the middle and the end. The bones
/// run down the +Y axis, one unit apart.
SkeletonDesc makeSkeleton()
{
    SkeletonDesc desc;
    desc.name = "spine";
    BoneDesc root;
    root.name = "root";
    root.parent = -1;
    desc.bones.push_back(root);
    BoneDesc middle;
    middle.name = "middle";
    middle.parent = 0;
    middle.restPosition = vec3{0.0f, 1.0f, 0.0f};
    desc.bones.push_back(middle);
    BoneDesc end;
    end.name = "end";
    end.parent = 1;
    end.restPosition = vec3{0.0f, 1.0f, 0.0f};
    desc.bones.push_back(end);
    return desc;
}
/// A one-second clip that rotates the root's local rotation from
/// identity to a 90-degree rotation about Z over the clip's duration.
ClipDesc makeClip()
{
    ClipDesc clip;
    clip.name = "bend";
    clip.duration = 1.0f;
    clip.loop = false;
    BoneTrack track;
    track.boneIndex = 0;
    track.rotations.push_back(
        QuatKey{0.0f, quat{1.0f, 0.0f, 0.0f, 0.0f}});
    track.rotations.push_back(
        QuatKey{1.0f, grace::core::fromAxisAngle(
                           vec3{0.0f, 0.0f, 1.0f}, 1.5707963f)});
    clip.tracks.push_back(track);
    return clip;
}
void testSkeletonValidation()
{
    std::string message;
    SkeletonDesc desc = makeSkeleton();
    GRACE_CHECK(desc.validate(message));
    GRACE_CHECK(message.empty());
    desc.name.clear();
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "a skeleton's name is empty");
    desc = makeSkeleton();
    desc.bones.clear();
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "a skeleton's bone list is empty");
    desc = makeSkeleton();
    desc.bones[1].parent = 2;
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message.find("topological") != std::string::npos);
    desc = makeSkeleton();
    desc.bones[2].name = "middle";
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message.find("duplicate") != std::string::npos);
    desc = makeSkeleton();
    desc.bones[0].name.clear();
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message.find("name is empty") != std::string::npos);
}
void testClipValidation()
{
    std::string message;
    ClipDesc clip = makeClip();
    GRACE_CHECK(clip.validate(message));
    clip.duration = 0.0f;
    GRACE_CHECK(!clip.validate(message));
    GRACE_CHECK(message == "a clip's duration is not positive");
    clip = makeClip();
    clip.name.clear();
    GRACE_CHECK(!clip.validate(message));
    GRACE_CHECK(message == "a clip's name is empty");
    clip = makeClip();
    clip.tracks[0].positions.clear();
    clip.tracks[0].rotations.clear();
    GRACE_CHECK(!clip.validate(message));
    GRACE_CHECK(message.find("both key lists are empty")
                != std::string::npos);
    clip = makeClip();
    clip.tracks[0].rotations[0].time = 2.0f;
    GRACE_CHECK(!clip.validate(message));
    GRACE_CHECK(message.find("not sorted by time") != std::string::npos);
}
void testBlendShapeValidation()
{
    std::string message;
    BlendShapeDesc shape;
    shape.name = "smile";
    shape.vertexIndices = {0u, 1u};
    shape.offsets = {vec3{0.0f, 0.1f, 0.0f}, vec3{0.0f, 0.1f, 0.0f}};
    GRACE_CHECK(shape.validate(message));
    shape.offsets.pop_back();
    GRACE_CHECK(!shape.validate(message));
    GRACE_CHECK(message.find("counts differ") != std::string::npos);
    shape.offsets.push_back(vec3{0.0f, 0.0f, 0.0f});
    shape.name.clear();
    GRACE_CHECK(!shape.validate(message));
    GRACE_CHECK(message == "a blendshape's name is empty");
}
void testSamplerConstruction()
{
    Sampler sampler;
    bool threw = false;
    try
    {
        sampler.setSkeleton(makeSkeleton());
    }
    catch (const MotionError&)
    {
        threw = true;
    }
    GRACE_CHECK(!threw);
    GRACE_CHECK(sampler.skeleton().bones.size() == 3u);
    // The clip and the blendshape validate through the sampler too.
    sampler.addClip(makeClip());
    GRACE_CHECK(sampler.clipCount() == 1u);
    BlendShapeDesc shape;
    shape.name = "smile";
    shape.vertexIndices = {0u};
    shape.offsets = {vec3{0.0f, 0.1f, 0.0f}};
    sampler.addBlendShape(shape);
    GRACE_CHECK(sampler.shapeCount() == 1u);
    // An invalid clip throws.
    ClipDesc bad = makeClip();
    bad.duration = 0.0f;
    threw = false;
    try
    {
        sampler.addClip(bad);
    }
    catch (const MotionError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}
void testSampleIdentity()
{
    Sampler sampler;
    sampler.setSkeleton(makeSkeleton());
    // A clip with no track on a bone keeps the rest pose: the local
    // rotation stays the identity and the local position stays at the
    // rest position.
    ClipDesc rest = makeClip();
    rest.tracks.clear();
    rest.duration = 1.0f;
    sampler.addClip(rest);
    const Pose pose = sampler.sample(0u, 0.5f);
    GRACE_CHECK(pose.positions.size() == 3u);
    GRACE_CHECK(pose.rotations.size() == 3u);
    GRACE_CHECK((pose.positions[1] == vec3{0.0f, 0.0f, 0.0f}));
    GRACE_CHECK((pose.rotations[0] == quat{1.0f, 0.0f, 0.0f, 0.0f}));
}
void testSampleInterpolation()
{
    Sampler sampler;
    sampler.setSkeleton(makeSkeleton());
    sampler.addClip(makeClip());
    // At t = 0 the rotation is the identity; at t = 1 it is the
    // 90-degree Z rotation; at t = 0.5 the slerp sits half way.
    const Pose start = sampler.sample(0u, 0.0f);
    GRACE_CHECK(start.rotations[0].w > 0.999f);
    const Pose end = sampler.sample(0u, 1.0f);
    const quat target =
        grace::core::fromAxisAngle(vec3{0.0f, 0.0f, 1.0f}, 1.5707963f);
    GRACE_CHECK(quatDot(end.rotations[0], target) > 0.999f);
    const Pose middle = sampler.sample(0u, 0.5f);
    const quat half = grace::core::slerp(
        quat{1.0f, 0.0f, 0.0f, 0.0f}, target, 0.5f);
    GRACE_CHECK(quatDot(middle.rotations[0], half) > 0.999f);
}
void testWrapTime()
{
    Sampler sampler;
    sampler.setSkeleton(makeSkeleton());
    // The loop clip wraps; the non-loop one clamps.
    ClipDesc loop = makeClip();
    loop.name = "walk";
    loop.duration = 1.0f;
    loop.loop = true;
    ClipDesc once = makeClip();
    once.name = "die";
    once.duration = 1.0f;
    once.loop = false;
    GRACE_CHECK(Sampler::wrapTime(loop, 2.5f) == 0.5f);
    GRACE_CHECK(Sampler::wrapTime(once, 2.5f) == 1.0f);
    GRACE_CHECK(Sampler::wrapTime(loop, -0.1f) == 0.0f);
}
void testBlendPose()
{
    Pose a;
    a.positions = {vec3{0.0f, 0.0f, 0.0f}, vec3{1.0f, 0.0f, 0.0f}};
    a.rotations = {quat{1.0f, 0.0f, 0.0f, 0.0f},
                   quat{1.0f, 0.0f, 0.0f, 0.0f}};
    a.shapeWeights = {0.0f};
    Pose b;
    b.positions = {vec3{0.0f, 2.0f, 0.0f}, vec3{1.0f, 4.0f, 0.0f}};
    b.rotations = {grace::core::fromAxisAngle(vec3{0.0f, 0.0f, 1.0f},
                                              1.5707963f),
                   quat{1.0f, 0.0f, 0.0f, 0.0f}};
    b.shapeWeights = {1.0f};
    // t = 0 keeps `a`; t = 1 reaches `b`; t = 0.5 sits half way.
    Pose half = a;
    Sampler::blendPose(half, b, 0.0f);
    GRACE_CHECK((half.positions[0] == vec3{0.0f, 0.0f, 0.0f}));
    GRACE_CHECK(half.shapeWeights[0] == 0.0f);
    half = a;
    Sampler::blendPose(half, b, 1.0f);
    GRACE_CHECK((half.positions[0] == vec3{0.0f, 2.0f, 0.0f}));
    GRACE_CHECK(half.shapeWeights[0] == 1.0f);
    half = a;
    Sampler::blendPose(half, b, 0.5f);
    GRACE_CHECK((half.positions[0] == vec3{0.0f, 1.0f, 0.0f}));
    GRACE_CHECK(half.shapeWeights[0] == 0.5f);
    const quat halfRotation = grace::core::slerp(
        a.rotations[0], b.rotations[0], 0.5f);
    GRACE_CHECK(quatDot(half.rotations[0], halfRotation)
                > 0.999f);
}
void testEvents()
{
    Sampler sampler;
    sampler.setSkeleton(makeSkeleton());
    ClipDesc clip = makeClip();
    clip.name = "attack";
    clip.duration = 2.0f;
    clip.loop = true;
    clip.events.push_back(ClipEvent{"hit", "damage", 1.0f});
    clip.events.push_back(ClipEvent{"step", "sfx", 0.5f});
    sampler.addClip(clip);
    // The events in (0, 1] fire: the step at 0.5 and the hit at 1.0.
    std::vector<ClipEvent> fired = sampler.eventsAt(0u, 0.0f, 1.0f);
    GRACE_CHECK(fired.size() == 2u);
    // The events in (1.0, 1.5] fire none.
    fired = sampler.eventsAt(0u, 1.0f, 1.5f);
    GRACE_CHECK(fired.empty());
    // The loop wrap: the interval (1.5, 0.25] spans (1.5, 2] and
    // [0, 0.25], which holds no event; the interval (1.6, 0.6] spans
    // (1.6, 2] and [0, 0.6], which holds the step at 0.5.
    fired = sampler.eventsAt(0u, 1.5f, 0.25f);
    GRACE_CHECK(fired.empty());
    fired = sampler.eventsAt(0u, 1.6f, 0.6f);
    GRACE_CHECK(fired.size() == 1u);
    GRACE_CHECK(fired[0].name == "step");
}
void testFkPose()
{
    Sampler sampler;
    sampler.setSkeleton(makeSkeleton());
    // The rest pose's world transforms: the root at the origin, the
    // middle one unit up, the end two units up.
    ClipDesc rest = makeClip();
    rest.tracks.clear();
    sampler.addClip(rest);
    const Pose pose = sampler.sample(0u, 0.0f);
    const std::vector<grace::core::mat4> world = sampler.fkPose(pose);
    GRACE_CHECK(world.size() == 3u);
    // The middle's world position is (0, 1, 0); the end's is (0, 2, 0).
    const vec3 middlePos{world[1](3, 0), world[1](3, 1), world[1](3, 2)};
    GRACE_CHECK(std::abs(middlePos.y - 1.0f) < 0.001f);
    const vec3 endPos{world[2](3, 0), world[2](3, 1), world[2](3, 2)};
    GRACE_CHECK(std::abs(endPos.y - 2.0f) < 0.001f);
}
void testMachineGraph()
{
    MachineDesc desc;
    StateDesc idle;
    idle.name = "idle";
    idle.clipId = 0u;
    idle.loop = true;
    StateDesc walk;
    walk.name = "walk";
    walk.clipId = 0u;
    walk.loop = true;
    desc.states = {idle, walk};
    TransitionDesc transition;
    transition.from = 0u;
    transition.to = 1u;
    transition.trigger = "start";
    transition.fadeDuration = 0.25f;
    desc.transitions = {transition};
    desc.initial = 0u;
    std::string message;
    GRACE_CHECK(desc.validate(message));
    grace::motion::Layer layer;
    layer.setGraph(desc);
    GRACE_CHECK(layer.state() == 0u);
    GRACE_CHECK(!layer.isFading());
    // A trigger that names no transition in the layer fires nothing.
    GRACE_CHECK(!layer.trigger("unknown"));
    GRACE_CHECK(layer.state() == 0u);
    // The real trigger fires and starts the cross-fade.
    GRACE_CHECK(layer.trigger("start"));
    GRACE_CHECK(layer.state() == 1u);
    GRACE_CHECK(layer.isFading());
    GRACE_CHECK(layer.previousState() == 0u);
    // The fade advances with the update and ends at the duration.
    layer.update(0.1f);
    GRACE_CHECK(layer.isFading());
    layer.update(0.2f);
    GRACE_CHECK(!layer.isFading());
}
void testMachineExitTime()
{
    MachineDesc desc;
    StateDesc play;
    play.name = "play";
    play.clipId = 0u;
    play.loop = false;
    StateDesc end;
    end.name = "end";
    end.clipId = 0u;
    end.loop = true;
    desc.states = {play, end};
    TransitionDesc transition;
    transition.from = 0u;
    transition.to = 1u;
    transition.exitTime = 0.5f;
    transition.fadeDuration = 0.0f;
    desc.transitions = {transition};
    desc.initial = 0u;
    grace::motion::Layer layer;
    layer.setGraph(desc);
    // The exit time fires at 0.5; the clip time resets on the entry.
    layer.update(0.3f);
    GRACE_CHECK(layer.state() == 0u);
    layer.update(0.3f);
    GRACE_CHECK(layer.state() == 1u);
    GRACE_CHECK(!layer.isFading());
    GRACE_CHECK(layer.clipTime() == 0.0f);
}
void testMachineBlend()
{
    MachineDesc desc;
    StateDesc a;
    a.name = "a";
    a.clipId = 0u;
    StateDesc b;
    b.name = "b";
    b.clipId = 0u;
    desc.states = {a, b};
    TransitionDesc transition;
    transition.from = 0u;
    transition.to = 1u;
    transition.trigger = "go";
    transition.fadeDuration = 1.0f;
    desc.transitions = {transition};
    desc.initial = 0u;
    grace::motion::Layer layer;
    layer.setGraph(desc);
    layer.trigger("go");
    // The poses per state; the blend at the fade's start keeps the
    // previous pose.
    Pose poseA;
    poseA.positions = {vec3{0.0f, 0.0f, 0.0f}};
    poseA.rotations = {quat{1.0f, 0.0f, 0.0f, 0.0f}};
    Pose poseB;
    poseB.positions = {vec3{0.0f, 2.0f, 0.0f}};
    poseB.rotations = {quat{1.0f, 0.0f, 0.0f, 0.0f}};
    std::vector<Pose> poses = {poseA, poseB};
    Pose blended;
    layer.blendPose(poses, blended);
    // At the fade's start the output is the previous pose: the
    // transition is smooth from the pose the layer was playing.
    GRACE_CHECK((blended.positions[0] == vec3{0.0f, 0.0f, 0.0f}));
    // After the fade advances, the output moves towards the current.
    layer.update(0.5f);
    layer.blendPose(poses, blended);
    GRACE_CHECK(std::abs(blended.positions[0].y - 1.0f) < 0.001f);
    layer.update(0.5f);
    layer.blendPose(poses, blended);
    GRACE_CHECK((blended.positions[0] == vec3{0.0f, 2.0f, 0.0f}));
}
void testControllers()
{
    // The look-at rotates the bone's forward axis (+Z) onto the aim
    // direction (+X, here the target point normalised).
    grace::motion::LookAtController look(2u, vec3{1.0f, 0.0f, 0.0f});
    GRACE_CHECK(look.bone() == 2u);
    Pose pose;
    pose.positions = {vec3{0.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 0.0f},
                      vec3{0.0f, 0.0f, 0.0f}};
    pose.rotations = {quat{1.0f, 0.0f, 0.0f, 0.0f},
                      quat{1.0f, 0.0f, 0.0f, 0.0f},
                      quat{1.0f, 0.0f, 0.0f, 0.0f}};
    quat rotation = pose.rotations[2];
    look.apply(pose, 0.0f, rotation);
    const vec3 aimed = grace::core::rotate(rotation,
                                           vec3{0.0f, 0.0f, 1.0f});
    GRACE_CHECK(aimed.x > 0.999f);
    // The foot placement rotates the bone's +Y axis onto the target.
    grace::motion::FootController foot(1u, vec3{0.0f, 1.0f, 0.0f});
    GRACE_CHECK(foot.bone() == 1u);
    quat footRotation = pose.rotations[1];
    foot.apply(pose, 0.0f, footRotation);
    const vec3 down = grace::core::rotate(footRotation,
                                          vec3{0.0f, 1.0f, 0.0f});
    GRACE_CHECK(down.y > 0.999f);
}
void testMachineValidation()
{
    std::string message;
    MachineDesc desc;
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message == "a machine's state list is empty");
    StateDesc state;
    state.name = "idle";
    desc.states = {state};
    GRACE_CHECK(desc.validate(message));
    TransitionDesc transition;
    transition.from = 0u;
    transition.to = 5u;
    desc.transitions = {transition};
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message.find("out of range") != std::string::npos);
    desc.transitions.clear();
    desc.initial = 3u;
    GRACE_CHECK(!desc.validate(message));
    GRACE_CHECK(message.find("initial state") != std::string::npos);
}
}  // namespace
int main()
{
    testSkeletonValidation();
    testClipValidation();
    testBlendShapeValidation();
    testSamplerConstruction();
    testSampleIdentity();
    testSampleInterpolation();
    testWrapTime();
    testBlendPose();
    testEvents();
    testFkPose();
    testMachineGraph();
    testMachineExitTime();
    testMachineBlend();
    testControllers();
    testMachineValidation();
    return grace::test::summary("motion");
}
