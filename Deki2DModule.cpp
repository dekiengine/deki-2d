/**
 * @file Deki2DModule.cpp
 * @brief Module entry point for deki-2d DLL
 *
 * This file exports the standard Deki plugin interface so the editor
 * can load deki-2d.dll and register its components.
 *
 * For linked DLLs (not dynamically loaded), Deki2D_EnsureRegistered()
 * must be called from the main executable to trigger the static initializers.
 */

#include "Deki2DModule.h"
#include "interop/DekiPlugin.h"
#include "DekiModuleFeatureMeta.h"
#include "SpriteComponent.h"
#include "TextComponent.h"
#include "GradientComponent.h"
#include "ButtonComponent.h"
#include "ButtonStyleComponent.h"
#include "ScrollComponent.h"
#include "RollerComponent.h"
#include "AnimationComponent.h"
#include "AnimationSystem.h"
#include "reflection/ComponentRegistry.h"
#include "reflection/ComponentFactory.h"

#ifdef DEKI_EDITOR
#include "editor/FontSyncHandler.h"
#include "editor/FontFileInspector.h"
#include <cstdlib>
#include "editor/BdfFileInspector.h"
#include "editor/FontCompiler.h"
#include "Texture2D.h"
#include "BitmapFont.h"
#include "imgui.h"
#include <deki-editor/EditorAssets.h>
#include <deki-editor/AssetPipeline.h>
#include "Guid.h"
#include "Prefab.h"
#include "DekiObject.h"
#include "DekiEngine.h"
#include "DekiLogSystem.h"
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>

// OpenGL for preview texture upload
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
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
extern const DekiComponentMeta* Deki2D_GetAutoComponentMeta(int index);

// Track if already registered to avoid duplicates
static bool s_Registered = false;

// Forward declaration — defined after extern "C" block
static BitmapFont* EditorFontResolve(TextComponent* tc);
#endif

extern "C" {

#ifndef DEKI_PLUGIN_EXPORTS
/**
 * @brief Set ImGui context for cross-DLL ImGui usage
 * Must be called from main exe before any ImGui functions are used in this DLL
 */
DEKI_2D_API void Deki2D_SetImGuiContext(void* ctx)
{
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
}

/**
 * @brief Ensure deki-2d module is loaded and components are registered
 *
 * Call this from the editor at startup. Simply calling this function is enough
 * to force the linker to include the DLL and trigger static initializers.
 *
 * @return Number of components registered by this module
 */
DEKI_2D_API int Deki2D_EnsureRegistered(void)
{
    if (s_Registered)
        return Deki2D_GetAutoComponentCount();
    s_Registered = true;

    // Auto-generated: registers all 2D components with ComponentRegistry + ComponentFactory
    Deki2D_RegisterComponents();

    // Note: Clip2DRenderPass is now self-registered via DekiRenderPassRegistry
    // and activated by the render pipeline configuration (.rpipeline asset).
    // No manual AddPass/AddSortingCallback needed here.

    // Register font-related editor features
    Deki2D::RegisterFontSyncHandlers();
    Deki2D::RegisterFontFileInspector();
    Deki2D::RegisterBdfFileInspector();

    // Initialize font preview callbacks for live editing in PrefabView
    Deki2D::InitializeFontPreviewCallbacks();

    // Register font resolve callback for TextComponent (GUID sync, preview, baking)
    TextComponent::SetFontResolveCallback(EditorFontResolve);

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
    return "Deki 2D Module";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_MODULE_VERSION
    return DEKI_MODULE_VERSION;
#else
    return "0.0.0-dev";
#endif
}

DEKI_PLUGIN_API const char* DekiPlugin_GetReflectionJson(void)
{
    // Not used - we use component metadata instead
    return "{}";
}

DEKI_PLUGIN_API int DekiPlugin_Init(void)
{
    // No special initialization needed
    return 0;
}

DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)
{
    // Note: Clip2DRenderPass lifecycle is now managed by DekiRendering_ShutdownSystem()
    // via the render pipeline configuration. No manual RemovePass needed here.

    s_Registered = false;
    Deki2D::ClearPreviewTextureCache();
}

