#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include "DekiBehaviour.h"
#include "SpriteComponent.h"
#include "assets/AssetRef.h"
#include "FrameAnimationData.h"

/**
 * @brief Component for handling sprite frame animations
 *
 * Uses .frameanim files which reference spritesheet frames by GUID.
 */
class AnimationSystem;  // Forward declaration for friend

class AnimationComponent : public DekiBehaviour
{
    friend class AnimationSystem;  // Allow AnimationSystem to call UpdateAnimation()
public:
    DEKI_COMPONENT(AnimationComponent, DekiBehaviour, "2D", "05f70a7a-334f-492f-9779-f3e351f64d9a", "DEKI_FEATURE_ANIMATION")

    SpriteComponent* sprite_component;         // Associated sprite component

    DEKI_EXPORT
    Deki::AssetRef<FrameAnimationData> animation;  // Frame animation asset reference (.frameanim)

    FrameAnimationData* animation_data;        // Loaded frame animation data
    bool owns_animation_data;                  // True if we own animation_data

    DEKI_EXPORT
    int32_t current_sequence;                 // Current animation sequence index
    DEKI_EXPORT
    int32_t current_frame;                    // Current frame index within sequence
    uint32_t frame_start_time;                // When current frame started (in ms)
    DEKI_EXPORT
    bool is_playing;                          // Whether animation is currently playing
    DEKI_EXPORT
    bool has_finished;                        // Whether non-looping animation has finished
    DEKI_EXPORT
    bool play_once_override;                  // Override loop setting to play once

    std::function<void()> completion_callback; // Callback to execute when animation completes

    AnimationComponent(SpriteComponent* sprite_comp = nullptr);
    virtual ~AnimationComponent();

    // DekiBehaviour overrides
    void Update() override;

    /**
     * @brief Setup the animation component after sprite_component is linked
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
    bool HasFinished() const { return has_finished; }

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
#include "generated/AnimationComponent.gen.h"
