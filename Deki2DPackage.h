#pragma once

// The one header for the 2D package: sprites and textures, animation,
// gradients, shapes, and UI widgets (buttons, scrolling, rollers, sliders and
// the rest). Its contents are included only when DEKI_PACKAGE_2D is defined.

#ifdef _WIN32
#if defined(DEKI_2D_EXPORTS) || defined(DEKI_PLUGIN_EXPORTS)
#define DEKI_2D_API __declspec(dllexport)
#else
#define DEKI_2D_API __declspec(dllimport)
#endif
#else
#define DEKI_2D_API __attribute__((visibility("default")))
#endif

#ifdef DEKI_PACKAGE_2D

// When DEKI_PACKAGE_FEATURES_CONFIGURED is set, only the features enabled
// with DEKI_FEATURE_* are included. Without it, every feature is.
#ifndef DEKI_PACKAGE_FEATURES_CONFIGURED
#define DEKI_FEATURE_SPRITE
#define DEKI_FEATURE_TEXT
#define DEKI_FEATURE_ANIMATION
#define DEKI_FEATURE_GRADIENT
#define DEKI_FEATURE_BUTTON
#define DEKI_FEATURE_SCROLL
#define DEKI_FEATURE_ROLLER
#define DEKI_FEATURE_SHAPE
#define DEKI_FEATURE_BAR
#define DEKI_FEATURE_SLIDER
#define DEKI_FEATURE_SWITCH
#define DEKI_FEATURE_CHECKBOX
#define DEKI_FEATURE_LED
#define DEKI_FEATURE_ARC
#define DEKI_FEATURE_SPINNER
#define DEKI_FEATURE_SPINBOX
#define DEKI_FEATURE_DROPDOWN
#define DEKI_FEATURE_TABVIEW
#define DEKI_FEATURE_MSGBOX
#define DEKI_FEATURE_DATE_PICKER
#endif

// Always included
#include "Bounds2D.h"

// Asset types, always included
#include <deki/assets/Texture2D.h>
#include "Sprite.h"
#include "ISpriteLoader.h"
#include "FrameAnimationData.h"

// Components. In editor builds they live in deki-2d.dll, so deki-engine-core
// itself (DEKI_ENGINE_EXPORTS) skips them: WINDOWS_EXPORT_ALL_SYMBOLS would
// otherwise export symbols it does not implement.
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
#ifdef DEKI_FEATURE_SHAPE
#include "ShapeComponent.h"
#endif
#ifdef DEKI_FEATURE_BAR
#include "BarComponent.h"
#endif
#ifdef DEKI_FEATURE_SLIDER
#include "SliderComponent.h"
#endif
#ifdef DEKI_FEATURE_SWITCH
#include "SwitchComponent.h"
#endif
#ifdef DEKI_FEATURE_CHECKBOX
#include "CheckboxComponent.h"
#endif
#ifdef DEKI_FEATURE_LED
#include "LedComponent.h"
#endif
#ifdef DEKI_FEATURE_ARC
#include "ArcComponent.h"
#endif
#ifdef DEKI_FEATURE_SPINNER
#include "SpinnerComponent.h"
#endif
#ifdef DEKI_FEATURE_SPINBOX
#include "SpinboxComponent.h"
#endif
#ifdef DEKI_FEATURE_DROPDOWN
#include "DropdownComponent.h"
#endif
#ifdef DEKI_FEATURE_TABVIEW
#include "TabviewComponent.h"
#endif
#ifdef DEKI_FEATURE_MSGBOX
#include "MsgboxComponent.h"
#endif
#ifdef DEKI_FEATURE_DATE_PICKER
#include "DatePickerComponent.h"
#endif
#endif  // !defined(DEKI_EDITOR) || !defined(DEKI_ENGINE_EXPORTS)

#endif  // DEKI_PACKAGE_2D

// Editor only: font preview for live editing in the scene view. Outside
// DEKI_PACKAGE_2D because deki-2d.dll needs these declarations whether or not
// the code including this header defines it.
#ifdef DEKI_EDITOR
#include <string>
#include <cstdint>

class BitmapFont;
struct GlyphInfo;

namespace Deki2D
{
/// Sets the preview font for a TTF/OTF font (`sourceGuid`) at `fontSize`
/// pixels. `atlasRGBA` and `glyphs` are copied; the caller keeps them. Every
/// object is allocated inside deki-2d.dll, because sharing heap allocations
/// across DLLs breaks in debug builds. Returns false on failure.
DEKI_2D_API bool SetPreviewFontFromData(const std::string& sourceGuid, int fontSize, const uint8_t* atlasRGBA,
                                        uint32_t atlasWidth, uint32_t atlasHeight, const GlyphInfo* glyphs,
                                        size_t glyphCount, uint8_t firstChar, uint8_t lastChar, uint8_t lineHeight,
                                        uint8_t baseline);

DEKI_2D_API void ClearPreviewFont();

/// Clears the cached preview GPU textures. Called when preview fonts are
/// cleared, so the two caches stay in step.
DEKI_2D_API void ClearPreviewTextureCache();

/// True when a preview font exists for this source font and size.
DEKI_2D_API bool HasPreviewFont(const std::string& sourceGuid, int fontSize);

/// The cached preview font, or nullptr.
DEKI_2D_API BitmapFont* GetPreviewFont(const std::string& sourceGuid, int fontSize);

/// Sets up the EditorAssets callbacks for live font preview. Called from
/// Deki2DEnsureRegistered.
DEKI_2D_API void InitializeFontPreviewCallbacks();
}  // namespace Deki2D
#endif  // DEKI_EDITOR
