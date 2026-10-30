// Editor-only font and texture preview support for deki-2d. Kept out of the
// package entry point (Deki2DModule.cpp), which should not pull in
// editor textures, JSON and the asset pipeline.

#ifdef DEKI_EDITOR

#include "Deki2DPackage.h"
#include "TextComponent.h"
#include <deki/assets/Texture2D.h>
#include "BitmapFont.h"
#include "editor/FontCompiler.h"
#include "editor/FontSyncHandler.h"
#include <deki-editor/EditorAssets.h>
#include <deki-editor/AssetPipeline.h>
#include <deki-editor/Paths.h>
#include <deki/Guid.h>
#include <deki/Scene.h>
#include <deki/Object.h>
#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <unordered_map>

#include <deki-editor/EditorTexture.h>

namespace Deki2D
{
// Installed as Deki2D::TextComponent's font resolve callback by Deki2DEnsureRegistered.
Deki2D::BitmapFont* EditorFontResolve(Deki2D::TextComponent* tc);
}  // namespace Deki2D

// =============================================================================
// Preview font management, kept here so TextComponent.cpp has no editor code.
// =============================================================================

namespace
{
static Deki2D::BitmapFont* s_PreviewFont = nullptr;
static std::string s_PreviewFontGuid;
static int s_PreviewFontSize = 0;
}  // anonymous namespace

static bool IsBdfFont(const std::string& sourceGuid)
{
    auto* pl = DekiEditor::AssetPipeline::Instance();
    const auto* fi = pl ? pl->GetAssetInfoByGuid(sourceGuid) : nullptr;
    if (!fi)
    {
        return false;
    }
    std::string ext = std::filesystem::path(fi->path).extension().string();
    for (char& c : ext)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return (ext == ".bdf");
}

static std::string ComputeBakedFontGuid(const std::string& sourceGuid, int fontSize)
{
    std::string seed = IsBdfFont(sourceGuid) ? (sourceGuid + ":bdf") : (sourceGuid + ":" + std::to_string(fontSize));
    return Deki::GenerateDeterministicGuid(seed);
}

// The baked variant's GUID for the component's (font.source, fontSize),
// worked out again only when either changes. This runs for every
// TextComponent every frame, in play and edit mode, and ComputeBakedFontGuid
// builds strings, hashes a GUID and resolves an asset path each time.
static const std::string& SyncedBakedFontGuid(Deki2D::TextComponent* tc)
{
    if (tc->editorSyncedFontSize != tc->fontSize || tc->editorSyncedFontSource != tc->font.source)
    {
        tc->editorSyncedFontSource = tc->font.source;
        tc->editorSyncedFontSize = tc->fontSize;
        tc->editorSyncedFontGuid = ComputeBakedFontGuid(tc->font.source, tc->fontSize);
    }
    return tc->editorSyncedFontGuid;
}