/**
 * @brief Standardized SetImGuiContext for dynamic module loading
 * Called by ModuleLoader after LoadLibrary to set ImGui context across DLL boundary
 */
DEKI_PLUGIN_API void DekiPlugin_SetImGuiContext(void* ctx)
{
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
}

DEKI_PLUGIN_API int DekiPlugin_GetComponentCount(void)
{
    return Deki2D_GetAutoComponentCount();
}

DEKI_PLUGIN_API const DekiComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return Deki2D_GetAutoComponentMeta(index);
}

DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    Deki2D_EnsureRegistered();
}

#endif // DEKI_PLUGIN_EXPORTS (resume after feature data)

// =============================================================================
// Module Feature API
// =============================================================================

// Component GUID arrays per feature (referenced from component StaticGuid)
static const char* s_SpriteGuids[]    = { SpriteComponent::StaticGuid };
static const char* s_TextGuids[]      = { TextComponent::StaticGuid };
static const char* s_AnimationGuids[] = { AnimationComponent::StaticGuid };
static const char* s_GradientGuids[]  = { GradientComponent::StaticGuid };
static const char* s_ButtonGuids[]    = { ButtonComponent::StaticGuid, ButtonStyleComponent::StaticGuid };
static const char* s_ScrollGuids[]    = { ScrollComponent::StaticGuid };
static const char* s_RollerGuids[]    = { RollerComponent::StaticGuid };

static const DekiModuleFeatureInfo s_Features[] = {
    {"sprite",    "Sprites",   "Sprite rendering from textures",      true, "DEKI_FEATURE_SPRITE",    s_SpriteGuids,    1},
    {"text",      "Text",      "Text rendering with bitmap fonts",    true, "DEKI_FEATURE_TEXT",      s_TextGuids,      1},
    {"animation", "Animation", "Frame-based sprite animation",        true, "DEKI_FEATURE_ANIMATION", s_AnimationGuids, 1},
    {"gradient",  "Gradients", "Gradient fill rendering",             true, "DEKI_FEATURE_GRADIENT",  s_GradientGuids,  1},
    {"button",    "Buttons",   "Interactive button components",       true, "DEKI_FEATURE_BUTTON",    s_ButtonGuids,    2},
    {"scroll",    "Scrolling", "Scrollable container components",     true, "DEKI_FEATURE_SCROLL",    s_ScrollGuids,    1},
    {"roller",    "Rollers",   "Roller/picker UI components",         true, "DEKI_FEATURE_ROLLER",    s_RollerGuids,    1},
};

#ifndef DEKI_PLUGIN_EXPORTS
DEKI_PLUGIN_API int DekiPlugin_GetFeatureCount(void)
{
    return sizeof(s_Features) / sizeof(s_Features[0]);
}

DEKI_PLUGIN_API const DekiModuleFeatureInfo* DekiPlugin_GetFeature(int index)
{
    if (index < 0 || index >= DekiPlugin_GetFeatureCount())
        return nullptr;
    return &s_Features[index];
}

#endif // DEKI_PLUGIN_EXPORTS

// =============================================================================
// Module-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_2D_API const char* Deki2D_GetName(void)
{
    return "2D";
}

DEKI_2D_API int Deki2D_GetFeatureCount(void)
{
    return static_cast<int>(sizeof(s_Features) / sizeof(s_Features[0]));
}

DEKI_2D_API const DekiModuleFeatureInfo* Deki2D_GetFeature(int index)
{
    if (index < 0 || index >= Deki2D_GetFeatureCount())
        return nullptr;
    return &s_Features[index];
}

// =============================================================================
// Play Mode Hook
// =============================================================================

