#pragma once

#include <vector>
#include <cstdint>
#include "AnimationComponent.h"

/**
 * @brief System for managing and updating sprite animations
 */
class AnimationSystem
{
public:
    /**
     * @brief Constructor
     */
    AnimationSystem();

    /**
     * @brief Destructor
     */
    ~AnimationSystem();

    /**
     * @brief Register an animation component with the system
     * @param animation_component Component to register
     */
    void RegisterAnimationComponent(AnimationComponent* animation_component);

    /**
     * @brief Unregister an animation component from the system
     * @param animation_component Component to unregister
     */
    void UnregisterAnimationComponent(AnimationComponent* animation_component);

    /**
     * @brief Update all registered animation components
     * @param current_time Current time in milliseconds
     */
    void UpdateAnimations(uint32_t current_time);

    /**
     * @brief Clear all registered animation components
     * Used when stopping play mode to prevent stale pointers
     */
    void ClearAll();

    /**
     * @brief Get the singleton instance
     * @return Reference to the singleton instance
     */
    static AnimationSystem& GetInstance();

private:
    std::vector<AnimationComponent*> animation_components;
    static AnimationSystem* instance;
};