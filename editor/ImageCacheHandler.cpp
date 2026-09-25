/**
 * @file ImageCacheHandler.cpp
 * @brief Cache handler and export encoder for source images
 *        (.png/.jpg/.jpeg/.bmp/.tga/.gif).
 *
 * Migrated from AssetPipeline.cpp::HandleImageCache so the engine has no
 * built-in knowledge of sprite-source extensions or sprite-settings shape.
 * deki-editor.dll exposes DecodeImageFile() — a thin wrapper over stb_image —
 * so this handler doesn't carry its own image-decode dependency.
 *
 * One compile serves both: the editor cache is the image stored for the
 * editor's target (the project's active platform), and an export for another
 * target runs the same compile for that one. Which format each gets is
 * ResolveTextureFormat's answer (TextureFormatResolve.h); how big, the
 * target's Max Size (TextureSettings.h).
 *
 * Registration happens via a static initializer that hooks AssetPipeline::OnStarted.
 * On package unload, ClearOnStartedCallbacks() drops the std::function before
 * FreeLibrary, so there are no dangling pointers across hot-reload.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/AssetTypeRegistry.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/TextureData.h>
#include <deki-editor/TextureFormatResolve.h>
#include <deki-editor/TextureSettings.h>
#include <deki-editor/SubAsset.h>
#include <deki/LogSystem.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

#include "ImageResample.h"


namespace fs = std::filesystem;
using json = nlohmann::json;

namespace DekiEditor
{

namespace
{

// Everything an image's sidecar (<image>.data) says about how it is stored.
struct ImageSidecar
{
    bool exists = false;
    json data;                     // the whole file, for the staleness checks
    SpriteSettings sprite;
    ChromaKeySettings chromaKey;
    TextureSettings texture;  // settings.texture: format and Max Size, per target
};

ImageSidecar ReadImageSidecar(const std::string& imagePath)
{
    ImageSidecar out;
    const std::string dataPath = imagePath + ".data";
    if (!fs::exists(dataPath))
        return out;
    std::ifstream dataFile(dataPath);
    if (!dataFile.is_open())
        return out;
    try
    {
        out.data = json::parse(dataFile);
        out.exists = true;
    }
    catch (const json::exception& e)
    {
        DEKI_LOG_WARNING("ImageCache: JSON parse error in %s: %s", dataPath.c_str(), e.what());
        return out;
    }
    const json& d = out.data;

    if (d.contains("settings") && d["settings"].contains("sprite"))
    {
        const auto& sprite = d["settings"]["sprite"];
        const std::string modeStr = sprite.value("mode", "grid");
        if (modeStr == "atlas")
        {
            out.sprite.mode = SpriteSlicingMode::Atlas;
            if (sprite.contains("frames") && sprite["frames"].is_array())
            {
                for (const auto& frameJson : sprite["frames"])
                {
                    AtlasFrame frame;
                    frame.x = frameJson.value("x", 0);
                    frame.y = frameJson.value("y", 0);
                    frame.width = frameJson.value("width", 0);
                    frame.height = frameJson.value("height", 0);
                    frame.name = frameJson.value("name", "");
                    out.sprite.frames.push_back(frame);
                }
            }
        }
        else if (sprite.contains("frameWidth") || sprite.contains("frameHeight"))
        {
            out.sprite.mode = SpriteSlicingMode::Grid;
            out.sprite.frameWidth = sprite.value("frameWidth", 0);
            out.sprite.frameHeight = sprite.value("frameHeight", 0);
        }
    }
    else if (d.contains("sprite"))
    {
        const auto& sprite = d["sprite"];
        out.sprite.mode = SpriteSlicingMode::Grid;
        out.sprite.frameWidth = sprite.value("frameWidth", 0);
        out.sprite.frameHeight = sprite.value("frameHeight", 0);
    }

    // Optional 9-slice borders: "nine_slice": [top, right, bottom, left]
    const json* nineSliceNode = nullptr;
    if (d.contains("settings") && d["settings"].contains("nine_slice"))
        nineSliceNode = &d["settings"]["nine_slice"];
    else if (d.contains("nine_slice"))
        nineSliceNode = &d["nine_slice"];
    if (nineSliceNode && nineSliceNode->is_array() && nineSliceNode->size() >= 4)
    {
        out.sprite.hasNineSlice = true;
        out.sprite.nineSliceTop = static_cast<uint16_t>((*nineSliceNode)[0].get<int>());
        out.sprite.nineSliceRight = static_cast<uint16_t>((*nineSliceNode)[1].get<int>());
        out.sprite.nineSliceBottom = static_cast<uint16_t>((*nineSliceNode)[2].get<int>());
        out.sprite.nineSliceLeft = static_cast<uint16_t>((*nineSliceNode)[3].get<int>());
    }

    if (d.contains("settings") && d["settings"].contains("texture"))
    {
        const auto& tex = d["settings"]["texture"];
        out.texture = ReadTextureSettings(tex);
        if (tex.contains("chroma_key"))
        {
            const auto& ck = tex["chroma_key"];
            out.chromaKey.enabled = ck.value("enabled", false);
            out.chromaKey.r = static_cast<uint8_t>(ck.value("r", 255));
            out.chromaKey.g = static_cast<uint8_t>(ck.value("g", 0));
            out.chromaKey.b = static_cast<uint8_t>(ck.value("b", 255));
        }
    }
    return out;
}

/// The cached file's format and whether its source had alpha, from its header.
bool ReadCachedFormat(const std::string& cachePath, TextureFormat& format, bool& hasAlpha)
{
    std::ifstream f(cachePath, std::ios::binary);
    TexHeader header{};
    if (!f.read(reinterpret_cast<char*>(&header), sizeof(header)))
        return false;
    format = static_cast<TextureFormat>(header.format);
    hasAlpha = (header.flags & static_cast<uint32_t>(TexFlags::HasAlpha)) != 0;
    return true;
}

/// The parts of the image that must not bleed into each other when it is
/// shrunk: its sprite frames, and the nine parts of a nine-slice image.
std::vector<Deki2DEditor::PixelRect> ShrinkRegions(const SpriteSettings& sprite, int w, int h)
{
    std::vector<Deki2DEditor::PixelRect> out;
    if (sprite.mode == SpriteSlicingMode::Atlas)
    {
        for (const AtlasFrame& f : sprite.frames)
            out.push_back({ f.x, f.y, f.x + f.width, f.y + f.height });
    }
    else if (sprite.mode == SpriteSlicingMode::Grid && (sprite.frameWidth > 0 || sprite.frameHeight > 0))
    {
        const int fw = sprite.frameWidth > 0 ? sprite.frameWidth : w;
        const int fh = sprite.frameHeight > 0 ? sprite.frameHeight : h;
        for (int y = 0; y + fh <= h; y += fh)
            for (int x = 0; x + fw <= w; x += fw)
                out.push_back({ x, y, x + fw, y + fh });
    }
    if (sprite.hasNineSlice)
    {
        const int xs[4] = { 0, sprite.nineSliceLeft, w - sprite.nineSliceRight, w };
        const int ys[4] = { 0, sprite.nineSliceTop, h - sprite.nineSliceBottom, h };
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i)
                out.push_back({ xs[i], ys[j], xs[i + 1], ys[j + 1] });
    }
    return out;
}

/// Decode the image and write it as `target` stores it: its format, and
/// shrunk to the target's Max Size. Registers the sprite frames with
/// `pipeline` when given (the editor cache); an export passes null.
bool CompileImage(const std::string& imagePath, const std::string& guid, const ImageSidecar& sidecar,
                  const AssetExportTarget& target, const std::string& outPath, AssetPipeline* pipeline)
{
    DecodedImage decoded;
    if (!DecodeImageFile(imagePath, decoded))
        return false;

    const size_t pixelCount = static_cast<size_t>(decoded.width) * static_cast<size_t>(decoded.height);
    const bool hasAlpha = TextureImporter::HasAlphaChannel(decoded.rgba.data(), pixelCount);
    const TextureFormat format = ResolveTextureFormat(sidecar.texture.format,
                                                      sidecar.texture.TargetFormat(target.platformId),
                                                      target.colorFormat, hasAlpha);

    // Max Size. Frames, nine-slice borders and the frame list stay in the
    // image's pixels; the file records the image's size and the runtime maps
    // them to the stored ones.
    int storedW = decoded.width, storedH = decoded.height;
    MaxSizeDims(decoded.width, decoded.height, sidecar.texture.MaxSizeFor(target.platformId), storedW, storedH);
    std::vector<uint8_t> shrunk;
    const uint8_t* pixels = decoded.rgba.data();
    if (storedW != decoded.width || storedH != decoded.height)
    {
        shrunk = Deki2DEditor::ShrinkImage(decoded.rgba.data(), decoded.width, decoded.height, storedW, storedH,
                                           ShrinkRegions(sidecar.sprite, decoded.width, decoded.height),
                                           sidecar.chromaKey.enabled);
        pixels = shrunk.data();
    }

    std::vector<SubAssetInfo> subAssets;
    if (sidecar.sprite.HasData())
    {
        subAssets = TextureImporter::GenerateFrameSubAssets(guid, decoded.width, decoded.height, sidecar.sprite);
        if (pipeline)
            pipeline->RegisterSubAssets(guid, subAssets);
    }

    const SpriteSettings* settingsPtr = sidecar.sprite.HasData() ? &sidecar.sprite : nullptr;
    const std::vector<SubAssetInfo>* subAssetsPtr = subAssets.empty() ? nullptr : &subAssets;
    const ChromaKeySettings* chromaPtr = sidecar.chromaKey.enabled ? &sidecar.chromaKey : nullptr;
    return TextureImporter::WriteTexFile(outPath, pixels, static_cast<uint32_t>(storedW), static_cast<uint32_t>(storedH),
                                         format, settingsPtr, subAssetsPtr, chromaPtr,
                                         static_cast<uint32_t>(decoded.width), static_cast<uint32_t>(decoded.height));
}

/// Whether the sidecar changed after the cache was written, in a way that
/// changes the cache.
bool SidecarNewerThanCache(const ImageSidecar& sidecar, const std::string& imagePath, const std::string& cachePath)
{
    if (!sidecar.exists)
        return false;
    const std::string dataPath = imagePath + ".data";
    std::error_code ec;
    const auto dataTime = fs::last_write_time(dataPath, ec);
    const auto cacheTime = fs::last_write_time(cachePath, ec);
    return !ec && dataTime > cacheTime;
}

AssetCacheResult HandleSpriteImageCache(const AssetCacheContext& ctx)
{
    bool result = ctx.hasCachedVersion;
    const ImageSidecar sidecar = ReadImageSidecar(ctx.absolutePath);
    const AssetExportTarget& editorTarget = ctx.pipeline->GetEditorTarget();

    // Check for staleness if cache exists
    if (result)
    {
        bool needsRegen = false;
        if (sidecar.sprite.HasData() && !TextureImporter::HasFrameListChunk(ctx.cachePath))
        {
            DEKI_LOG_EDITOR("ImageCache: Cache missing FrameList for %s, regenerating", ctx.relativePath.c_str());
            needsRegen = true;
        }
        else if (SidecarNewerThanCache(sidecar, ctx.absolutePath, ctx.cachePath))
        {
            DEKI_LOG_EDITOR("ImageCache: Settings newer than cache for %s, regenerating", ctx.relativePath.c_str());
            needsRegen = true;
        }
        else
        {
            // Stored for another target (the active platform changed, or the
            // image predates Automatic): store it for this one.
            TextureFormat cachedFormat;
            bool cachedHasAlpha = false;
            if (ReadCachedFormat(ctx.cachePath, cachedFormat, cachedHasAlpha))
            {
                const TextureFormat wanted =
                    ResolveTextureFormat(sidecar.texture.format, sidecar.texture.TargetFormat(editorTarget.platformId),
                                         editorTarget.colorFormat, cachedHasAlpha);
                if (wanted != cachedFormat)
                {
                    DEKI_LOG_EDITOR("ImageCache: %s is %s, the editor's target wants %s; regenerating",
                                    ctx.relativePath.c_str(), TextureFormatName(cachedFormat),
                                    TextureFormatName(wanted));
                    needsRegen = true;
                }
            }
            // ... and at that target's Max Size.
            TexData cached;
            int sourceW = 0, sourceH = 0;
            if (!needsRegen && TextureImporter::ReadTexFile(ctx.cachePath, cached) &&
                TextureImporter::ReadSourceSize(ctx.cachePath, sourceW, sourceH))
            {
                int wantW = 0, wantH = 0;
                MaxSizeDims(sourceW, sourceH, sidecar.texture.MaxSizeFor(editorTarget.platformId), wantW, wantH);
                if (static_cast<int>(cached.header.width) != wantW || static_cast<int>(cached.header.height) != wantH)
                {
                    DEKI_LOG_EDITOR("ImageCache: %s is stored at %ux%u, the editor's target wants %dx%d; regenerating",
                                    ctx.relativePath.c_str(), cached.header.width, cached.header.height, wantW, wantH);
                    needsRegen = true;
                }
            }
        }

        if (needsRegen)
        {
            fs::remove(ctx.cachePath);
            result = false;
        }
    }

    // Generate cache if not cached
    if (!result)
        result = CompileImage(ctx.absolutePath, ctx.guid, sidecar, editorTarget, ctx.cachePath, ctx.pipeline);

    // Always register sub-assets for cached images (warm-cache path). In the
    // image's pixels, which a shrunk cache records.
    if (result && sidecar.sprite.HasData())
    {
        int sourceW = 0, sourceH = 0;
        if (TextureImporter::ReadSourceSize(ctx.cachePath, sourceW, sourceH))
        {
            auto subAssets = TextureImporter::GenerateFrameSubAssets(ctx.guid, sourceW, sourceH, sidecar.sprite);
            DEKI_LOG_EDITOR("ImageCache: Registering %zu subassets for '%s'", subAssets.size(), ctx.guid.c_str());
            ctx.pipeline->RegisterSubAssets(ctx.guid, subAssets);
        }
        else
        {
            DEKI_LOG_WARNING("ImageCache: Failed to read cached texture '%s'", ctx.cachePath.c_str());
        }
    }

    return result ? AssetCacheResult::Cached : AssetCacheResult::NotCached;
}

bool EncodeImageForTarget(const AssetExportContext& ctx)
{
    const ImageSidecar sidecar = ReadImageSidecar(ctx.absolutePath);
    return CompileImage(ctx.absolutePath, ctx.guid, sidecar, ctx.target, ctx.outPath, nullptr);
}

struct ImageCacheRegistrar
{
    ImageCacheRegistrar()
    {
        AssetPipeline::OnStarted([](AssetPipeline* p) {
            for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"})
            {
                p->RegisterCacheHandler(ext, HandleSpriteImageCache);
                p->RegisterExportEncoder(ext, EncodeImageForTarget);
            }
        });
        // Claim the Texture category for raster image extensions so the
        // editor's UI classification (icons, browser grouping, file dialogs)
        // can resolve `.png` etc. through the registry instead of a
        // hardcoded list. .dtex (the compiled texture) is also Texture so
        // the picker treats already-cached previews uniformly.
        for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".dtex"})
            AssetTypeRegistry::Instance().RegisterCategory(ext, AssetCategory::Texture);
    }
};
static ImageCacheRegistrar s_ImageCacheRegistrar;

} // namespace
} // namespace DekiEditor

#endif // DEKI_EDITOR
