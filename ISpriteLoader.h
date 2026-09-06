#pragma once
#include "Sprite.h"

/**
 * @brief Base interface for sprite loaders
 *
 * This class defines the common interface that all sprite loaders must implement.
 * Each concrete implementation should define its own format constants.
 */
class ISpriteLoader
{
   public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~ISpriteLoader() = default;

    /**
     * @brief Load a sprite from a file path
     * @param file_path Path to the sprite file
     * @return A pointer to the created Sprite, or nullptr if loading failed
     */
    virtual Sprite* LoadFromFile(const char* file_path) = 0;

    /**
     * @brief Get the format identifier for this loader
     * @return Format string identifier
     */
    virtual const char* GetFormat() const = 0;

    /**
     * @brief Register this loader with the sprite system
     */
    virtual void RegisterLoader() = 0;
};