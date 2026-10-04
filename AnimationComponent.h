#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <deki/Component.h>
#include "SpriteComponent.h"
#include <deki/assets/AssetRef.h>
#include "FrameAnimationData.h"

namespace Deki2D
{

/// Plays sprite frame animations from a .anim asset, which names
/// spritesheet frames by GUID.
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Plays a frame animation asset on the object's sprite.")
DEKI_FORMER_NAME("AnimationComponent")
class AnimationComponent : public Deki::Component
{
public:
    SpriteComponent* spriteComponent;

    DEKI_EXPORT
    DEKI_TOOLTIP("A frame animation asset, which lists the frames and how long each is held.")
    Deki::AssetRef<FrameAnimationData> animation;  // .anim asset

    FrameAnimationData* animationData;  // Loaded frame animation data
    bool ownsAnimationData;             // True if this component deletes animationData

    DEKI_EXPORT
    DEKI_TOOLTIP("Which named sequence is playing, by index. Sequences are the separate animations inside one asset, "
                 "such as idle and walk.")
    int32_t currentSequence;
    DEKI_EXPORT
    DEKI_TOOLTIP("Frame within the current sequence. Set it to scrub; it is also useful to read while debugging.")
    int32_t currentFrame;     // Index within the current sequence
    uint32_t frameStartTime;  // When the current frame started, in ms

    // Frame GUID -> SpriteFrame, resolved once per (sprite, animation data)
    // pair so a frame change does not search the sheet by string. Rebuilt
    // lazily when either pointer changes.
    std::vector<std::vector<const SpriteFrame*>> resolvedFrames;
    const Sprite* resolvedSprite = nullptr;
    const FrameAnimationData* resolvedData = nullptr;
    // The asset manager's epoch at resolve time. A reimported sprite can come
    // back at the same address with new frames, which the pointers above
    // cannot tell apart; the epoch moves on every reimport.
    uint64_t resolvedEpoch = 0;
    void ResolveFrames(const Sprite* sprite);
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Whether the animation is advancing. Clearing this freezes it on the current frame rather than resetting it.")
    bool isPlaying;
    DEKI_EXPORT
    DEKI_TOOLTIP("Set when a non-looping sequence reaches its last frame. Read it to know when to move on.")
    bool hasFinished;
    DEKI_EXPORT
    DEKI_TOOLTIP(
        "Play the current sequence once even if the asset marks it as looping. Cleared when the sequence changes.")
    bool playOnceOverride;

    std::function<void()> completionCallback;  // Runs once when a non-looping play finishes

    AnimationComponent(SpriteComponent* spriteComp = nullptr);
    virtual ~AnimationComponent();

    // Deki::Component overrides
    void Awake() override;
    void Update() override;

    /// Takes the animation from the asset reference, finds the sprite
    /// component on the same object if none is set, and starts the first
    /// sequence.
    void Setup();

    /// Plays the current sequence from its first frame. When it is already
    /// playing, restarts it only if `restartIfPlaying` is true.
    void Play(bool restartIfPlaying = false);

    /// Plays the sequence called `name` (such as "idle" or "walk"). Returns
    /// false when there is no such sequence.
    bool PlayAnimation(const char* name, bool restartIfPlaying = false);

    /// Plays the current sequence once, even if it is set to loop.
    void PlayOnce();

    /// Plays the sequence called `name` once. Returns false when there is no
    /// such sequence.
    bool PlayAnimationOnce(const char* name);

    /// Selects the sequence called `name` and shows its first frame without
    /// playing it. Returns false when there is no such sequence.
    bool SetAnimation(const char* name);

    /// The current sequence's name, or an empty string when there is none.
    const char* GetCurrentAnimationName() const;

    int GetAnimationCount() const;

    /// The name of sequence `index`, or an empty string when out of range.
    const char* GetAnimationName(int index) const;

    /// Stops playing and shows the first frame.
    void Stop();

    /// Stops playing and keeps the current frame.
    void Pause();

    /// Continues from the current frame.
    void Resume();

    /// True once a non-looping sequence has reached its last frame.
    bool HasFinished() const { return hasFinished; }

    /// Sets a function to run once when a non-looping sequence finishes.
    void SetCompletionCallback(std::function<void()> callback) { completionCallback = callback; }

private:
    /// The current sequence, or nullptr when the index is invalid.
    const FrameAnimSequence* GetCurrentSequence() const;

    /// The index of the sequence called `name`, or -1.
    int FindAnimationIndex(const char* name) const;

    /// Advances frames by the time passed. `currentTime` is in milliseconds.
    void UpdateAnimation(uint32_t currentTime);

    /// Shows the current frame on the sprite component.
    void ApplyCurrentFrame();

    void InitializeToFirstFrame();
};

}  // namespace Deki2D
