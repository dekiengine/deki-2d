/**
 * @file Deki2DEditorPreview.cpp
 * @brief Editor-only font and texture preview support for deki-2d.
 *
 * Moved out of the package entry point (Deki2DModule.cpp), which should not
 * carry windows.h, OpenGL, JSON and the asset pipeline. Everything here is
 * editor-only.
 */

#ifdef DEKI_EDITOR

#include "Deki2DPackage.h"
#include "TextComponent.h"
#include "Texture2D.h"
#include "BitmapFont.h"
#include "editor/FontCompiler.h"
#include "editor/FontSyncHandler.h"
#include <deki-editor/EditorAssets.h>
#include <deki-editor/AssetPipeline.h>
#include <deki/Guid.h>
#include <deki/Scene.h>
#include <deki/Object.h>
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>

// OpenGL for preview texture upload
#ifdef _WIN32
// NOMINMAX: windows.h defines min/max as macros, which breaks every std::min/std::max
// call that follows it in the same translation unit. That is invisible under one-file-
// per-TU compilation but bites as soon as this file shares a TU (unity build).
#define NOMINMAX
#include <windows.h>
#endif
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Deki2D
{
// Installed as TextComponent's font resolve callback by Deki2D_EnsureRegistered.
BitmapFont* EditorFontResolve(TextComponent* tc);
}

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
    atlas->hasAlpha = true;
    atlas->hasTransparency = true;

    size_t atlasSize = result.atlasWidth * result.atlasHeight * 4;
    if (Deki::Memory::IsInitialized())
    {
        atlas->data = static_cast<uint8_t*>(Deki::Memory::Allocate(atlasSize, Deki::MemoryUse::Buffer, "FontPreviewAtlas"));
        atlas->allocatedWithBackend = true;
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

// The baked variant's GUID for the component's (font.source, fontSize),
// recomputed only when either changed. This runs for every TextComponent on
// every frame in play and edit mode, and ComputeBakedFontGuid builds strings,
// hashes a deterministic GUID and resolves an asset path each time.
static const std::string& SyncedBakedFontGuid(TextComponent* tc)
{
    if (tc->editorSyncedFontSize != tc->fontSize || tc->editorSyncedFontSource != tc->font.source)
    {
        tc->editorSyncedFontSource = tc->font.source;
        tc->editorSyncedFontSize = tc->fontSize;
        tc->editorSyncedFontGuid = ComputeBakedFontGuid(tc->font.source, tc->fontSize);
    }
    return tc->editorSyncedFontGuid;
}

BitmapFont* Deki2D::EditorFontResolve(TextComponent* tc)
{
    if (Deki::Engine::IsRuntimeMode())
    {
        // Play mode: update GUID if fontSize changed (e.g., by RollerComponent)
        if (!tc->font.source.empty() && tc->fontSize > 0)
        {
            const std::string& expectedGuid = SyncedBakedFontGuid(tc);
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
        const std::string& expectedGuid = SyncedBakedFontGuid(tc);
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
    atlas->hasAlpha = true;
    atlas->hasTransparency = true;

    size_t atlasSize = atlasWidth * atlasHeight * 4;
    if (Deki::Memory::IsInitialized())
    {
        atlas->data = static_cast<uint8_t*>(Deki::Memory::Allocate(atlasSize, Deki::MemoryUse::Buffer, "FontPreviewAtlas"));
        atlas->allocatedWithBackend = true;
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
        // Unpack state is global; other GL users (e.g. ImGui glyph uploads) can leave
        // a row stride behind, which would shear/overread this tightly packed upload.
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
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