#ifndef DEKI_PLUGIN_EXPORTS
DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStart(void* prefabPtr)
{
    if (!prefabPtr)
        return;

    ::Prefab* prefab = static_cast<::Prefab*>(prefabPtr);

    // Set baked font GUIDs on TextComponents for play mode
    std::function<void(DekiObject*)> setFontGuidsRecursive = [&](DekiObject* obj) {
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

        for (DekiObject* child : obj->GetChildren())
        {
            setFontGuidsRecursive(child);
        }
    };

    for (DekiObject* obj : prefab->GetObjects())
    {
        setFontGuidsRecursive(obj);
    }
}

DEKI_PLUGIN_API void DekiPlugin_OnPlayModeStop(void)
{
    AnimationSystem::GetInstance().ClearAll();
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

// =============================================================================
// Preview Font Management (moved from TextComponent.cpp for clean separation)
// =============================================================================

namespace {
    static BitmapFont* s_PreviewFont = nullptr;
    static std::string s_PreviewFontGuid;
    static int s_PreviewFontSize = 0;
} // anonymous namespace

static BitmapFont* GetEditorFontVariant(const std::string& fontGuid, int fontSize)
{
    if (fontGuid.empty() || fontSize <= 0)
        return nullptr;

    if (s_PreviewFont && s_PreviewFontGuid == fontGuid && s_PreviewFontSize == fontSize)
        return s_PreviewFont;

    if (s_PreviewFont)
    {
        delete s_PreviewFont;
        s_PreviewFont = nullptr;
        s_PreviewFontGuid.clear();
        s_PreviewFontSize = 0;
    }

    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline) return nullptr;
    const DekiEditor::AssetInfo* info = pipeline->GetAssetInfoByGuid(fontGuid);
    if (!info) return nullptr;
    std::string ttfPath = (std::filesystem::path(pipeline->GetProjectPath()) / info->path).string();
    if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
        return nullptr;

    Deki2D::FontCompiler::CompileOptions options;
    options.fontSize = fontSize;
    options.firstChar = 32;
    options.lastChar = 126;
    options.padding = 2;

    Deki2D::FontCompiler::CompileResult result;
    if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, result))
        return nullptr;

    Texture2D* atlas = new Texture2D();
    atlas->width = result.atlasWidth;
    atlas->height = result.atlasHeight;
    atlas->format = Texture2D::TextureFormat::RGBA8888;
    atlas->has_alpha = true;
    atlas->has_transparency = true;

    size_t atlasSize = result.atlasWidth * result.atlasHeight * 4;
    if (DekiMemoryProvider::IsInitialized())
    {
        atlas->data = static_cast<uint8_t*>(DekiMemoryProvider::Allocate(atlasSize, false, "FontPreviewAtlas"));
        atlas->allocated_with_backend = true;
    }
    else
    {
        atlas->data = static_cast<uint8_t*>(std::malloc(atlasSize));
    }

    if (!atlas->data) { delete atlas; return nullptr; }
    memcpy(atlas->data, result.atlasRGBA.data(), atlasSize);

    GlyphInfo* glyphsCopy = new GlyphInfo[result.glyphs.size()];
    memcpy(glyphsCopy, result.glyphs.data(), result.glyphs.size() * sizeof(GlyphInfo));

    BitmapFont* font = BitmapFont::CreateFromMemory(
        atlas, glyphsCopy,
        result.firstChar, result.lastChar,
        result.lineHeight, result.baseline);

    if (font)
    {
        s_PreviewFont = font;
        s_PreviewFontGuid = fontGuid;
        s_PreviewFontSize = fontSize;
    }
    return font;
}

