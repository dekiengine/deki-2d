#include "AnimationSystem.h"
#include "DekiLogSystem.h"
#include <algorithm>

AnimationSystem* AnimationSystem::instance = nullptr;

AnimationSystem::AnimationSystem()
{
    DEKI_LOG_INTERNAL("AnimationSystem initialized");
}

AnimationSystem::~AnimationSystem()
{
    animation_components.clear();
    DEKI_LOG_INTERNAL("AnimationSystem destroyed");
}

void AnimationSystem::RegisterAnimationComponent(AnimationComponent* animation_component)
{
    if (!animation_component)
    {
        DEKI_LOG_ERROR("AnimationSystem::RegisterAnimationComponent - null component");
        return;
    }

    // Check if already registered
    auto it = std::find(animation_components.begin(), animation_components.end(), animation_component);
    if (it != animation_components.end())
    {
        DEKI_LOG_INTERNAL("AnimationSystem::RegisterAnimationComponent - component already registered");
        return;
    }

    animation_components.push_back(animation_component);
    DEKI_LOG_INTERNAL("AnimationSystem::RegisterAnimationComponent - registered component (total: %d)",
                  static_cast<int>(animation_components.size()));
}

void AnimationSystem::UnregisterAnimationComponent(AnimationComponent* animation_component)
{
    if (!animation_component)
    {
        return;
    }

    auto it = std::find(animation_components.begin(), animation_components.end(), animation_component);
    if (it != animation_components.end())
    {
        animation_components.erase(it);
        DEKI_LOG_INTERNAL("AnimationSystem::UnregisterAnimationComponent - unregistered component (total: %d)",
                      static_cast<int>(animation_components.size()));
    }
}

void AnimationSystem::ClearAll()
{
    animation_components.clear();
}

void AnimationSystem::UpdateAnimations(uint32_t current_time)
{
    // Update all animation components
    for (auto* component : animation_components)
    {
        if (component && component->is_playing)
        {
            component->UpdateAnimation(current_time);
        }
    }
}

AnimationSystem& AnimationSystem::GetInstance()
{
    if (!instance)
    {
        instance = new AnimationSystem();
    }
    return *instance;
}