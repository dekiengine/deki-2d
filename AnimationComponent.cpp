#include "AnimationComponent.h"
#include "FrameAnimationMsgPack.h"
#include "Sprite.h"
#include <deki/LogSystem.h>
#include <deki/Time.h>
#include <deki/Object.h>

// ============================================================================

AnimationComponent::AnimationComponent(SpriteComponent* sprite_comp)
    : spriteComponent(sprite_comp)
    , animationData(nullptr)
    , ownsAnimationData(false)
    , currentSequence(0)
    , currentFrame(0)
    , frameStartTime(0)
    , isPlaying(false)
    , hasFinished(false)
    , playOnceOverride(false)
    , completion_callback(nullptr)
{
    SetNeedsUpdate(true);
}

AnimationComponent::~AnimationComponent()
{
    if (animationData && ownsAnimationData)
    {
        delete animationData;
    }
    animationData = nullptr;
}

void AnimationComponent::Awake()
{
    Setup();
}

void AnimationComponent::Setup()
{
    // Use animation data from AssetRef if available
    if (animation.ptr && !animationData)
    {
        animationData = animation.Get();
        ownsAnimationData = false;
    }

    // Auto-find spriteComponent on same object if not already set
    if (!spriteComponent)
    {
        Deki::Object* owner = GetOwner();
        if (owner)
        {
            spriteComponent = owner->GetComponent<SpriteComponent>();
        }
    }

    // Auto-play first animation if set
    if (animationData && spriteComponent && !animationData->animations.empty())
    {
        isPlaying = true;
        currentSequence = 0;
        currentFrame = 0;
        frameStartTime = 0;
        ApplyCurrentFrame();
    }
}

void AnimationComponent::Update()
{
    UpdateAnimation(Deki::Time::GetTime());
}

void AnimationComponent::Play(bool restart_if_playing)
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!seq || seq->frames.empty())
        return;

    if (isPlaying && !restart_if_playing)
        return;

    currentFrame = 0;
    frameStartTime = 0;
    isPlaying = true;
    hasFinished = false;
    playOnceOverride = false;

    ApplyCurrentFrame();
}

bool AnimationComponent::PlayAnimation(const char* name, bool restart_if_playing)
{
    int index = FindAnimationIndex(name);
    if (index < 0)
        return false;

    // Check if same animation is already playing
    if (currentSequence == index && isPlaying && !restart_if_playing)
        return true;

    currentSequence = index;
    currentFrame = 0;
    frameStartTime = 0;
    isPlaying = true;
    hasFinished = false;
    playOnceOverride = false;

    ApplyCurrentFrame();
    return true;
}

void AnimationComponent::PlayOnce()
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!seq || seq->frames.empty())
        return;

    currentFrame = 0;
    frameStartTime = 0;
    isPlaying = true;
    hasFinished = false;
    playOnceOverride = true;

    ApplyCurrentFrame();
}

bool AnimationComponent::PlayAnimationOnce(const char* name)
{
    int index = FindAnimationIndex(name);
    if (index < 0)
        return false;

    currentSequence = index;
    currentFrame = 0;
    frameStartTime = 0;
    isPlaying = true;
    hasFinished = false;
    playOnceOverride = true;

    ApplyCurrentFrame();
    return true;
}

bool AnimationComponent::SetAnimation(const char* name)
{
    int index = FindAnimationIndex(name);
    if (index < 0)
        return false;

    currentSequence = index;
    currentFrame = 0;
    frameStartTime = 0;
    hasFinished = false;

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
    return animationData ? static_cast<int>(animationData->animations.size()) : 0;
}

const char* AnimationComponent::GetAnimationName(int index) const
{
    if (!animationData || index < 0 || index >= static_cast<int>(animationData->animations.size()))
        return "";
    return animationData->animations[index].name.c_str();
}

void AnimationComponent::Stop()
{
    isPlaying = false;
    currentFrame = 0;
    hasFinished = false;

    ApplyCurrentFrame();
}

void AnimationComponent::Pause()
{
    isPlaying = false;
}

void AnimationComponent::Resume()
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (seq && !seq->frames.empty())
    {
        isPlaying = true;
    }
}