static bool IsBdfFont(const std::string& sourceGuid)
{
    auto* pl = DekiEditor::AssetPipeline::Instance();
    const auto* fi = pl ? pl->GetAssetInfoByGuid(sourceGuid) : nullptr;
    if (!fi) return false;
    std::string ext = std::filesystem::path(fi->path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (ext == ".bdf");
}

static std::string ComputeBakedFontGuid(const std::string& sourceGuid, int fontSize)
{
    std::string seed = IsBdfFont(sourceGuid)
        ? (sourceGuid + ":bdf")
        : (sourceGuid + ":" + std::to_string(fontSize));
    return Deki::GenerateDeterministicGuid(seed);
}

static BitmapFont* EditorFontResolve(TextComponent* tc)
{
    if (DekiEngine::IsRuntimeMode())
    {
        // Play mode: update GUID if fontSize changed (e.g., by RollerComponent)
        if (!tc->font.source.empty() && tc->fontSize > 0)
        {
            std::string expectedGuid = ComputeBakedFontGuid(tc->font.source, tc->fontSize);
            if (tc->font.guid != expectedGuid)
            {
                tc->font.guid = expectedGuid;
                tc->font.ptr = nullptr;
                tc->font.loadAttempted = false;
            }
        }
        return nullptr;
    }

    // Edit mode: preview takes priority
    if (tc->previewEnabled && tc->previewSize > 0)
        return GetEditorFontVariant(tc->font.source, tc->previewSize);

    // Edit mode: GUID sync with font baking
    if (!tc->font.source.empty() && tc->fontSize > 0)
    {
        std::string expectedGuid = ComputeBakedFontGuid(tc->font.source, tc->fontSize);
        if (tc->font.guid != expectedGuid)
        {
            tc->font.guid = expectedGuid;
            tc->font.ptr = nullptr;
            tc->font.loadAttempted = false;
            Deki2D::EnsureFontSizeBaked(tc->font.source, tc->fontSize);
        }
    }

    tc->fontSizeUnavailable = (tc->font.Get() == nullptr
                                && !tc->font.source.empty() && tc->fontSize > 0);
    return nullptr;
}

namespace Deki2D {

// Forward declaration — defined in Font Preview Callbacks section below
void ClearPreviewTextureCache();

void ClearPreviewFont()
{
    if (s_PreviewFont)
    {
        delete s_PreviewFont;
        s_PreviewFont = nullptr;
    }
    s_PreviewFontGuid.clear();
    s_PreviewFontSize = 0;
    ClearPreviewTextureCache();
}

bool SetPreviewFontFromData(
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
    uint8_t baseline)
{
    if (!atlasRGBA || !glyphs || glyphCount == 0 || atlasWidth == 0 || atlasHeight == 0)
        return false;

    if (s_PreviewFontGuid != sourceGuid || s_PreviewFontSize != fontSize)
        ClearPreviewFont();

    Texture2D* atlas = new Texture2D();
    atlas->width = atlasWidth;
    atlas->height = atlasHeight;
    atlas->format = Texture2D::TextureFormat::RGBA8888;
    atlas->has_alpha = true;
    atlas->has_transparency = true;

    size_t atlasSize = atlasWidth * atlasHeight * 4;
    if (DekiMemoryProvider::IsInitialized())
    {
        atlas->data = static_cast<uint8_t*>(DekiMemoryProvider::Allocate(atlasSize, false, "FontPreviewAtlas"));
        atlas->allocated_with_backend = true;
    }
    else
    {
        atlas->data = static_cast<uint8_t*>(std::malloc(atlasSize));
    }

    if (!atlas->data) { delete atlas; return false; }
    memcpy(atlas->data, atlasRGBA, atlasSize);

    GlyphInfo* glyphsCopy = new GlyphInfo[glyphCount];
    memcpy(glyphsCopy, glyphs, glyphCount * sizeof(GlyphInfo));

    BitmapFont* font = BitmapFont::CreateFromMemory(
        atlas, glyphsCopy,
        firstChar, lastChar,
        lineHeight, baseline);

    if (!font) return false;

    s_PreviewFont = font;
    s_PreviewFontGuid = sourceGuid;
    s_PreviewFontSize = fontSize;
    return true;
}

bool HasPreviewFont(const std::string& sourceGuid, int fontSize)
{
    return s_PreviewFont && s_PreviewFontGuid == sourceGuid && s_PreviewFontSize == fontSize;
}

BitmapFont* GetPreviewFont(const std::string& sourceGuid, int fontSize)
{
    if (s_PreviewFont && s_PreviewFontGuid == sourceGuid && s_PreviewFontSize == fontSize)
        return s_PreviewFont;
    return nullptr;
}

} // namespace Deki2D

// =============================================================================
// Font Preview Callbacks
// =============================================================================

namespace {
    // Cache for preview font GPU textures (keyed by "preview:sourceGuid:fontSize")
    struct PreviewFontTexture
    {
        uint32_t textureId = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    static std::unordered_map<std::string, PreviewFontTexture> s_PreviewTextureCache;

    /**
     * @brief Read the variant GUID from the font's .data sidecar file
     * @param sourceGuid The source font GUID (TTF/OTF file)
     * @param fontSize The font size to look up
     * @return The variant GUID if found, empty string otherwise
     */
    static std::string GetVariantGuidFromData(const std::string& sourceGuid, int fontSize)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        if (!pipeline)
            return "";

        const DekiEditor::AssetInfo* fontInfo = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!fontInfo)
            return "";

        namespace fs = std::filesystem;
        std::string fontPath = (fs::path(pipeline->GetProjectPath()) / fontInfo->path).string();
        std::string dataPath = fontPath + ".data";

        if (!fs::exists(dataPath))
            return "";

        std::ifstream file(dataPath);
        if (!file.is_open())
            return "";

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);
            std::string sizeKey = std::to_string(fontSize);

            if (j.contains("variants") &&
                j["variants"].contains(sizeKey) &&
                j["variants"][sizeKey].contains("guid"))
            {
                return j["variants"][sizeKey]["guid"].get<std::string>();
            }
        }
        catch (...)
        {
            // Parse error
        }

        return "";
    }

    static std::string ResolveTTFPath(const std::string& sourceGuid)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        if (!pipeline)
            return "";

        const DekiEditor::AssetInfo* info = pipeline->GetAssetInfoByGuid(sourceGuid);
        if (!info)
            return "";

        namespace fs = std::filesystem;
        return (fs::path(pipeline->GetProjectPath()) / info->path).string();
    }

    static uint32_t UploadTextureToGPU(const uint8_t* rgba, uint32_t width, uint32_t height)
    {
        if (!rgba || width == 0 || height == 0)
            return 0;

        GLuint texId = 0;
        glGenTextures(1, &texId);
        if (texId == 0)
            return 0;

        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        return static_cast<uint32_t>(texId);
    }
} // anonymous namespace

