/**
 * @file Deki2DPackage.cpp
 * @brief Package entry point for deki-2d DLL
 *
 * This file exports the standard Deki plugin interface so the editor
 * can load deki-2d.dll and register its components.
 *
 * For linked DLLs (not dynamically loaded), Deki2D_EnsureRegistered()
 * must be called from the main executable to trigger the static initializers.
 */

#include "Deki2DPackage.h"
#include <deki/interop/Plugin.h>
#include "SpriteComponent.h"
#include "TextComponent.h"
#include "GradientComponent.h"
#include "ButtonComponent.h"
#include "ButtonStyleComponent.h"
#include "ScrollComponent.h"
#include "RollerComponent.h"
#include "AnimationComponent.h"
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/ComponentFactory.h>

#ifdef DEKI_EDITOR
#include "editor/FontSyncHandler.h"
#include "editor/FontFileInspector.h"
#include "editor/BdfFileInspector.h"
#include "editor/FontCompiler.h"
#include "BitmapFont.h"
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/EditorAssets.h>
#include <deki/Guid.h>
#include <deki/Scene.h>
#include <deki/Object.h>
#include <deki/LogSystem.h>
#include <cstdlib>
#include <functional>
#endif

#ifdef DEKI_EDITOR

// =============================================================================
// Linked DLL initialization
// =============================================================================
// When deki-2d is linked (not dynamically loaded), the editor must call
// this function to ensure the DLL code is actually loaded and the static
// initializers (REGISTER_COMPONENT) have run.

#ifndef DEKI_PLUGIN_EXPORTS
// Auto-generated registration helpers (standalone DLL only)
extern void Deki2D_RegisterComponents();
extern int Deki2D_GetAutoComponentCount();
extern const Deki::ComponentMeta* Deki2D_GetAutoComponentMeta(int index);

// Track if already registered to avoid duplicates
static bool s_Registered = false;
#endif

namespace Deki2D
{
// The editor preview font resolver, defined in editor/Deki2DEditorPreview.cpp.
BitmapFont* EditorFontResolve(TextComponent* tc);
}

extern "C" {

#ifndef DEKI_PLUGIN_EXPORTS
/**
 * @brief Ensure deki-2d package is loaded and components are registered
 *
 * Call this from the editor at startup. Simply calling this function is enough
 * to force the linker to include the DLL and trigger static initializers.
 *
 * @return Number of components registered by this package
 */
DEKI_2D_API int Deki2D_EnsureRegistered(void)
{
    if (s_Registered)
        return Deki2D_GetAutoComponentCount();
    s_Registered = true;

    // Auto-generated: registers all 2D components with ComponentRegistry + ComponentFactory
    Deki2D_RegisterComponents();

    // Clipping needs no pass: Standard2DRenderer pushes a clip rect for every
    // object that provides IClipProvider (ClipComponent) while it draws.

    // Register font-related editor features
    Deki2D::RegisterFontSyncHandlers();
    Deki2D::RegisterFontFileInspector();
    Deki2D::RegisterBdfFileInspector();

    // Initialize font preview callbacks for live editing in SceneView
    Deki2D::InitializeFontPreviewCallbacks();

    // Register font resolve callback for TextComponent (GUID sync, preview, baking)
    TextComponent::SetFontResolveCallback(Deki2D::EditorFontResolve);

    // Register image loader and font factory with EditorAssets
    DekiEditor::EditorAssets::RegisterImageLoader(Texture2D::LoadAsRGBA);
    DekiEditor::EditorAssets::RegisterFontFactory(
        // Font factory: load a BitmapFont (handles v1/v2/v3/v4) and, separately,
        // hand the editor the raw RGBA bytes of the atlas so it can upload a
        // preview texture.
        [](const char* dfontPath, uint8_t** outAtlasRGBA, int32_t& outW, int32_t& outH) -> void* {
            if (!dfontPath) return nullptr;

            BitmapFont* font = BitmapFont::Load(dfontPath);
            if (!font) return nullptr;

            const std::string& atlasAbsPath = font->GetAtlasPath();
            if (atlasAbsPath.empty()) { delete font; return nullptr; }

            int32_t atlasW = 0, atlasH = 0;
            bool hasAlpha = false;
            uint8_t* rgba = Texture2D::LoadAsRGBA(atlasAbsPath.c_str(), atlasW, atlasH, hasAlpha);
            if (!rgba) { delete font; return nullptr; }

            if (outAtlasRGBA) *outAtlasRGBA = rgba; else free(rgba);
            outW = atlasW;
            outH = atlasH;
            return font;
        },
        // Font destroyer
        [](void* f) { delete static_cast<BitmapFont*>(f); }
    );

    return Deki2D_GetAutoComponentCount();
}

#endif // DEKI_PLUGIN_EXPORTS

} // extern "C"