void AnimationComponent::UpdateAnimation(uint32_t current_time)
{
    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!isPlaying || !seq || seq->frames.empty())
        return;

    // Optimization: Don't update single-frame animations
    if (seq->frames.size() == 1)
        return;

    // Initialize frame start time on first update
    if (frameStartTime == 0)
    {
        frameStartTime = current_time;
    }

    uint32_t elapsed = current_time - frameStartTime;
    const size_t frame_count = seq->frames.size();
    bool advanced = false;

    while (elapsed >= static_cast<uint32_t>(seq->frames[currentFrame].duration))
    {
        elapsed -= static_cast<uint32_t>(seq->frames[currentFrame].duration);
        currentFrame++;
        advanced = true;

        if (currentFrame >= static_cast<int32_t>(frame_count))
        {
            if (seq->loop && !playOnceOverride)
            {
                currentFrame = 0;
            }
            else
            {
                currentFrame = static_cast<int32_t>(frame_count - 1);
                isPlaying = false;
                hasFinished = true;
                playOnceOverride = false;

                if (completion_callback)
                {
                    completion_callback();
                    completion_callback = nullptr;
                }
                elapsed = 0;
                break;
            }
        }
    }

    if (advanced)
    {
        frameStartTime = current_time - elapsed;
        ApplyCurrentFrame();
    }
}

const FrameAnimSequence* AnimationComponent::GetCurrentSequence() const
{
    if (!animationData || currentSequence < 0 ||
        currentSequence >= static_cast<int32_t>(animationData->animations.size()))
        return nullptr;
    return &animationData->animations[currentSequence];
}

int AnimationComponent::FindAnimationIndex(const char* name) const
{
    if (!animationData || !name)
        return -1;

    for (size_t i = 0; i < animationData->animations.size(); ++i)
    {
        if (animationData->animations[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

void AnimationComponent::ApplyCurrentFrame()
{
    if (!spriteComponent || !spriteComponent->sprite)
        return;

    const FrameAnimSequence* seq = GetCurrentSequence();
    if (!seq)
        return;

    if (currentFrame < 0 || currentFrame >= static_cast<int32_t>(seq->frames.size()))
        return;

    const auto& frameData = seq->frames[currentFrame];

    // Look up frame coordinates from sprite using GUID
    Sprite* sprite = spriteComponent->sprite.Get();
    if (!sprite)
    {
        DEKI_LOG_ERROR("AnimationComponent::ApplyCurrentFrame - sprite is not a Sprite");
        return;
    }

    if (m_ResolvedSprite != sprite || m_ResolvedData != animationData)
        ResolveFrames(sprite);

    const SpriteFrame* spriteFrame = nullptr;
    if (currentSequence >= 0 && currentSequence < static_cast<int32_t>(m_ResolvedFrames.size()) &&
        currentFrame < static_cast<int32_t>(m_ResolvedFrames[currentSequence].size()))
        spriteFrame = m_ResolvedFrames[currentSequence][currentFrame];
    if (!spriteFrame)
    {
        // Reported once per resolve (see ResolveFrames), not once per frame
        // advance: this runs at animation rate.
        (void)frameData;
        return;
    }

    spriteComponent->SetFrameRect(spriteFrame->x, spriteFrame->y, spriteFrame->width, spriteFrame->height);
}

void AnimationComponent::ResolveFrames(const Sprite* sprite)
{
    m_ResolvedFrames.clear();
    m_ResolvedSprite = sprite;
    m_ResolvedData = animationData;
    if (!sprite || !animationData)
        return;

    m_ResolvedFrames.resize(animationData->animations.size());
    for (size_t s = 0; s < animationData->animations.size(); ++s)
    {
        const FrameAnimSequence& seq = animationData->animations[s];
        auto& out = m_ResolvedFrames[s];
        out.resize(seq.frames.size(), nullptr);
        for (size_t f = 0; f < seq.frames.size(); ++f)
        {
            out[f] = sprite->FindFrame(seq.frames[f].frameGuid);
            if (!out[f])
                DEKI_LOG_ERROR("AnimationComponent: animation '%s' frame %zu references frame %s, which the sprite does not have",
                               seq.name.c_str(), f, seq.frames[f].frameGuid.c_str());
        }
    }
}

void AnimationComponent::InitializeToFirstFrame()
{
    if (!spriteComponent || !spriteComponent->sprite || !animationData)
        return;

    if (animationData->animations.empty())
        return;

    currentSequence = 0;
    currentFrame = 0;
    ApplyCurrentFrame();
}
