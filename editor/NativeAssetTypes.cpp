/**
 * @file NativeAssetTypes.cpp
 * @brief Asset type registrations for native (pre-compiled) types owned by deki-2d.
 *
 * Sprite (.png/.jpg/.dtex/...) and BitmapFont (.ttf/.otf/.dfont/.bdf) are native
 * file types — there is no JSON envelope to compile, no inspector inside this
 * editor (per-extension FileInspectors handle that). The only reason these
 * AssetTypeEditor subclasses exist is to bind the extension list to a typeName
 * via AssetTypeRegistry, so AssetPipeline::GetAssetTypeFromExtension stays out
 * of the business of knowing which module owns which file extension.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>

namespace DekiEditor
{

class SpriteAssetType : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "Sprite"; }
    const char* GetDisplayName() const override { return "Sprite"; }
    std::vector<std::string> GetExtensions() const override
    {
        return { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".dtex" };
    }
};

class BitmapFontAssetType : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "BitmapFont"; }
    const char* GetDisplayName() const override { return "Bitmap Font"; }
    std::vector<std::string> GetExtensions() const override
    {
        return { ".ttf", ".otf", ".dfont", ".bdf" };
    }
};

REGISTER_EDITOR(SpriteAssetType)
REGISTER_EDITOR(BitmapFontAssetType)

} // namespace DekiEditor

#endif // DEKI_EDITOR