Deki2D::BitmapFont* Deki2D::EditorFontResolve(Deki2D::TextComponent* tc)
{
    if (Deki::Engine::IsRuntimeMode())
    {
        // Play mode: follow fontSize changes (RollerComponent makes them, say).
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

    // Edit mode: sync the GUID to the baked variant. TextComponentEditor
    // bakes the size when it is missing.
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

    tc->fontSizeUnavailable = (tc->font.Get() == nullptr && !tc->font.source.empty() && tc->fontSize > 0);
    return nullptr;
}

namespace Deki2D
{

// Defined in the Font Preview Callbacks section below.
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

bool SetPreviewFontFromData(const std::string& sourceGuid, int fontSize, const uint8_t* atlasRGBA, uint32_t atlasWidth,
                            uint32_t atlasHeight, const GlyphInfo* glyphs, size_t glyphCount, uint8_t firstChar,
                            uint8_t lastChar, uint8_t lineHeight, uint8_t baseline)
{
    if (!atlasRGBA || !glyphs || glyphCount == 0 || atlasWidth == 0 || atlasHeight == 0)
    {
        return false;
    }

    if (s_PreviewFontGuid != sourceGuid || s_PreviewFontSize != fontSize)
    {
        ClearPreviewFont();
    }

    Deki::Texture2D* atlas = new Deki::Texture2D();
    atlas->width = atlasWidth;
    atlas->height = atlasHeight;
    atlas->format = Deki::Texture2D::TextureFormat::RGBA8888;
    atlas->hasAlpha = true;
    atlas->hasTransparency = true;

    size_t atlasSize = atlasWidth * atlasHeight * 4;
    if (Deki::Memory::IsInitialized())
    {
        atlas->data = static_cast<uint8_t*>(Deki::Memory::Allocate(atlasSize, Deki::Memory::External));
        atlas->allocatedWithBackend = true;
    }
    else
    {
        atlas->data = static_cast<uint8_t*>(std::malloc(atlasSize));
    }

    if (!atlas->data)
    {
        delete atlas;
        return false;
    }
    memcpy(atlas->data, atlasRGBA, atlasSize);

    Deki::Buffer<Deki2D::GlyphInfo> glyphsCopy(glyphCount, Deki::Memory::Internal);
    if (!glyphsCopy)
    {
        delete atlas;
        return false;
    }
    memcpy(glyphsCopy.Data(), glyphs, glyphsCopy.Bytes());

    Deki2D::BitmapFont* font =
        Deki2D::BitmapFont::CreateFromMemory(atlas, std::move(glyphsCopy), firstChar, lastChar, lineHeight, baseline);

    if (!font)
    {
        return false;
    }

    s_PreviewFont = font;
    s_PreviewFontGuid = sourceGuid;
    s_PreviewFontSize = fontSize;
    return true;
}

bool HasPreviewFont(const std::string& sourceGuid, int fontSize)
{
    return s_PreviewFont && s_PreviewFontGuid == sourceGuid && s_PreviewFontSize == fontSize;
}

Deki2D::BitmapFont* GetPreviewFont(const std::string& sourceGuid, int fontSize)
{
    if (s_PreviewFont && s_PreviewFontGuid == sourceGuid && s_PreviewFontSize == fontSize)
    {
        return s_PreviewFont;
    }
    return nullptr;
}

}  // namespace Deki2D

// =============================================================================
// Font Preview Callbacks
// =============================================================================

namespace
{
// GPU textures of preview fonts, keyed by "preview:sourceGuid:fontSize".
struct PreviewFontTexture
{
    uint32_t textureId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};
static std::unordered_map<std::string, PreviewFontTexture> s_PreviewTextureCache;

// The GUID of the font's baked variant of this size, or "" when it is not
// baked.
static std::string GetBakedVariantGuid(const std::string& sourceGuid, int fontSize)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
    {
        return "";
    }
    std::string variantGuid = Deki::GenerateDeterministicGuid(sourceGuid + ":" + std::to_string(fontSize));
    if (!std::filesystem::exists(DekiEditor::GetCachePath(pipeline->GetProjectPath(), variantGuid)))
    {
        return "";
    }
    return variantGuid;
}

static std::string ResolveTTFPath(const std::string& sourceGuid)
{
    auto* pipeline = DekiEditor::AssetPipeline::Instance();
    if (!pipeline)
    {
        return "";
    }

    const DekiEditor::AssetInfo* info = pipeline->GetAssetInfoByGuid(sourceGuid);
    if (!info)
    {
        return "";
    }

    namespace fs = std::filesystem;
    return (fs::path(pipeline->GetProjectPath()) / info->path).string();
}

static uint32_t UploadTextureToGPU(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    if (!rgba || width == 0 || height == 0)
    {
        return 0;
    }

    return DekiEditor::EditorTexture::CreateRgba(static_cast<int>(width), static_cast<int>(height), rgba);
}
}  // anonymous namespace

namespace Deki2D
{

void InitializeFontPreviewCallbacks()
{
    DekiEditor::EditorAssets::Get()->SetFontBakingCallbacks(
        // Returns the BitmapFont, for text metrics.
        [](const std::string& sourceGuid, int fontSize) -> Deki2D::BitmapFont*
        {
            if (sourceGuid.empty() || fontSize <= 0)
            {
                return nullptr;
            }

            // 1. Already baked to disk: use the variant GUID from the .data file.
            std::string variantGuid = GetBakedVariantGuid(sourceGuid, fontSize);
            if (!variantGuid.empty())
            {
                Deki2D::BitmapFont* baked = DekiEditor::EditorAssets::Get()->GetBitmapFont(variantGuid);
                if (baked)
                {
                    return baked;
                }
            }

            // 2. A preview font that already has its GPU texture. The
            // "preview:" prefix keeps the key apart from baked font GUIDs.
            std::string cacheKey = "preview:" + sourceGuid + ":" + std::to_string(fontSize);
            Deki2D::BitmapFont* preview = Deki2D::GetPreviewFont(sourceGuid, fontSize);
            if (preview)
            {
                auto texIt = s_PreviewTextureCache.find(cacheKey);
                if (texIt != s_PreviewTextureCache.end() && texIt->second.textureId != 0)
                {
                    return preview;
                }
                // The font is there but its texture is not: compile again for the texture.
                DEKI_LOG_DEBUG("FontPreview: Preview font exists but no GPU texture, recompiling %s @ %d px",
                               sourceGuid.c_str(), fontSize);
            }

            // 3. Compile the font now.
            std::string ttfPath = ResolveTTFPath(sourceGuid);
            if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
            {
                DEKI_LOG_WARNING("FontPreview: TTF not found for %s", sourceGuid.c_str());
                return nullptr;
            }

            Deki2D::FontCompiler::CompileOptions options = Deki2D::LoadFontCompileOptions(ttfPath);
            options.fontSize = fontSize;

            Deki2D::FontCompiler::CompileResult result;
            if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, result))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to compile %s", ttfPath.c_str());
                return nullptr;
            }

