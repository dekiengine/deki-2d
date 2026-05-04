/**
 * @file ImageCacheHandler.cpp
 * @brief Cache handler for source images (.png/.jpg/.jpeg/.bmp/.tga/.gif).
 *
 * Migrated from AssetPipeline.cpp::HandleImageCache so the engine has no
 * built-in knowledge of sprite-source extensions or sprite-settings shape.
 * deki-editor.dll exposes DecodeImageFile() — a thin wrapper over stb_image —
 * so this handler doesn't carry its own image-decode dependency.
 *
 * Registration happens via a static initializer that hooks AssetPipeline::OnStarted.
 * On module unload, ClearOnStartedCallbacks() drops the std::function before
 * FreeLibrary, so there are no dangling pointers across hot-reload.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/AssetPipeline.h>
#include <deki-editor/TextureImporter.h>
#include <deki-editor/TextureData.h>
#include <deki-editor/SubAsset.h>
#include "DekiLogSystem.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace DekiEditor
{

namespace
{

AssetCacheResult HandleSpriteImageCache(const AssetCacheContext& ctx)
{
    bool result = ctx.hasCachedVersion;

    // Check for staleness if cache exists
    if (result)
    {
        std::string dataPath = ctx.absolutePath + ".data";
        if (fs::exists(dataPath))
        {
            std::ifstream dataFile(dataPath);
            if (dataFile.is_open())
            {
                try
                {
                    json dataJson = json::parse(dataFile);
                    bool hasFrameSettings = false;

                    if (dataJson.contains("settings") && dataJson["settings"].contains("sprite"))
                    {
                        auto& sprite = dataJson["settings"]["sprite"];
                        std::string modeStr = sprite.value("mode", "grid");
                        if (modeStr == "atlas" && sprite.contains("frames") && sprite["frames"].is_array())
                            hasFrameSettings = !sprite["frames"].empty();
                        else if (sprite.contains("frameWidth") || sprite.contains("frameHeight"))
                            hasFrameSettings = sprite.value("frameWidth", 0) > 0 || sprite.value("frameHeight", 0) > 0;
                    }
                    else if (dataJson.contains("sprite"))
                    {
                        auto& sprite = dataJson["sprite"];
                        hasFrameSettings = sprite.value("frameWidth", 0) > 0 || sprite.value("frameHeight", 0) > 0;
                    }

                    bool needsRegen = false;
                    if (hasFrameSettings && !TextureImporter::HasFrameListChunk(ctx.cachePath))
                    {
                        DEKI_LOG_EDITOR("ImageCache: Cache missing FrameList for %s, regenerating", ctx.relativePath.c_str());
                        needsRegen = true;
                    }
                    else if (hasFrameSettings)
                    {
                        auto dataTime = fs::last_write_time(dataPath);
                        auto cacheTime = fs::last_write_time(ctx.cachePath);
                        if (dataTime > cacheTime)
                        {
                            DEKI_LOG_EDITOR("ImageCache: Data file newer than cache for %s, regenerating", ctx.relativePath.c_str());
                            needsRegen = true;
                        }
                    }

                    if (!needsRegen && dataJson.contains("settings") && dataJson["settings"].contains("texture"))
                    {
                        auto dataTime = fs::last_write_time(dataPath);
                        auto cacheTime = fs::last_write_time(ctx.cachePath);
                        if (dataTime > cacheTime)
                        {
                            DEKI_LOG_EDITOR("ImageCache: Texture settings newer than cache for %s, regenerating", ctx.relativePath.c_str());
                            needsRegen = true;
                        }
                    }

                    bool hasNineSliceSettings =
                        (dataJson.contains("settings") && dataJson["settings"].contains("nine_slice")) ||
                        dataJson.contains("nine_slice");
                    if (!needsRegen && hasNineSliceSettings)
                    {
                        auto dataTime = fs::last_write_time(dataPath);
                        auto cacheTime = fs::last_write_time(ctx.cachePath);
                        if (dataTime > cacheTime)
                        {
                            DEKI_LOG_EDITOR("ImageCache: 9-slice settings newer than cache for %s, regenerating", ctx.relativePath.c_str());
                            needsRegen = true;
                        }
                    }

                    if (needsRegen)
                    {
                        fs::remove(ctx.cachePath);
                        result = false;
                    }
                }
                catch (const json::exception&) {}
            }
        }
    }

    // Generate cache if not cached
    if (!result)
    {
        DecodedImage decoded;
        if (DecodeImageFile(ctx.absolutePath, decoded))
        {
            int width = decoded.width;
            int height = decoded.height;

            size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            bool hasAlpha = TextureImporter::HasAlphaChannel(decoded.rgba.data(), pixelCount);

            SpriteSettings spriteSettings;
            ChromaKeySettings chromaKey;  // populated from .data sidecar below if present
            TextureFormat textureFormat = hasAlpha ? TextureFormat::RGB565A8
                                                   : TextureFormat::RGB565;
            std::string dataPath = ctx.absolutePath + ".data";
            if (fs::exists(dataPath))
            {
                std::ifstream dataFile(dataPath);
                if (dataFile.is_open())
                {
                    try
                    {
                        json dataJson = json::parse(dataFile);
                        if (dataJson.contains("settings") && dataJson["settings"].contains("sprite"))
                        {
                            auto& sprite = dataJson["settings"]["sprite"];
                            std::string modeStr = sprite.value("mode", "grid");
                            if (modeStr == "atlas")
                            {
                                spriteSettings.mode = SpriteSlicingMode::Atlas;
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
                                        spriteSettings.frames.push_back(frame);
                                    }
                                }
                            }
                            else if (sprite.contains("frameWidth") || sprite.contains("frameHeight"))
                            {
                                spriteSettings.mode = SpriteSlicingMode::Grid;
                                spriteSettings.frameWidth = sprite.value("frameWidth", 0);
                                spriteSettings.frameHeight = sprite.value("frameHeight", 0);
                            }
                        }
                        else if (dataJson.contains("sprite"))
                        {
                            auto& sprite = dataJson["sprite"];
                            spriteSettings.mode = SpriteSlicingMode::Grid;
                            spriteSettings.frameWidth = sprite.value("frameWidth", 0);
                            spriteSettings.frameHeight = sprite.value("frameHeight", 0);
                        }

                        // Optional 9-slice borders: "nine_slice": [top, right, bottom, left]
                        const json* nineSliceNode = nullptr;
                        if (dataJson.contains("settings") && dataJson["settings"].contains("nine_slice"))
                            nineSliceNode = &dataJson["settings"]["nine_slice"];
                        else if (dataJson.contains("nine_slice"))
                            nineSliceNode = &dataJson["nine_slice"];
                        if (nineSliceNode && nineSliceNode->is_array() && nineSliceNode->size() >= 4)
                        {
                            spriteSettings.hasNineSlice    = true;
                            spriteSettings.nineSliceTop    = static_cast<uint16_t>((*nineSliceNode)[0].get<int>());
                            spriteSettings.nineSliceRight  = static_cast<uint16_t>((*nineSliceNode)[1].get<int>());
                            spriteSettings.nineSliceBottom = static_cast<uint16_t>((*nineSliceNode)[2].get<int>());
                            spriteSettings.nineSliceLeft   = static_cast<uint16_t>((*nineSliceNode)[3].get<int>());
                        }

                        if (dataJson.contains("settings") && dataJson["settings"].contains("texture"))
                        {
                            std::string fmtStr = dataJson["settings"]["texture"].value("format", "RGB565A8");
                            if (fmtStr == "RGB888") textureFormat = TextureFormat::RGB888;
                            else if (fmtStr == "RGBA8888") textureFormat = TextureFormat::RGBA8888;
                            else if (fmtStr == "RGB565") textureFormat = TextureFormat::RGB565;
                            else if (fmtStr == "RGB565A8") textureFormat = TextureFormat::RGB565A8;
                            else if (fmtStr == "ALPHA8") textureFormat = TextureFormat::ALPHA8;
                        }

                        if (dataJson.contains("settings") &&
                            dataJson["settings"].contains("texture") &&
                            dataJson["settings"]["texture"].contains("chroma_key"))
                        {
                            auto& ck = dataJson["settings"]["texture"]["chroma_key"];
                            chromaKey.enabled = ck.value("enabled", false);
                            chromaKey.r = static_cast<uint8_t>(ck.value("r", 255));
                            chromaKey.g = static_cast<uint8_t>(ck.value("g", 0));
                            chromaKey.b = static_cast<uint8_t>(ck.value("b", 255));
                        }
                    }
                    catch (const json::exception&) {}
                }
            }

            std::vector<SubAssetInfo> subAssets;
            if (spriteSettings.HasData())
            {
                subAssets = TextureImporter::GenerateFrameSubAssets(
                    ctx.guid, width, height, spriteSettings);
                ctx.pipeline->RegisterSubAssets(ctx.guid, subAssets);
            }

            const SpriteSettings* settingsPtr = spriteSettings.HasData() ? &spriteSettings : nullptr;
            const std::vector<SubAssetInfo>* subAssetsPtr = subAssets.empty() ? nullptr : &subAssets;
            const ChromaKeySettings* chromaPtr = chromaKey.enabled ? &chromaKey : nullptr;
            if (TextureImporter::WriteTexFile(ctx.cachePath, decoded.rgba.data(),
                                              static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height),
                                              textureFormat, settingsPtr, subAssetsPtr,
                                              chromaPtr))
            {
                result = true;
            }
        }
    }

    // Always register sub-assets for cached images (warm-cache path)
    if (result)
    {
        DEKI_LOG_EDITOR("ImageCache: Checking subassets for '%s' (guid=%s)",
                      ctx.relativePath.c_str(), ctx.guid.c_str());

        SpriteSettings spriteSettings;
        std::string dataPath = ctx.absolutePath + ".data";
        if (fs::exists(dataPath))
        {
            DEKI_LOG_EDITOR("ImageCache: Found .data file at '%s'", dataPath.c_str());
            std::ifstream dataFile(dataPath);
            if (dataFile.is_open())
            {
                try
                {
                    json dataJson = json::parse(dataFile);
                    if (dataJson.contains("settings") && dataJson["settings"].contains("sprite"))
                    {
                        auto& sprite = dataJson["settings"]["sprite"];
                        std::string modeStr = sprite.value("mode", "grid");
                        DEKI_LOG_EDITOR("ImageCache: Sprite mode='%s'", modeStr.c_str());
                        if (modeStr == "atlas")
                        {
                            spriteSettings.mode = SpriteSlicingMode::Atlas;
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
                                    spriteSettings.frames.push_back(frame);
                                }
                                DEKI_LOG_EDITOR("ImageCache: Atlas mode with %zu frames", spriteSettings.frames.size());
                            }
                        }
                        else if (sprite.contains("frameWidth") || sprite.contains("frameHeight"))
                        {
                            spriteSettings.mode = SpriteSlicingMode::Grid;
                            spriteSettings.frameWidth = sprite.value("frameWidth", 0);
                            spriteSettings.frameHeight = sprite.value("frameHeight", 0);
                            DEKI_LOG_EDITOR("ImageCache: Grid mode with frameWidth=%d, frameHeight=%d",
                                          spriteSettings.frameWidth, spriteSettings.frameHeight);
                        }
                    }
                    else if (dataJson.contains("sprite"))
                    {
                        auto& sprite = dataJson["sprite"];
                        spriteSettings.mode = SpriteSlicingMode::Grid;
                        spriteSettings.frameWidth = sprite.value("frameWidth", 0);
                        spriteSettings.frameHeight = sprite.value("frameHeight", 0);
                        DEKI_LOG_EDITOR("ImageCache: Legacy grid mode with frameWidth=%d, frameHeight=%d",
                                      spriteSettings.frameWidth, spriteSettings.frameHeight);
                    }
                    else
                    {
                        DEKI_LOG_EDITOR("ImageCache: No sprite settings in .data file");
                    }
                }
                catch (const json::exception& e)
                {
                    DEKI_LOG_WARNING("ImageCache: JSON parse error in .data file: %s", e.what());
                }
            }
        }
        else
        {
            DEKI_LOG_EDITOR("ImageCache: No .data file found");
        }

        if (spriteSettings.HasData())
        {
            DEKI_LOG_EDITOR("ImageCache: Generating subassets for '%s'", ctx.relativePath.c_str());
            TexData texData;
            if (TextureImporter::ReadTexFile(ctx.cachePath, texData) && texData.isValid())
            {
                auto subAssets = TextureImporter::GenerateFrameSubAssets(
                    ctx.guid, texData.header.width, texData.header.height, spriteSettings);
                DEKI_LOG_EDITOR("ImageCache: Registering %zu subassets for '%s'",
                              subAssets.size(), ctx.guid.c_str());
                ctx.pipeline->RegisterSubAssets(ctx.guid, subAssets);
            }
            else
            {
                DEKI_LOG_WARNING("ImageCache: Failed to read cached texture '%s'", ctx.cachePath.c_str());
            }
        }
        else
        {
            DEKI_LOG_EDITOR("ImageCache: No frame settings, skipping subasset generation");
        }
    }

    return result ? AssetCacheResult::Cached : AssetCacheResult::NotCached;
}

struct ImageCacheRegistrar
{
    ImageCacheRegistrar()
    {
        AssetPipeline::OnStarted([](AssetPipeline* p) {
            for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"})
                p->RegisterCacheHandler(ext, HandleSpriteImageCache);
        });
    }
};
static ImageCacheRegistrar s_ImageCacheRegistrar;

} // namespace
} // namespace DekiEditor

#endif // DEKI_EDITOR
