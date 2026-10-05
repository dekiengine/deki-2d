// Command-line and MCP tools that slice sprite sheets and fill frame
// animations, so 2D art can be set up from `--tool` / `--script` the way the
// Sprite Slicer and the Animation window do it.
#ifdef DEKI_EDITOR

#include <deki-editor/AssetData.h>
#include <deki-editor/AssetDatabase.h>
#include <deki-editor/CliTool.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/SubAsset.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace DekiEditor
{
namespace
{
using json = nlohmann::json;
namespace fs = std::filesystem;

bool ParseArgs(const std::string& argsJson, json& args, std::string& error)
{
    args = json::parse(argsJson, nullptr, /*allow_exceptions=*/false);
    if (!args.is_object())
    {
        error = "arguments must be a JSON object";
        return false;
    }
    return true;
}

// The frames of a sliced sheet, in slicing order.
std::vector<const SubAssetInfo*> SheetFrames(const std::string& sheetGuid)
{
    std::vector<const SubAssetInfo*> frames;
    if (const auto* subAssets = AssetDatabase::GetSubAssets(sheetGuid))
    {
        for (const SubAssetInfo& sub : *subAssets)
        {
            frames.push_back(&sub);
        }
    }
    std::sort(frames.begin(), frames.end(),
              [](const SubAssetInfo* a, const SubAssetInfo* b) { return a->subAssetIndex < b->subAssetIndex; });
    return frames;
}

class SpriteSheetGridTool : public CliTool
{
public:
    const char* GetToolName() const override { return "sprite_sheet_grid"; }
    const char* GetToolDescription() const override
    {
        return "Slice an imported image into a grid of equal frames, read left to right and top to bottom, as "
               "the Sprite Slicer's grid does. Frame ids become the frames' positions. Returns each frame's GUID.";
    }
    const char* GetInputSchema() const override
    {
        return R"json({"type":"object","required":["image","frame_width","frame_height"],"properties":{
            "image":{"type":"string","description":"The image, project-relative (e.g. assets/sprites/hero.png)"},
            "frame_width":{"type":"integer","description":"Frame width in pixels"},
            "frame_height":{"type":"integer","description":"Frame height in pixels"}}})json";
    }

    bool Run(const CliToolContext& context, const std::string& argsJson, std::string& resultJson,
             std::string& error) override
    {
        json args;
        if (!ParseArgs(argsJson, args, error))
        {
            return false;
        }
        if (context.projectPath.empty())
        {
            error = "no project is open";
            return false;
        }
        const std::string image = args.value("image", std::string());
        const int frameWidth = args.value("frame_width", 0);
        const int frameHeight = args.value("frame_height", 0);
        if (frameWidth <= 0 || frameHeight <= 0)
        {
            error = "frame_width and frame_height are pixel sizes above 0";
            return false;
        }

        const std::string guid = AssetDatabase::AssetPathToGUID(image);
        const std::string dataPath = GetAssetDataPath((fs::path(context.projectPath) / image).string());
        AssetData data;
        if (guid.empty() || !LoadAssetData(dataPath, data))
        {
            error = "'" + image + "' is not an imported image";
            return false;
        }

        json sprite = json::object();
        sprite["mode"] = "grid";
        sprite["frameWidth"] = frameWidth;
        sprite["frameHeight"] = frameHeight;
        data.settings["settings"]["sprite"] = sprite;
        if (!SaveAssetData(dataPath, data))
        {
            error = "could not write '" + image + ".data'";
            return false;
        }
        if (context.refreshAsset)
        {
            context.refreshAsset(image);
        }

        const std::vector<const SubAssetInfo*> frames = SheetFrames(guid);
        if (frames.empty())
        {
            error = "'" + image + "' is smaller than one frame";
            return false;
        }
        json guids = json::array();
        for (const SubAssetInfo* frame : frames)
        {
            guids.push_back(frame->guid);
        }
        resultJson = json{ { "sheet", guid }, { "frames", guids } }.dump();
        return true;
    }
};