            // 4. Put the preview font in the engine's cache.
            if (!Deki2D::SetPreviewFontFromData(sourceGuid, fontSize, result.atlasRGBA.data(), result.atlasWidth,
                                                result.atlasHeight, result.glyphs.data(), result.glyphs.size(),
                                                result.firstChar, result.lastChar, result.lineHeight, result.baseline))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to set preview font data");
                return nullptr;
            }

            // 5. Upload the atlas to the GPU for the editor, replacing any old texture.
            auto it = s_PreviewTextureCache.find(cacheKey);
            if (it != s_PreviewTextureCache.end() && it->second.textureId != 0)
            {
                DekiEditor::EditorTexture::Destroy(it->second.textureId);
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

            DEKI_LOG_EDITOR("FontPreview: compiled %s @ %d px (atlas %ux%u)", sourceGuid.c_str(), fontSize,
                            result.atlasWidth, result.atlasHeight);

            return Deki2D::GetPreviewFont(sourceGuid, fontSize);
        },

        // Returns the atlas's GPU texture, for drawing.
        [](const std::string& sourceGuid, int fontSize, uint32_t* outW, uint32_t* outH) -> uint32_t
        {
            if (sourceGuid.empty() || fontSize <= 0)
            {
                if (outW)
                {
                    *outW = 0;
                }
                if (outH)
                {
                    *outH = 0;
                }
                return 0;
            }

            // 1. Already baked to disk: use the variant GUID from the .data file.
            std::string variantGuid = GetBakedVariantGuid(sourceGuid, fontSize);
            if (!variantGuid.empty())
            {
                uint32_t bakedAtlas = DekiEditor::EditorAssets::Get()->LoadFontAtlas(variantGuid, outW, outH);
                if (bakedAtlas != 0)
                {
                    return bakedAtlas;
                }
            }

            // 2. The preview texture cache. The "preview:" prefix keeps the
            // key apart from baked font GUIDs.
            std::string cacheKey = "preview:" + sourceGuid + ":" + std::to_string(fontSize);
            auto it = s_PreviewTextureCache.find(cacheKey);
            if (it != s_PreviewTextureCache.end() && it->second.textureId != 0)
            {
                if (outW)
                {
                    *outW = it->second.width;
                }
                if (outH)
                {
                    *outH = it->second.height;
                }
                return it->second.textureId;
            }

            // 3. Not found: compile now.
            std::string ttfPath = ResolveTTFPath(sourceGuid);
            if (ttfPath.empty() || !std::filesystem::exists(ttfPath))
            {
                DEKI_LOG_WARNING("FontPreview: TTF not found for %s (path: %s)", sourceGuid.c_str(), ttfPath.c_str());
                if (outW)
                {
                    *outW = 0;
                }
                if (outH)
                {
                    *outH = 0;
                }
                return 0;
            }

            Deki2D::FontCompiler::CompileOptions options = Deki2D::LoadFontCompileOptions(ttfPath);
            options.fontSize = fontSize;

            Deki2D::FontCompiler::CompileResult result;
            if (!Deki2D::FontCompiler::CompileTrueTypeFont(ttfPath, options, result))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to compile %s", ttfPath.c_str());
                if (outW)
                {
                    *outW = 0;
                }
                if (outH)
                {
                    *outH = 0;
                }
                return 0;
            }

            // Put the preview font in the engine's cache.
            if (!Deki2D::SetPreviewFontFromData(sourceGuid, fontSize, result.atlasRGBA.data(), result.atlasWidth,
                                                result.atlasHeight, result.glyphs.data(), result.glyphs.size(),
                                                result.firstChar, result.lastChar, result.lineHeight, result.baseline))
            {
                DEKI_LOG_ERROR("FontPreview: Failed to set preview font data");
                if (outW)
                {
                    *outW = 0;
                }
                if (outH)
                {
                    *outH = 0;
                }
                return 0;
            }

            uint32_t texId = UploadTextureToGPU(result.atlasRGBA.data(), result.atlasWidth, result.atlasHeight);
            if (texId != 0)
            {
                PreviewFontTexture cached;
                cached.textureId = texId;
                cached.width = result.atlasWidth;
                cached.height = result.atlasHeight;
                s_PreviewTextureCache[cacheKey] = cached;

                if (outW)
                {
                    *outW = result.atlasWidth;
                }
                if (outH)
                {
                    *outH = result.atlasHeight;
                }

                DEKI_LOG_EDITOR("FontPreview: compiled atlas %s @ %d px (%ux%u)", sourceGuid.c_str(), fontSize,
                                result.atlasWidth, result.atlasHeight);
                return texId;
            }

            if (outW)
            {
                *outW = 0;
            }
            if (outH)
            {
                *outH = 0;
            }
            return 0;
        });
}

void ClearPreviewTextureCache()
{
    for (auto& [key, cached] : s_PreviewTextureCache)
    {
        if (cached.textureId != 0)
        {
            DekiEditor::EditorTexture::Destroy(cached.textureId);
        }
    }
    s_PreviewTextureCache.clear();
    DEKI_LOG_DEBUG("Deki2D: Preview texture cache cleared");
}

}  // namespace Deki2D

#endif  // DEKI_EDITOR
