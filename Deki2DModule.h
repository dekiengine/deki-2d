#pragma once

/**
 * @file Deki2DModule.h
 * @brief Central header for the Deki 2D Graphics Module
 *
 * This header includes all components of the 2D graphics module:
 * - Sprite and texture system
 * - Animation system
 * - Gradient rendering
 * - UI components (buttons, scrolling, rollers)
 *
 * Include this single header to access all 2D graphics functionality.
 *
 * This module can be disabled by commenting out DEKI_MODULE_2D in DekiTargets.h
 * If disabled, removing the modules/2d/ folder will not break the build.
 */

// DLL export macro
#ifdef _WIN32
    #if defined(DEKI_2D_EXPORTS) || defined(DEKI_PLUGIN_EXPORTS)
        #define DEKI_2D_API __declspec(dllexport)
    #else
        #define DEKI_2D_API __declspec(dllimport)
    #endif
#else
    #define DEKI_2D_API __attribute__((visibility("default")))
#endif

// Include all module headers when module is enabled
#ifdef DEKI_MODULE_2D

// Feature-level control: when DEKI_MODULE_FEATURES_CONFIGURED is set,
// only explicitly enabled features (DEKI_FEATURE_*) are included.
// Without it, all features are included for backward compatibility.
#ifndef DEKI_MODULE_FEATURES_CONFIGURED
#define DEKI_FEATURE_SPRITE
#define DEKI_FEATURE_TEXT
#define DEKI_FEATURE_ANIMATION
#define DEKI_FEATURE_GRADIENT
#define DEKI_FEATURE_BUTTON
#define DEKI_FEATURE_SCROLL
#define DEKI_FEATURE_ROLLER
#endif

// Common types (always included)
#include "Bounds2D.h"

// Asset Types (always included - stay in deki-engine-core)
#include "Texture2D.h"
#include "Sprite.h"
#include "ISpriteLoader.h"
#include "FrameAnimationData.h"

// Components (in deki-2d.dll for editor builds)
// For editor builds, components are in the deki-2d DLL.
// When building deki-engine-core itself (DEKI_ENGINE_EXPORTS), we skip these
// to avoid WINDOWS_EXPORT_ALL_SYMBOLS exporting symbols we don't implement.
// Game code and plugins can still include this header to get all components.
#if !defined(DEKI_EDITOR) || !defined(DEKI_ENGINE_EXPORTS)
#ifdef DEKI_FEATURE_SPRITE
#include "SpriteComponent.h"
#endif
#ifdef DEKI_FEATURE_BUTTON
#include "ButtonComponent.h"
#endif
#ifdef DEKI_FEATURE_SCROLL
#include "ScrollComponent.h"
#endif
#ifdef DEKI_FEATURE_ROLLER
#include "RollerComponent.h"
#endif
#ifdef DEKI_FEATURE_ANIMATION
#include "AnimationComponent.h"
#endif
#ifdef DEKI_FEATURE_GRADIENT
#include "GradientComponent.h"
#endif
#ifdef DEKI_FEATURE_TEXT
#include "TextComponent.h"
#endif
#endif // !defined(DEKI_EDITOR) || !defined(DEKI_ENGINE_EXPORTS)

#endif // DEKI_MODULE_2D

// Editor-only: Font preview API for live editing in prefab view
// NOTE: This is outside DEKI_MODULE_2D because deki-2d.dll builds need these
// declarations regardless of whether the consuming code has DEKI_MODULE_2D defined.
#ifdef DEKI_EDITOR
#include <string>
#include <cstdint>

class BitmapFont;
struct GlyphInfo;

namespace Deki2D {
    /**
     * @brief Set a preview font from raw data (cross-DLL safe)
     *
     * This function takes raw data and allocates all objects internally within deki-2d.dll,
     * avoiding cross-DLL heap allocation issues in debug builds.
     *
     * @param sourceGuid The base font GUID (TTF/OTF)
     * @param fontSize The font size in pixels
     * @param atlasRGBA Raw RGBA pixel data for the atlas (copied, caller retains ownership)
     * @param atlasWidth Atlas width in pixels
     * @param atlasHeight Atlas height in pixels
     * @param glyphs Array of glyph info (copied, caller retains ownership)
     * @param glyphCount Number of glyphs
     * @param firstChar First character code
     * @param lastChar Last character code
     * @param lineHeight Line height in pixels
     * @param baseline Baseline offset in pixels
     * @return true on success, false on failure
     */
    DEKI_2D_API bool SetPreviewFontFromData(
        const std::string& sourceGuid,
        int fontSize,
        const uint8_t* atlasRGBA,
        uint32_t atlasWidth,
        uint32_t atlasHeight,
        const GlyphInfo* glyphs,
        size_t glyphCount,
        uint8_t firstChar,
        uint8_t lastChar,
        uint8_t lineHeight,
        uint8_t baseline
    );

    /**
     * @brief Clear the current preview font
     */
    DEKI_2D_API void ClearPreviewFont();

    /**
     * @brief Clear the preview texture cache (GPU textures)
     * Called when preview fonts are cleared to keep caches in sync
     */
    DEKI_2D_API void ClearPreviewTextureCache();

    /**
     * @brief Check if a preview font is active for the given source and size
     */
    DEKI_2D_API bool HasPreviewFont(const std::string& sourceGuid, int fontSize);

    /**
     * @brief Get preview font from cache (returns nullptr if not found)
     */
    DEKI_2D_API BitmapFont* GetPreviewFont(const std::string& sourceGuid, int fontSize);

    /**
     * @brief Initialize font preview callbacks for live editing
     * Called during Deki2D_EnsureRegistered to set up EditorAssets callbacks
     */
    DEKI_2D_API void InitializeFontPreviewCallbacks();
}
#endif // DEKI_EDITOR