namespace Deki2D {

void InitializeFontPreviewCallbacks()
{
    DekiEditor::EditorAssets::Get()->SetFontBakingCallbacks(
        // Font callback - returns BitmapFont for text metrics
        [](const std::string& sourceGuid, int fontSize) -> BitmapFont* {
            DEKI_LOG_EDITOR("FontPreview: GetBitmapFontWithBaking called for %s @ %d px", sourceGuid.c_str(), fontSize);

            if (sourceGuid.empty() || fontSize <= 0)
                return nullptr;

            // 1. Check if already baked to disk - use actual variant GUID from .data file
            std::string variantGuid = GetVariantGuidFromData(sourceGuid, fontSize);
            if (!variantGuid.empty())
            {
                BitmapFont* baked = DekiEditor::EditorAssets::Get()->GetBitmapFont(variantGuid);
                if (baked)
                {
                    DEKI_LOG_EDITOR("FontPreview: Found baked font for %s @ %d px (variant %s)", sourceGuid.c_str(), fontSize, variantGuid.c_str());
                    return baked;
                }
            }

            // 2. Check if preview font already exists AND has GPU texture
            // Use "preview:" prefix to keep separate from baked font GUIDs
            std::string cacheKey = "preview:" + sourceGuid + ":" + std::to_string(fontSize);
            BitmapFont* preview = Deki2D::GetPreviewFont(sourceGuid, fontSize);
            if (preview)
            {
                // Make sure GPU texture exists too
                auto texIt = s_PreviewTextureCache.find(cacheKey);
                if (texIt != s_PreviewTextureCache.end() && texIt->second.textureId != 0)
                {
                    DEKI_LOG_EDITOR("FontPreview: Found preview font for %s @ %d px", sourceGuid.c_str(), fontSize);
                    return preview;
                }
                // Font exists but no texture - need to recompile to get texture
                DEKI_LOG_EDITOR("FontPreview: Preview font exists but no GPU texture, recompiling %s @ %d px", sourceGuid.c_str(), fontSize);
            }

            // 3. Compile font on-demand
            std::string ttfPath = ResolveTTFPath(sourceGuid);
            if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
            {
                DEKI_LOG_WARNING("FontPreview: TTF not found for %s", sourceGuid.c_str());
                return nullptr;
            }

            DEKI_LOG_EDITOR("FontPreview: Compiling %s @ %d px", sourceGuid.c_str(), fontSize);

            Deki2D::FontCompiler::CompileOptions options;
            options.fontSize = fontSize;
            options.firstChar = 32;
            options.lastChar = 126;
            options.padding = 2;

            Deki2D::FontCompiler::CompileResult result;
            if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, result))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to compile %s", ttfPath.c_str());
                return nullptr;
            }

