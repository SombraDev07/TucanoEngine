// Gate for the skeletal animation math (Phase I-1, ported from B3DFramework).
// Curves, quaternion handling, skeleton evaluation, clip sampling, blending and playback are all
// exercised numerically — no window, no asset files.

#define GLM_ENABLE_EXPERIMENTAL
#include "Animation/AnimationClip.h"
#include "Animation/AnimationCurve.h"
#include "Animation/Skeleton.h"

#include "AssetPipeline/GLTFAnimation.h"
#include "gltf_fixture.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>

#include <cmath>
#include <cstdio>
#include <string>

using namespace tucano::anim;

namespace {

int g_failures = 0;

void check(const std::string& label, bool ok) {
  std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label.c_str());
  if (!ok) ++g_failures;
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

bool nearVec(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f) {
  return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

} // namespace

int main() {
  std::printf("-- float curve --\n");
  {
    CurveFloat c;
    c.addKey({0.0f, 0.0f, 0.0f, 0.0f});
    c.addKey({10.0f, 0.0f, 0.0f, 1.0f});
    c.addKey({20.0f, 0.0f, 0.0f, 2.0f});

    check("start/end time", near(c.startTime(), 0.0f) && near(c.endTime(), 2.0f));
    check("length", near(c.length(), 2.0f));
    check("exact key hit at t=0", near(c.evaluate(0.0f), 0.0f));
    check("exact key hit at t=1", near(c.evaluate(1.0f), 10.0f));
    check("exact key hit at t=2", near(c.evaluate(2.0f), 20.0f));
    // Zero tangents make the segment a smooth ease, so the midpoint sits at the halfway value.
    check("midpoint interpolates", near(c.evaluate(0.5f), 5.0f, 0.01f));
    check("clamps before start", near(c.evaluate(-5.0f), 0.0f));
    check("clamps after end", near(c.evaluate(99.0f), 20.0f));
  }

  std::printf("\n-- keys are sorted regardless of insert order --\n");
  {
    CurveFloat c;
    c.addKey({30.0f, 0.0f, 0.0f, 3.0f});
    c.addKey({10.0f, 0.0f, 0.0f, 1.0f});
    c.addKey({20.0f, 0.0f, 0.0f, 2.0f});
    check("sorted by time", c.keys()[0].time < c.keys()[1].time && c.keys()[1].time < c.keys()[2].time);
    check("evaluates correctly after sort", near(c.evaluate(2.0f), 20.0f));
  }

  std::printf("\n-- wrap modes --\n");
  {
    CurveFloat c;
    c.addKey({0.0f, 0.0f, 0.0f, 0.0f});
    c.addKey({10.0f, 0.0f, 0.0f, 1.0f});

    check("loop wraps past the end", near(c.evaluate(1.5f, WrapMode::Loop), c.evaluate(0.5f), 0.01f));
    check("loop wraps a full cycle", near(c.evaluate(2.0f, WrapMode::Loop), 0.0f, 0.01f));
    check("loop handles negative time", near(c.evaluate(-0.5f, WrapMode::Loop), c.evaluate(0.5f), 0.01f));
    // Ping-pong at 1.5 is halfway back down the curve.
    check("pingpong reverses", near(c.evaluate(1.5f, WrapMode::PingPong), c.evaluate(0.5f), 0.01f));
  }

  std::printf("\n-- vec3 curve --\n");
  {
    CurveVec3 c;
    c.addKey({glm::vec3(0, 0, 0), glm::vec3(0), glm::vec3(0), 0.0f});
    c.addKey({glm::vec3(10, 20, 30), glm::vec3(0), glm::vec3(0), 1.0f});
    check("vec3 endpoints", nearVec(c.evaluate(1.0f), glm::vec3(10, 20, 30)));
    check("vec3 midpoint", nearVec(c.evaluate(0.5f), glm::vec3(5, 10, 15), 0.05f));
  }

  std::printf("\n-- quaternion curve uses slerp --\n");
  {
    CurveQuat c;
    const glm::quat q0 = glm::angleAxis(0.0f, glm::vec3(0, 1, 0));
    const glm::quat q1 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    c.addKey({q0, glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0), 0.0f});
    c.addKey({q1, glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0), 1.0f});

