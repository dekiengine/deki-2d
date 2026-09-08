#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <deki/Component.h>
#include "SpriteComponent.h"
#include <deki/assets/AssetRef.h>
#include "FrameAnimationData.h"

/**
 * @brief Component for handling sprite frame animations
 *
 * Uses .frameanim files which reference spritesheet frames by GUID.
 */
DEKI_CATEGORY("2D")
DEKI_DESCRIPTION("Plays a frame animation asset on the object's sprite.")
class AnimationComponent : public Deki::Component
{
public:

    SpriteComponent* spriteComponent;         // Associated sprite component

    DEKI_EXPORT
    Deki::AssetRef<FrameAnimationData> animation;  // Frame animation asset reference (.frameanim)

    FrameAnimationData* animationData;        // Loaded frame animation data
    bool ownsAnimationData;                  // True if we own animationData

    DEKI_EXPORT
    int32_t currentSequence;                 // Current animation sequence index
    DEKI_EXPORT
    int32_t currentFrame;                    // Current frame index within sequence
    uint32_t frameStartTime;                // When current frame started (in ms)

    // Frame GUID -> SpriteFrame resolved once per (sprite, animation data)
    // pair; each frame change used to string-compare the GUID against every
    // frame of the sheet. Rebuilt lazily when either pointer changes.
    std::vector<std::vector<const SpriteFrame*>> m_ResolvedFrames;
    const Sprite* m_ResolvedSprite = nullptr;
    const FrameAnimationData* m_ResolvedData = nullptr;
    void ResolveFrames(const Sprite* sprite);
    DEKI_EXPORT
    bool isPlaying;                          // Whether animation is currently playing
    DEKI_EXPORT
    bool hasFinished;                        // Whether non-looping animation has finished
    DEKI_EXPORT
    bool playOnceOverride;                  // Override loop setting to play once

    std::function<void()> completion_callback; // Callback to execute when animation completes

    AnimationComponent(SpriteComponent* sprite_comp = nullptr);
    virtual ~AnimationComponent();

    // Deki::Component overrides
    void Awake() override;
    void Update() override;

    /**
     * @brief Setup the animation component after spriteComponent is linked
     */
    void Setup();

    /**
     * @brief Play the current animation
     * @param restart_if_playing Whether to restart if already playing
     */
    void Play(bool restart_if_playing = false);

    /**
     * @brief Play a specific animation by name
     * @param name Animation sequence name (e.g., "idle", "walk")
     * @param restart_if_playing Whether to restart if same animation is already playing
     * @return true if animation was found and started
     */
    bool PlayAnimation(const char* name, bool restart_if_playing = false);

    /**
     * @brief Play the current animation once (ignoring loop setting)
     */
    void PlayOnce();

    /**
     * @brief Play a specific animation once by name
     * @param name Animation sequence name
     * @return true if animation was found and started
     */
    bool PlayAnimationOnce(const char* name);

    /**
     * @brief Set current animation without playing
     * @param name Animation sequence name
     * @return true if animation was found
     */
    bool SetAnimation(const char* name);

    /**
     * @brief Get current animation sequence name
     * @return Animation name or empty string if none
     */
    const char* GetCurrentAnimationName() const;

    /**
     * @brief Get number of animation sequences
     */
    int GetAnimationCount() const;

    /**
     * @brief Get animation sequence name by index
     */
    const char* GetAnimationName(int index) const;

    /**
     * @brief Stop the animation
     */
    void Stop();

    /**
     * @brief Pause the animation
     */
    void Pause();

    /**
     * @brief Resume the animation
     */
    void Resume();

    /**
     * @brief Check if animation has finished (for non-looping animations)
     * @return true if animation has completed
     */
    bool HasFinished() const { return hasFinished; }

    /**
     * @brief Set callback to execute when animation completes
     * @param callback Function to call when animation finishes
     */
    void SetCompletionCallback(std::function<void()> callback) { completion_callback = callback; }

private:
    /**
     * @brief Get current animation sequence (or nullptr if invalid)
     */
    const FrameAnimSequence* GetCurrentSequence() const;

    /**
     * @brief Find animation sequence index by name
     * @return Index or -1 if not found
     */
    int FindAnimationIndex(const char* name) const;

    /**
     * @brief Update animation timing and advance frames
     * @param current_time Current time in milliseconds
     */
    void UpdateAnimation(uint32_t current_time);

    /**
     * @brief Apply current frame to the sprite component
     */
    void ApplyCurrentFrame();

    /**
     * @brief Initialize to first frame
     */
    void InitializeToFirstFrame();

};

// Generated property metadata (after class definition for offsetof)