            // 4. Create preview font in engine cache
            if (!Deki2D::SetPreviewFontFromData(
                    sourceGuid, fontSize,
                    result.atlasRGBA.data(), result.atlasWidth, result.atlasHeight,
                    result.glyphs.data(), result.glyphs.size(),
                    result.firstChar, result.lastChar,
                    result.lineHeight, result.baseline))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to set preview font data");
                return nullptr;
            }

            // 5. Upload atlas to GPU for editor rendering
            // (cacheKey already defined above)

            // Delete old texture if exists
            auto it = s_PreviewTextureCache.find(cacheKey);
            if (it != s_PreviewTextureCache.end() && it->second.textureId != 0)
            {
                GLuint texId = static_cast<GLuint>(it->second.textureId);
                glDeleteTextures(1, &texId);
            }

            uint32_t texId = UploadTextureToGPU(result.atlasRGBA.data(), result.atlasWidth, result.atlasHeight);
            if (texId != 0)
            {
                PreviewFontTexture cached;
                cached.textureId = texId;
                cached.width = result.atlasWidth;
                cached.height = result.atlasHeight;
                s_PreviewTextureCache[cacheKey] = cached;
            }

            DEKI_LOG_EDITOR("FontPreview: Ready %s @ %d px (atlas %ux%u)",
                          sourceGuid.c_str(), fontSize, result.atlasWidth, result.atlasHeight);

            // Return the font we just created
            return Deki2D::GetPreviewFont(sourceGuid, fontSize);
        },

        // Atlas callback - returns GPU texture for rendering
        [](const std::string& sourceGuid, int fontSize, uint32_t* outW, uint32_t* outH) -> uint32_t {
            DEKI_LOG_EDITOR("FontPreview: LoadFontAtlasWithBaking called for %s @ %d px", sourceGuid.c_str(), fontSize);

            if (sourceGuid.empty() || fontSize <= 0)
            {
                if (outW) *outW = 0;
                if (outH) *outH = 0;
                return 0;
            }

            // 1. Check if already baked to disk - use actual variant GUID from .data file
            std::string variantGuid = GetVariantGuidFromData(sourceGuid, fontSize);
            if (!variantGuid.empty())
            {
                uint32_t bakedAtlas = DekiEditor::EditorAssets::Get()->LoadFontAtlas(variantGuid, outW, outH);
                if (bakedAtlas != 0)
                {
                    DEKI_LOG_EDITOR("FontPreview: Found baked atlas for %s @ %d px (variant %s)", sourceGuid.c_str(), fontSize, variantGuid.c_str());
                    return bakedAtlas;
                }
            }

            // 2. Check preview texture cache
            // Use "preview:" prefix to keep separate from baked font GUIDs
            std::string cacheKey = "preview:" + sourceGuid + ":" + std::to_string(fontSize);
            auto it = s_PreviewTextureCache.find(cacheKey);
            if (it != s_PreviewTextureCache.end() && it->second.textureId != 0)
            {
                DEKI_LOG_EDITOR("FontPreview: Found cached preview atlas for %s @ %d px", sourceGuid.c_str(), fontSize);
                if (outW) *outW = it->second.width;
                if (outH) *outH = it->second.height;
                return it->second.textureId;
            }

            // 3. Not found - compile on demand
            DEKI_LOG_EDITOR("FontPreview: Compiling on-demand for atlas %s @ %d px", sourceGuid.c_str(), fontSize);

            std::string ttfPath = ResolveTTFPath(sourceGuid);
            if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
            {
                DEKI_LOG_WARNING("FontPreview: TTF not found for %s (path: %s)", sourceGuid.c_str(), ttfPath.c_str());
                if (outW) *outW = 0;
                if (outH) *outH = 0;
                return 0;
            }

            Deki2D::FontCompiler::CompileOptions options;
            options.fontSize = fontSize;
            options.firstChar = 32;
            options.lastChar = 126;
            options.padding = 2;

            Deki2D::FontCompiler::CompileResult result;
            if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, result))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to compile %s", ttfPath.c_str());
                if (outW) *outW = 0;
                if (outH) *outH = 0;
                return 0;
            }

            // Create preview font in engine cache
            if (!Deki2D::SetPreviewFontFromData(
                    sourceGuid, fontSize,
                    result.atlasRGBA.data(), result.atlasWidth, result.atlasHeight,
                    result.glyphs.data(), result.glyphs.size(),
                    result.firstChar, result.lastChar,
                    result.lineHeight, result.baseline))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to set preview font data");
                if (outW) *outW = 0;
                if (outH) *outH = 0;
                return 0;
            }

            // Upload atlas to GPU
            uint32_t texId = UploadTextureToGPU(result.atlasRGBA.data(), result.atlasWidth, result.atlasHeight);
            if (texId != 0)
            {
                PreviewFontTexture cached;
                cached.textureId = texId;
                cached.width = result.atlasWidth;
                cached.height = result.atlasHeight;
                s_PreviewTextureCache[cacheKey] = cached;

                if (outW) *outW = result.atlasWidth;
                if (outH) *outH = result.atlasHeight;

                DEKI_LOG_EDITOR("FontPreview: Compiled and uploaded atlas %s @ %d px (%ux%u)",
                              sourceGuid.c_str(), fontSize, result.atlasWidth, result.atlasHeight);
                return texId;
            }

            if (outW) *outW = 0;
            if (outH) *outH = 0;
            return 0;
        }
    );

    DEKI_LOG_EDITOR("Deki2D: Font preview callbacks initialized");
}

void ClearPreviewTextureCache()
{
    for (auto& [key, cached] : s_PreviewTextureCache)
    {
        if (cached.textureId != 0)
        {
            GLuint texId = static_cast<GLuint>(cached.textureId);
            glDeleteTextures(1, &texId);
        }
    }
    s_PreviewTextureCache.clear();
    DEKI_LOG_EDITOR("Deki2D: Preview texture cache cleared");
}

} // namespace Deki2D

#endif // DEKI_EDITOR