    const glm::quat mid = c.evaluate(0.5f);
    // Slerp keeps constant angular velocity: halfway must be exactly 45 degrees.
    const float angle = glm::degrees(2.0f * std::acos(std::clamp(mid.w, -1.0f, 1.0f)));
    check("halfway is 45 degrees", near(angle, 45.0f, 0.5f));
    check("result stays unit length", near(glm::length(mid), 1.0f, 1e-4f));

    // The short way around must be taken even when the keys are on opposite hemispheres.
    CurveQuat flipped;
    flipped.addKey({q0, glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0), 0.0f});
    flipped.addKey({-q1, glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0), 1.0f});
    const glm::quat midFlipped = flipped.evaluate(0.5f);
    const float angleFlipped = glm::degrees(2.0f * std::acos(std::clamp(std::fabs(midFlipped.w), -1.0f, 1.0f)));
    check("negated key still takes the short arc", near(angleFlipped, 45.0f, 0.5f));
  }

  std::printf("\n-- tangent generation --\n");
  {
    CurveFloat c;
    c.addKey({0.0f, 0.0f, 0.0f, 0.0f});
    c.addKey({10.0f, 0.0f, 0.0f, 1.0f});
    c.addKey({20.0f, 0.0f, 0.0f, 2.0f});
    c.buildSmoothTangents();
    // On a straight ramp the smooth fit must stay on the line, not overshoot.
    check("smooth tangents keep a linear ramp linear", near(c.evaluate(0.5f), 5.0f, 0.01f));
    check("smooth tangents preserve keys", near(c.evaluate(1.0f), 10.0f));

    c.makeLinear();
    check("makeLinear zeroes tangents", near(c.keys()[1].inTangent, 0.0f));
  }

  std::printf("\n-- skeleton --\n");
  {
    Skeleton s;
    Bone root;
    root.name = "Root";
    root.parent = -1;
    s.addBone(root);

    Bone child;
    child.name = "Child";
    child.parent = 0;
    child.localPosition = glm::vec3(0, 2, 0);
    s.addBone(child);

    Bone grandchild;
    grandchild.name = "GrandChild";
    grandchild.parent = 1;
    grandchild.localPosition = glm::vec3(0, 3, 0);
    s.addBone(grandchild);

    check("bone lookup by name", s.findBone("Child") == 1);
    check("unknown bone is -1", s.findBone("Nope") == -1);
    check("hierarchy is topologically sorted", s.isTopologicallySorted());

    auto pose = s.bindPose();
    check("bind pose size", pose.size() == 3);

    std::vector<glm::mat4> world;
    s.computeWorldMatrices(pose, world);
    // Transforms must accumulate down the chain: 0 + 2 + 3 = 5.
    const glm::vec3 gcWorld = glm::vec3(world[2][3]);
    check("child inherits parent translation", near(glm::vec3(world[1][3]).y, 2.0f));
    check("grandchild accumulates the chain", near(gcWorld.y, 5.0f));

    // Moving the root must carry the whole chain with it.
    pose.positions[0] = glm::vec3(10, 0, 0);
    s.computeWorldMatrices(pose, world);
    check("moving the root moves descendants", near(glm::vec3(world[2][3]).x, 10.0f));

    // With an identity bind pose, skinning matrices equal the world matrices.
    std::vector<glm::mat4> skin;
    s.computeSkinningMatrices(pose, skin);
    check("skinning matrices sized per bone", skin.size() == 3);
    check("identity bind pose gives world matrices", near(glm::vec3(skin[2][3]).y, 5.0f));
  }

  std::printf("\n-- clip sampling --\n");
  {
    Skeleton s;
    Bone root; root.name = "Root"; s.addBone(root);
    Bone arm; arm.name = "Arm"; arm.parent = 0; s.addBone(arm);

    AnimationClip clip;
    clip.setName("Wave");

    BoneTrack t;
    t.boneName = "Arm";
    t.position.setKeys({{glm::vec3(0, 0, 0), {}, {}, 0.0f},
                        {glm::vec3(0, 5, 0), {}, {}, 1.0f}});
    clip.addTrack(std::move(t));

    // A track for a bone this rig doesn't have must be ignored, not crash or corrupt.
    BoneTrack stray;
    stray.boneName = "NotHere";
    stray.position.setKeys({{glm::vec3(99, 99, 99), {}, {}, 0.0f}});
    clip.addTrack(std::move(stray));

    const size_t resolved = clip.resolveBones(s);
    check("only matching tracks resolve", resolved == 1);
    check("clip duration from keys", near(clip.duration(), 1.0f));

    auto pose = s.bindPose();
    clip.sample(1.0f, WrapMode::Clamp, pose);
    check("animated bone moved", nearVec(pose.positions[1], glm::vec3(0, 5, 0)));
    check("un-animated bone kept its bind value", nearVec(pose.positions[0], glm::vec3(0, 0, 0)));

    clip.sample(0.5f, WrapMode::Clamp, pose);
    check("mid-clip sample interpolates", near(pose.positions[1].y, 2.5f, 0.05f));
  }

  std::printf("\n-- blending --\n");
  {
    Pose a, b, out;
    a.resize(1);
    b.resize(1);
    a.positions[0] = glm::vec3(0, 0, 0);
    b.positions[0] = glm::vec3(10, 0, 0);
    a.rotations[0] = glm::angleAxis(0.0f, glm::vec3(0, 1, 0));
    b.rotations[0] = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

    AnimationClip::blend(a, b, 0.0f, out);
    check("weight 0 gives a", nearVec(out.positions[0], glm::vec3(0, 0, 0)));
    AnimationClip::blend(a, b, 1.0f, out);
    check("weight 1 gives b", nearVec(out.positions[0], glm::vec3(10, 0, 0)));
    AnimationClip::blend(a, b, 0.5f, out);
    check("weight 0.5 is halfway", nearVec(out.positions[0], glm::vec3(5, 0, 0)));
    check("blended rotation stays unit", near(glm::length(out.rotations[0]), 1.0f, 1e-4f));
  }

  std::printf("\n-- playback --\n");
  {
    Skeleton s;
    Bone root; root.name = "Root"; s.addBone(root);

    AnimationClip clip;
    BoneTrack t;
    t.boneName = "Root";
    t.position.setKeys({{glm::vec3(0), {}, {}, 0.0f}, {glm::vec3(0, 10, 0), {}, {}, 2.0f}});
    clip.addTrack(std::move(t));
    clip.resolveBones(s);

    AnimationPlayer p;
    check("idle player is not playing", !p.isPlaying());

    p.play(&clip, /*loop=*/false);
    check("play starts", p.isPlaying() && near(p.time(), 0.0f));

    for (int i = 0; i < 60; ++i) p.update(1.0f / 60.0f); // one second
    check("time advances with dt", near(p.time(), 1.0f, 0.02f));

    auto pose = s.bindPose();
    p.evaluate(pose);
    check("evaluated pose follows the clip", near(pose.positions[0].y, 5.0f, 0.2f));

    for (int i = 0; i < 120; ++i) p.update(1.0f / 60.0f);
    check("non-looping clip stops at the end", !p.isPlaying() && near(p.time(), 2.0f, 0.01f));

    p.play(&clip, /*loop=*/true);
    for (int i = 0; i < 300; ++i) p.update(1.0f / 60.0f); // five seconds over a 2s clip
    check("looping clip keeps playing", p.isPlaying());
    p.evaluate(pose);
    check("looped pose stays in range",
          pose.positions[0].y >= -0.1f && pose.positions[0].y <= 10.1f);

    p.setSpeed(2.0f);
    const float before = p.time();
    p.update(1.0f);
    check("speed multiplies dt", near(p.time() - before, 2.0f, 0.01f));

    p.stop();
    check("stop resets", !p.isPlaying() && near(p.time(), 0.0f));
  }

  std::printf("\n-- glTF skin + animation import --\n");
  {
    const char* tmp = std::getenv("TEMP");
    const std::string dir = tmp ? tmp : ".";
    const std::string path = fixture::writeSkinnedGltf(dir);

    std::string err;
    auto asset = tucano::loadGLTFSkinnedAsset(path, &err);
    check("skinned asset loaded (" + err + ")", asset.valid());

    if (asset.valid()) {
      check("two joints imported", asset.skeleton.boneCount() == 2);
      check("joint names preserved",
            asset.skeleton.findBone("Root") >= 0 && asset.skeleton.findBone("Child") >= 0);
      check("non-joint node excluded", asset.skeleton.findBone("Unused") == -1);
      check("parents precede children", asset.skeleton.isTopologicallySorted());

      const int child = asset.skeleton.findBone("Child");
      check("child parents to root",
            child >= 0 && asset.skeleton.bones()[size_t(child)].parent == asset.skeleton.findBone("Root"));
      check("child local translation read",
            child >= 0 && near(asset.skeleton.bones()[size_t(child)].localPosition.y, 2.0f));
      // The inverse bind pose should undo the bind translation.
      check("inverse bind matrix read",
            child >= 0 && near(asset.skeleton.bones()[size_t(child)].inverseBindPose[3][1], -2.0f));

      check("one clip imported", asset.clips.size() == 1);
      if (!asset.clips.empty() && child >= 0) {
        auto& clip = *asset.clips[0];
        check("clip name", clip.name() == "Bend");
        check("clip duration", near(clip.duration(), 1.0f));
        check("one track", clip.tracks().size() == 1);
        check("track bound to the child", clip.tracks()[0].boneName == "Child");
        check("rotation channel has 3 keys", clip.tracks()[0].rotation.size() == 3);

        clip.resolveBones(asset.skeleton);
        auto pose = asset.skeleton.bindPose();

        clip.sample(0.0f, WrapMode::Clamp, pose);
        const float a0 = glm::degrees(2.0f * std::acos(std::clamp(pose.rotations[size_t(child)].w, -1.0f, 1.0f)));
        check("t=0 is unrotated", near(a0, 0.0f, 1.0f));

        clip.sample(1.0f, WrapMode::Clamp, pose);
        const float a1 = glm::degrees(2.0f * std::acos(std::clamp(pose.rotations[size_t(child)].w, -1.0f, 1.0f)));
        check("t=1 reaches 90 degrees", near(a1, 90.0f, 1.0f));

        clip.sample(0.5f, WrapMode::Clamp, pose);
        const float a2 = glm::degrees(2.0f * std::acos(std::clamp(pose.rotations[size_t(child)].w, -1.0f, 1.0f)));
        check("t=0.5 is halfway", near(a2, 45.0f, 1.5f));

        // The point of skinning matrices: the animated bone must displace the vertices bound to it.
        // The sample point has to be off the bone's pivot — a vertex sitting exactly on the joint
        // origin (0,2,0 here) stays put under any rotation, which says nothing.
        std::vector<glm::mat4> skin;
        clip.sample(1.0f, WrapMode::Clamp, pose);
        asset.skeleton.computeSkinningMatrices(pose, skin);

        const glm::vec4 pivot = skin[size_t(child)] * glm::vec4(0, 2, 0, 1);
        check("the joint origin itself stays put",
              near(pivot.x, 0.0f, 0.01f) && near(pivot.y, 2.0f, 0.01f));

        // 2 units above the joint, rotated 90 degrees about Z, must swing out onto the X axis.
        const glm::vec4 tip = skin[size_t(child)] * glm::vec4(0, 4, 0, 1);
        check("vertex above the joint swings onto X", std::fabs(tip.x) > 1.5f);
        check("...and drops to the joint's height", near(tip.y, 2.0f, 0.05f));
      }
    }

    std::string missingErr;
    auto none = tucano::loadGLTFSkinnedAsset(dir + "/does_not_exist.gltf", &missingErr);
    check("missing file reports an error", !none.valid() && !missingErr.empty());
  }

  std::printf("\n=== failures: %d ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
