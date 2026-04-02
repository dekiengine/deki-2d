#include "AnimationComponent.h"
#include "FrameAnimationMsgPack.h"
#include "Sprite.h"
#include "DekiLogSystem.h"
#include "DekiTime.h"
#include "DekiObject.h"

// ============================================================================

AnimationComponent::AnimationComponent(SpriteComponent* sprite_comp)
    : sprite_component(sprite_comp)
    , animation_data(nullptr)
    , owns_animation_data(false)
    , current_sequence(0)
    , current_frame(0)
    , frame_start_time(0)
    , is_playing(false)
    , has_finished(false)
    , play_once_override(false)
    , completion_callback(nullptr)
{
    SetNeedsUpdate(true);
}

AnimationComponent::~AnimationComponent()
{
    if (animation_data && owns_animation_data)
    {
        delete animation_data;
    }
    animation_data = nullptr;
}

void AnimationComponent::Setup()
{
    // Use animation data from AssetRef if available
    if (animation.ptr && !animation_data)
    {
        animation_data = animation.Get();
        owns_animation_data = false;
    }

    // Auto-find sprite_component on same object if not already set
    if (!sprite_component)
    {
        DekiObject* owner = GetOwner();
        if (owner)
        {
            sprite_component = owner->GetComponent<SpriteComponent>();
        }
    }

    // Auto-play first animation if set
    if (animation_data && sprite_component && !animation_data->animations.empty())
    {
        is_playing = true;
        current_sequence = 0;
        current_frame = 0;
        frame_start_time = 0;
        ApplyCurrentFrame();
    }
}

void AnimationComponent::Update()
{
    UpdateAnimation(DekiTime::GetTime());
}

void AnimationComponent::Play(bool restart_if_playing)
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!seq || seq->frames.empty())
        return;

    if (is_playing && !restart_if_playing)
        return;

    current_frame = 0;
    frame_start_time = 0;
    is_playing = true;
    has_finished = false;
    play_once_override = false;

    ApplyCurrentFrame();
}

bool AnimationComponent::PlayAnimation(const char* name, bool restart_if_playing)
{
    int index = FindAnimationIndex(name);
    if (index < 0)
        return false;

    // Check if same animation is already playing
    if (current_sequence == index && is_playing && !restart_if_playing)
        return true;

    current_sequence = index;
    current_frame = 0;
    frame_start_time = 0;
    is_playing = true;
    has_finished = false;
    play_once_override = false;

    ApplyCurrentFrame();
    return true;
}

void AnimationComponent::PlayOnce()
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!seq || seq->frames.empty())
        return;

    current_frame = 0;
    frame_start_time = 0;
    is_playing = true;
    has_finished = false;
    play_once_override = true;

    ApplyCurrentFrame();
}

bool AnimationComponent::PlayAnimationOnce(const char* name)
{
    int index = FindAnimationIndex(name);
    if (index < 0)
        return false;

    current_sequence = index;
    current_frame = 0;
    frame_start_time = 0;
    is_playing = true;
    has_finished = false;
    play_once_override = true;

    ApplyCurrentFrame();
    return true;
}

bool AnimationComponent::SetAnimation(const char* name)
{
    int index = FindAnimationIndex(name);
    if (index < 0)
        return false;

    current_sequence = index;
    current_frame = 0;
    frame_start_time = 0;
    has_finished = false;

    ApplyCurrentFrame();
    return true;
}

const char* AnimationComponent::GetCurrentAnimationName() const
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    return seq ? seq->name.c_str() : "";
}

int AnimationComponent::GetAnimationCount() const
{
    return animation_data ? static_cast<int>(animation_data->animations.size()) : 0;
}

const char* AnimationComponent::GetAnimationName(int index) const
{
    if (!animation_data || index < 0 || index >= static_cast<int>(animation_data->animations.size()))
        return "";
    return animation_data->animations[index].name.c_str();
}

void AnimationComponent::Stop()
{
    is_playing = false;
    current_frame = 0;
    has_finished = false;

    ApplyCurrentFrame();
}

void AnimationComponent::Pause()
{
    is_playing = false;
}

void AnimationComponent::Resume()
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (seq && !seq->frames.empty())
    {
        is_playing = true;
    }
}

void AnimationComponent::UpdateAnimation(uint32_t current_time)
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!is_playing || !seq || seq->frames.empty())
        return;

    // Optimization: Don't update single-frame animations
    if (seq->frames.size() == 1)
        return;

    // Initialize frame start time on first update
    if (frame_start_time == 0)
    {
        frame_start_time = current_time;
    }

    // Check if it's time to advance to next frame
    uint32_t elapsed = current_time - frame_start_time;
    const auto& currentFrameData = seq->frames[current_frame];

    if (elapsed >= static_cast<uint32_t>(currentFrameData.duration))
    {
        current_frame++;

        // Check if we've reached the end
        if (current_frame >= static_cast<int32_t>(seq->frames.size()))
        {
            if (seq->loop && !play_once_override)
            {
                current_frame = 0;
            }
            else
            {
                current_frame = static_cast<int32_t>(seq->frames.size() - 1);
                is_playing = false;
                has_finished = true;
                play_once_override = false;

                if (completion_callback)
                {
                    completion_callback();
                    completion_callback = nullptr;
                }
                return;
            }
        }

        frame_start_time = current_time;
        ApplyCurrentFrame();
    }
}

const FrameAnimSequence* AnimationComponent::GetCurrentSequence() const
{
    if (!animation_data || current_sequence < 0 ||
        current_sequence >= static_cast<int32_t>(animation_data->animations.size()))
        return nullptr;
    return &animation_data->animations[current_sequence];
}

int AnimationComponent::FindAnimationIndex(const char* name) const
{
    if (!animation_data || !name)
        return -1;

    for (size_t i = 0; i < animation_data->animations.size(); ++i)
    {
        if (animation_data->animations[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

void AnimationComponent::ApplyCurrentFrame()
{
    if (!sprite_component || !sprite_component->sprite)
        return;

    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!seq)
        return;

    if (current_frame < 0 || current_frame >= static_cast<int32_t>(seq->frames.size()))
        return;

    const auto& frameData = seq->frames[current_frame];

    // Look up frame coordinates from sprite using GUID
    Sprite* sprite = sprite_component->sprite.Get();
    if (!sprite)
    {
        DEKI_LOG_ERROR("AnimationComponent::ApplyCurrentFrame - sprite is not a Sprite");
        return;
    }

    const SpriteFrame* spriteFrame = sprite->FindFrame(frameData.frame_guid);
    if (!spriteFrame)
    {
        DEKI_LOG_ERROR("AnimationComponent::ApplyCurrentFrame - frame not found: %s",
                      frameData.frame_guid.c_str());
        return;
    }

    sprite_component->SetFrameRect(spriteFrame->x, spriteFrame->y, spriteFrame->width, spriteFrame->height);
}

void AnimationComponent::InitializeToFirstFrame()
{
    if (!sprite_component || !sprite_component->sprite || !animation_data)
        return;

    if (animation_data->animations.empty())
        return;

    current_sequence = 0;
    current_frame = 0;
    ApplyCurrentFrame();
}
