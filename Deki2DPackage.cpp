// Entry point of deki-2d.dll: exports the standard Deki plugin interface so
// the editor can load the DLL and register its components.
//
// When the DLL is linked rather than loaded at runtime, the main executable
// must call Deki2DEnsureRegistered() so the static initializers run.

#include "Deki2DPackage.h"
#include "Deki2DInit.h"
#include <deki/interop/Plugin.h>
// The editor's font sync below uses TextComponent and other components. A
// device build takes its components from Deki2DPackage.h, feature by feature,
// so a stripped build names only what it compiled.
#ifdef DEKI_EDITOR
#include "SpriteComponent.h"
#include "TextComponent.h"
#include "GradientComponent.h"
#include "ButtonComponent.h"
#include "ButtonStyleComponent.h"
#include "ScrollComponent.h"
#include "RollerComponent.h"
#include "AnimationComponent.h"
#endif
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

// Linked DLL initialization: when deki-2d is linked rather than loaded at
// runtime, the editor calls Deki2DEnsureRegistered() so the DLL is loaded and
// its static initializers have run.

#ifndef DEKI_PLUGIN_EXPORTS
// Generated registration helpers (standalone DLL only)
extern void Deki2DRegisterComponents();
extern int Deki2DGetAutoComponentCount();
extern const Deki::ComponentMeta* Deki2DGetAutoComponentMeta(int index);

static bool s_Registered = false;
#endif

namespace Deki2D
{
// The editor preview font resolver, defined in editor/Deki2DEditorPreview.cpp.
BitmapFont* EditorFontResolve(TextComponent* tc);
}  // namespace Deki2D

// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace Deki2D;

extern "C"
{
#ifndef DEKI_PLUGIN_EXPORTS
    /// Makes sure the package is loaded and its components are registered.
    /// The editor calls it at startup; the call alone makes the linker keep
    /// the DLL and run its static initializers. Returns the number of
    /// components the package registers.
    DEKI_2D_API int Deki2DEnsureRegistered(void)
    {
        if (s_Registered)
        {
            return ::Deki2DGetAutoComponentCount();
        }
        s_Registered = true;

        // Generated: registers every 2D component with ComponentRegistry and ComponentFactory
        ::Deki2DRegisterComponents();

        // Clipping needs no pass: DekiRendering::Standard2DRenderer pushes a clip rect for every
        // object that provides IClipProvider (Deki2D::ClipComponent) while it draws.

        Deki2D::RegisterFontSyncHandlers();
        Deki2D::RegisterFontFileInspector();
        Deki2D::RegisterBdfFileInspector();

        Deki2D::InitializeFontPreviewCallbacks();

        // Font resolving for TextComponent: GUID sync, preview, baking
        Deki2D::TextComponent::SetFontResolveCallback(Deki2D::EditorFontResolve);

        DekiEditor::EditorAssets::RegisterImageLoader(Deki::Texture2D::LoadAsRGBA);
        DekiEditor::EditorAssets::RegisterFontFactory(
            // Loads a BitmapFont (any .dfont version) and also hands the editor
            // the atlas's raw RGBA bytes, so it can upload a preview texture.
            [](const char* dfontPath, uint8_t** outAtlasRGBA, int32_t& outW, int32_t& outH) -> void*
            {
                if (!dfontPath)
                {
                    return nullptr;
                }

                Deki2D::BitmapFont* font = Deki2D::BitmapFont::Load(dfontPath);
                if (!font)
                {
                    return nullptr;
                }

                const std::string& atlasAbsPath = font->GetAtlasPath();
                if (atlasAbsPath.empty())
                {
                    delete font;
                    return nullptr;
                }

                int32_t atlasW = 0, atlasH = 0;
                bool hasAlpha = false;
                uint8_t* rgba = Deki::Texture2D::LoadAsRGBA(atlasAbsPath.c_str(), atlasW, atlasH, hasAlpha);
                if (!rgba)
                {
                    delete font;
                    return nullptr;
                }

                if (outAtlasRGBA)
                {
                    *outAtlasRGBA = rgba;
                }
                else
                {
                    free(rgba);
                }
                outW = atlasW;
                outH = atlasH;
                return font;
            },
            [](void* f) { delete static_cast<Deki2D::BitmapFont*>(f); });

        return ::Deki2DGetAutoComponentCount();
    }

#endif  // DEKI_PLUGIN_EXPORTS

}  // extern "C"

