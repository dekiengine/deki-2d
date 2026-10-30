// Editor support for TextComponent: its Inspector, baking the font sizes
// texts use, and its display size.

#ifdef DEKI_EDITOR

#include <deki-editor/EditorRegistry.h>
#include <deki-editor/CustomEditor.h>
#include <deki-editor/SceneView.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/Localization.h>
#include <deki-editor/AssetPipeline.h>
#include "editor/FontSyncHandler.h"
#include "TextComponent.h"
#include <deki/Engine.h>
#include <cstdio>
#include <string>
#include <filesystem>

// Editor extensions live in DekiEditor; the package's own types are in Deki2D.
using namespace Deki2D;

namespace DekiEditor
{

class TextComponentEditor : public CustomEditor
{
public:
    const char* GetComponentName() const override { return "TextComponent"; }

    // Every frame, for every text: lands the bakes that finished, and bakes
    // this text's size when it is missing. Running here rather than in the
    // Inspector covers every way a size changes: the field, undo, a script,
    // MCP set_property.
    void OnEditorUpdate(Deki::Component* comp) override
    {
        Deki2D::PumpFontBakes();

        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
        {
            return;
        }

        // A font picked as a plain asset (a BDF font) names its source in
        // font.guid; the font resolve hook works from font.source.
        if (textComp->font.source.empty() && !textComp->font.guid.empty() && IsFontSource(textComp->font.guid))
        {
            textComp->font.source = textComp->font.guid;
        }

        if (textComp->font.source != m_RequestedSource || textComp->fontSize != m_RequestedSize)
        {
            m_RequestedSource = textComp->font.source;
            m_RequestedSize = textComp->fontSize;
            if (!m_RequestedSource.empty())
            {
                Deki2D::EnsureFontSizeBaked(m_RequestedSource, m_RequestedSize, textComp);
            }
        }
    }

private:
    // The font and size this text last asked to have baked. Each component
    // gets its own editor instance.
    std::string m_RequestedSource;
    int32_t m_RequestedSize = 0;

    static std::string LowerExtension(const std::string& guid)
    {
        auto* pipeline = DekiEditor::AssetPipeline::Instance();
        const DekiEditor::AssetInfo* info = pipeline ? pipeline->GetAssetInfoByGuid(guid) : nullptr;
        if (!info)
        {
            return "";
        }
        std::string ext = std::filesystem::path(info->path).extension().string();
        for (char& c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    // True for a font file the editor bakes (TTF, OTF, BDF).
    static bool IsFontSource(const std::string& guid)
    {
        const std::string ext = LowerExtension(guid);
        return ext == ".ttf" || ext == ".otf" || ext == ".bdf";
    }

public:
    // width/height are world meters; gizmo consumers want pixels.
    bool GetDisplaySize(Deki::Component* comp, float& outWidth, float& outHeight) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
        {
            return false;
        }

        const float ppm = Deki::EngineSettings::Global().pixelsPerMeter;
        const float effective = ppm > 0.0f ? ppm : 1.0f;
        outWidth = textComp->width * effective;
        outHeight = textComp->height * effective;
        return true;
    }

    bool HitTest(Deki::Component* comp, float localX, float localY, float width, float height) override
    {
        float halfW = width * 0.5f;
        float halfH = height * 0.5f;
        return (localX >= -halfW && localX <= halfW && localY >= -halfH && localY <= halfH);
    }

    bool WantsInspectorOverride(Deki::Component* comp) override { return true; }

    void OnInspectorGUI(Deki::Component* comp) override
    {
        auto* textComp = static_cast<TextComponent*>(comp);
        if (!textComp)
        {
            return;
        }

        auto& gui = EditorUI::Get();

        gui.PropertyField("text");
        gui.PropertyField("font");

        const std::string& sourceGuid = textComp->font.source;
        const bool isBdfFont = !sourceGuid.empty() && LowerExtension(sourceGuid) == ".bdf";

        // Pixel Scale (useful for bitmap/BDF fonts, available for all)
        {
            gui.PropertyRow("Pixel Scale");
            // Styled drag int (no +/- steppers); DragInt clamps to [1, 8].
            gui.DragInt("##pixelScale", &textComp->pixelScale, 1.0f, 1, 8);
        }

        // A BDF font has one size, so it has no Font Size.
        if (!isBdfFont)
        {
            gui.PropertyField("fontSize");
            const int32_t size = textComp->fontSize;
            if (size < Deki2D::kMinFontSize || size > Deki2D::kMaxFontSize)
            {
                char buf[128];
                std::snprintf(buf, sizeof(buf), Tr("Sizes from %d to %d px can be baked."), Deki2D::kMinFontSize,
                              Deki2D::kMaxFontSize);
                gui.Hint(buf);
            }
            else if (!sourceGuid.empty() && Deki2D::IsFontSizeBaking(sourceGuid, size))
            {
                gui.Hint(Tr("Baking this size..."));
            }
            else if (!sourceGuid.empty() && Deki2D::DidFontSizeBakeFail(sourceGuid, size))
            {
                gui.Hint(Tr("This size could not be baked. The log says why."));
            }
        }

        gui.PropertyField("width");
        gui.PropertyField("height");
        gui.PropertyField("color");
        gui.PropertyField("decorationColor");
        gui.PropertyField("align");
        gui.PropertyField("verticalAlign");
        gui.PropertyField("sortingOrder");
    }
};

REGISTER_EDITOR(TextComponentEditor)
REGISTER_CREATE_MENU_ITEM(TextComponent, "2D", "Text", "Text", "TextComponent")

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