class AnimationSetSequenceTool : public CliTool
{
public:
    const char* GetToolName() const override { return "animation_set_sequence"; }
    const char* GetToolDescription() const override
    {
        return "Add a sequence, such as \"Walk\", to a frame animation (.anim, made with create_asset), or replace "
               "the one with the same name. Its frames are positions in the animation's sprite sheet, which "
               "sprite_sheet_grid slices. Every sequence in an animation uses the same sheet.";
    }
    const char* GetInputSchema() const override
    {
        return R"json({"type":"object","required":["asset","sheet","name","frames"],"properties":{
            "asset":{"type":"string","description":"The animation, project-relative (e.g. assets/animations/hero.anim)"},
            "sheet":{"type":"string","description":"The sliced sprite sheet image, project-relative"},
            "name":{"type":"string","description":"Sequence name, as AnimationComponent::PlayAnimation takes it"},
            "frames":{"type":"array","items":{"type":"integer"},"description":"Frame positions in the sheet, in play order"},
            "duration_ms":{"type":"integer","description":"How long each frame shows, in milliseconds (default 100)"},
            "loop":{"type":"boolean","description":"Whether the sequence loops (default true)"}}})json";
    }

    bool Run(const CliToolContext& context, const std::string& argsJson, std::string& resultJson,
             std::string& error) override
    {
        json args;
        if (!ParseArgs(argsJson, args, error))
        {
            return false;
        }
        if (context.projectPath.empty())
        {
            error = "no project is open";
            return false;
        }
        const std::string asset = args.value("asset", std::string());
        const std::string name = args.value("name", std::string());
        const int duration = args.value("duration_ms", 100);
        if (name.empty())
        {
            error = "name the sequence in 'name'";
            return false;
        }
        if (duration <= 0)
        {
            error = "duration_ms must be above 0";
            return false;
        }

        const fs::path animPath = fs::path(context.projectPath) / asset;
        std::ifstream in(animPath);
        json anim = in ? json::parse(in, nullptr, /*allow_exceptions=*/false) : json();
        in.close();
        if (!anim.is_object() || !anim.contains("animations") || !anim["animations"].is_array())
        {
            error = "'" + asset + "' is not a frame animation";
            return false;
        }

        const std::string sheet = args.value("sheet", std::string());
        const std::string sheetGuid = AssetDatabase::AssetPathToGUID(sheet);
        if (sheetGuid.empty())
        {
            error = "'" + sheet + "' is not an imported image";
            return false;
        }
        const std::string current = anim.value("spritesheetGuid", std::string());
        if (!current.empty() && current != sheetGuid && !anim["animations"].empty())
        {
            error = "'" + asset + "' already uses another sheet (" + current + ")";
            return false;
        }

        const std::vector<const SubAssetInfo*> sheetFrames = SheetFrames(sheetGuid);
        if (sheetFrames.empty())
        {
            error = "'" + sheet + "' is not sliced into frames; use sprite_sheet_grid first";
            return false;
        }
        if (!args.contains("frames") || !args["frames"].is_array() || args["frames"].empty())
        {
            error = "list the sequence's frames in 'frames'";
            return false;
        }
        json frames = json::array();
        for (const json& position : args["frames"])
        {
            if (!position.is_number_integer() || position.get<int>() < 0 ||
                position.get<size_t>() >= sheetFrames.size())
            {
                error = "frames are positions from 0 to " + std::to_string(sheetFrames.size() - 1);
                return false;
            }
            frames.push_back({ { "frameGuid", sheetFrames[position.get<size_t>()]->guid }, { "duration", duration } });
        }

        json sequence = { { "name", name }, { "loop", args.value("loop", true) }, { "frames", frames } };
        json& sequences = anim["animations"];
        auto same = std::find_if(sequences.begin(), sequences.end(),
                                 [&name](const json& s) { return s.value("name", std::string()) == name; });
        if (same != sequences.end())
        {
            *same = sequence;
        }
        else
        {
            sequences.push_back(sequence);
        }
        anim["spritesheetGuid"] = sheetGuid;

        std::ofstream out(animPath, std::ios::trunc);
        out << anim.dump(2) << "\n";
        if (!out)
        {
            error = "could not write '" + asset + "'";
            return false;
        }
        out.close();
        if (context.refreshAsset)
        {
            context.refreshAsset(asset);
        }

        resultJson =
            json{ { "animation", AssetDatabase::AssetPathToGUID(asset) }, { "sequences", sequences.size() } }.dump();
        return true;
    }
};

}  // namespace

REGISTER_EDITOR(SpriteSheetGridTool)
REGISTER_EDITOR(AnimationSetSequenceTool)

}  // namespace DekiEditor

#endif  // DEKI_EDITOR
