// Asset type registrations for deki-2d's native (already compiled) types.
//
// Sprite (.png/.jpg/.dtex/...) and BitmapFont (.ttf/.otf/.dfont/.bdf) are
// native file types: there is no JSON envelope to compile, and per-extension
// FileInspectors provide their inspectors. These AssetTypeEditor subclasses
// only bind the extensions to a typeName in AssetTypeRegistry, so
// AssetPipeline::GetAssetTypeFromExtension need not know which package owns
// which extension.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/AssetTypeRegistry.h>

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
    std::vector<std::string> GetExtensions() const override { return { ".ttf", ".otf", ".dfont", ".bdf" }; }
};

REGISTER_EDITOR(SpriteAssetType)
REGISTER_EDITOR(BitmapFontAssetType)

// Category registration via static initializer keeps AssetTypeEditor's
// vtable stable across package rebuilds (see EditorExtension.h note).
namespace
{
struct NativeAssetCategoryRegistrar
{
    NativeAssetCategoryRegistrar()
    {
        auto& reg = AssetTypeRegistry::Instance();
        for (const char* ext : { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".dtex" })
        {
            reg.RegisterCategory(ext, AssetCategory::Texture);
        }
        for (const char* ext : { ".ttf", ".otf", ".dfont", ".bdf" })
        {
            reg.RegisterCategory(ext, AssetCategory::Font);
        }
    }
};
static NativeAssetCategoryRegistrar s_NativeAssetCategoryRegistrar;
}  // namespace

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