// Plugin metadata, for loading at runtime

extern "C"
{
#ifndef DEKI_PLUGIN_EXPORTS
    DEKI_PLUGIN_API const char* DekiPluginGetName(void)
    {
        return "Deki 2D Package";
    }

    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void)
    {
#ifdef DEKI_PACKAGE_VERSION
        return DEKI_PACKAGE_VERSION;
#else
        return "0.0.0-dev";
#endif
    }

    DEKI_PLUGIN_API int DekiPluginInit(void)
    {
        Deki2DInitSystem();
        return 0;
    }

    DEKI_PLUGIN_API void DekiPluginShutdown(void)
    {
        s_Registered = false;
        Deki2D::ClearPreviewTextureCache();
    }

    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void)
    {
        return ::Deki2DGetAutoComponentCount();
    }

    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index)
    {
        return ::Deki2DGetAutoComponentMeta(index);
    }

    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void)
    {
        Deki2DEnsureRegistered();
    }

#endif  // DEKI_PLUGIN_EXPORTS

    // Package-specific API, named so a linked DLL does not clash with other packages

    DEKI_2D_API const char* Deki2DGetName(void)
    {
        return "2D";
    }

    // Play mode hooks

#ifndef DEKI_PLUGIN_EXPORTS
    DEKI_PLUGIN_API void DekiPluginOnPlayModeStart(void* scenePtr)
    {
        if (!scenePtr)
        {
            return;
        }

        ::Deki::Scene* scene = static_cast<::Deki::Scene*>(scenePtr);

        // Point every TextComponent at its baked font's GUID for play mode
        std::function<void(Deki::Object*)> setFontGuidsRecursive = [&](Deki::Object* obj)
        {
            if (!obj)
            {
                return;
            }

            Deki2D::TextComponent* textComp = obj->GetComponent<Deki2D::TextComponent>();
            if (textComp && !textComp->font.source.empty())
            {
                // BDF and TTF fonts derive the baked GUID differently
                bool isBdf = false;
                auto* pl = DekiEditor::AssetPipeline::Instance();
                const auto* fi = pl ? pl->GetAssetInfoByGuid(textComp->font.source) : nullptr;
                if (fi)
                {
                    std::string ext = std::filesystem::path(fi->path).extension().string();
                    for (char& c : ext)
                    {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    isBdf = (ext == ".bdf");
                }

                std::string seed = isBdf ? (textComp->font.source + ":bdf")
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

    DEKI_PLUGIN_API void DekiPluginOnPlayModeStop(void)
    {
        // Nothing to reset: Deki2D::AnimationComponent drives itself from Update().
    }

    // Font compilation for AssetExporter, called through function pointers.
    // The result is an opaque handle, so the editor never needs FontCompiler
    // headers. Order: CompileFont, GetCompiledFontAtlas, WriteDfont,
    // FreeCompileResult.

    DEKI_PLUGIN_API void* DekiPluginCompileFont(const char* ttfPath, int fontSize, int firstChar, int lastChar,
                                                int padding, int maxAtlas)
    {
        if (!ttfPath)
        {
            return nullptr;
        }

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

    DEKI_PLUGIN_API bool DekiPluginGetCompiledFontAtlas(const void* handle, const uint8_t** atlasData, int* width,
                                                        int* height)
    {
        if (!handle || !atlasData || !width || !height)
        {
            return false;
        }

        const auto* result = static_cast<const Deki2D::FontCompiler::CompileResult*>(handle);
        *atlasData = result->atlasRGBA.data();
        *width = result->atlasWidth;
        *height = result->atlasHeight;
        return true;
    }

    DEKI_PLUGIN_API bool DekiPluginWriteDfont(const char* path, const void* handle, const char* atlasGuid)
    {
        if (!path || !handle || !atlasGuid)
        {
            return false;
        }

        const auto* result = static_cast<const Deki2D::FontCompiler::CompileResult*>(handle);
        return Deki2D::FontCompiler::WriteDfontFile(path, *result, atlasGuid);
    }

    DEKI_PLUGIN_API void DekiPluginFreeCompileResult(void* handle)
    {
        delete static_cast<Deki2D::FontCompiler::CompileResult*>(handle);
    }

#endif  // DEKI_PLUGIN_EXPORTS

}  // extern "C"

#endif  // DEKI_EDITOR