// =============================================================================
// Plugin metadata (for dynamic loading compatibility)
// =============================================================================

extern "C" {

#ifndef DEKI_PLUGIN_EXPORTS
DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)
{
    return "Deki 2D Package";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_PACKAGE_VERSION
    return DEKI_PACKAGE_VERSION;
#else
    return "0.0.0-dev";
#endif
}

DEKI_PLUGIN_API int DekiPlugin_Init(void)
{
    // No special initialization needed
    return 0;
}

DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)
{
    s_Registered = false;
    Deki2D::ClearPreviewTextureCache();
}

DEKI_PLUGIN_API int DekiPlugin_GetComponentCount(void)
{
    return Deki2D_GetAutoComponentCount();
}

DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return Deki2D_GetAutoComponentMeta(index);
}

DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    Deki2D_EnsureRegistered();
}

#endif // DEKI_PLUGIN_EXPORTS

// =============================================================================
// Package-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_2D_API const char* Deki2D_GetName(void)
{
    return "2D";
}

// =============================================================================
// Play Mode Hook
// =============================================================================

#ifndef DEKI_PLUGIN_EXPORTS
DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStart(void* scenePtr)
{
    if (!scenePtr)
        return;

    ::Deki::Scene* scene = static_cast<::Deki::Scene*>(scenePtr);

    // Set baked font GUIDs on TextComponents for play mode
    std::function<void(Deki::Object*)> setFontGuidsRecursive = [&](Deki::Object* obj) {
        if (!obj) return;

        TextComponent* textComp = obj->GetComponent<TextComponent>();
        if (textComp && !textComp->font.source.empty())
        {
            // Detect BDF vs TTF to use correct GUID convention
            bool isBdf = false;
            auto* pl = DekiEditor::AssetPipeline::Instance();
            const auto* fi = pl ? pl->GetAssetInfoByGuid(textComp->font.source) : nullptr;
            if (fi)
            {
                std::string ext = std::filesystem::path(fi->path).extension().string();
                for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                isBdf = (ext == ".bdf");
            }

            std::string seed = isBdf
                ? (textComp->font.source + ":bdf")
                : (textComp->font.source + ":" + std::to_string(textComp->fontSize));
            textComp->font.guid = Deki::GenerateDeterministicGuid(seed);
        }

        for (Deki::Object* child : obj->GetChildren())
        {
            setFontGuidsRecursive(child);
        }
    };

    for (Deki::Object* obj : scene->GetObjects())
    {
        setFontGuidsRecursive(obj);
    }
}

DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStop(void)
{
    // Nothing to reset: AnimationComponent drives itself from Update().
}

// =============================================================================
// Font Compilation Wrappers (for AssetExporter via function pointers)
//
// Uses opaque handle pattern so the editor never needs FontCompiler headers.
// Flow: CompileFont → GetCompiledFontAtlas → WriteDfont → FreeCompileResult
// =============================================================================

DEKI_PLUGIN_API void* DekiPlugin_CompileFont(const char* ttfPath, int fontSize,
                                              int firstChar, int lastChar, int padding, int maxAtlas)
{
    if (!ttfPath)
        return nullptr;

    auto* result = new Deki2D::FontCompiler::CompileResult();
    Deki2D::FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = firstChar;
    options.lastChar = lastChar;
    options.padding = padding;
    options.maxAtlasSize = maxAtlas;

    if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, *result))
    {
        delete result;
        return nullptr;
    }

    return result;
}

DEKI_PLUGIN_API bool DekiPlugin_GetCompiledFontAtlas(const void* handle,
                                                      const uint8_t** atlasData, int* width, int* height)
{
    if (!handle || !atlasData || !width || !height)
        return false;

    const auto* result = static_cast<const Deki2D::FontCompiler::CompileResult*>(handle);
    *atlasData = result->atlasRGBA.data();
    *width = result->atlasWidth;
    *height = result->atlasHeight;
    return true;
}

DEKI_PLUGIN_API bool DekiPlugin_WriteDfont(const char* path, const void* handle, const char* atlasGuid)
{
    if (!path || !handle || !atlasGuid)
        return false;

    const auto* result = static_cast<const Deki2D::FontCompiler::CompileResult*>(handle);
    return Deki2D::FontCompiler::WriteDfontFile(path, *result, atlasGuid);
}

DEKI_PLUGIN_API void DekiPlugin_FreeCompileResult(void* handle)
{
    delete static_cast<Deki2D::FontCompiler::CompileResult*>(handle);
}

#endif // DEKI_PLUGIN_EXPORTS

} // extern "C"

#endif // DEKI_EDITOR
