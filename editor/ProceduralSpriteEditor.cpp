/**
 * @file ProceduralSpriteEditor.cpp
 * @brief Asset type editor for procedural sprites (rounded rectangles, circles, etc.)
 *
 * This file is only compiled into the editor, not the runtime.
 * Uses SDF-based rendering for antialiased procedural shapes.
 */

#ifdef DEKI_EDITOR

#include <deki-editor/EditorExtension.h>
#include <deki-editor/EditorRegistry.h>
#include <deki-editor/EditorApplication.h>
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace DekiEditor
{

// ============================================================================
// SDF Rendering Functions
// ============================================================================

static float smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static float circleSDF(float px, float py, float radius)
{
    return sqrtf(px * px + py * py) - radius;
}

static float ellipseSDF(float px, float py, float radiusX, float radiusY)
{
    if (fabsf(radiusX - radiusY) < 0.001f)
    {
        return circleSDF(px, py, radiusX);
    }
    float nx = px / radiusX;
    float ny = py / radiusY;
    float d = sqrtf(nx * nx + ny * ny);
    float minRadius = (std::min)(radiusX, radiusY);
    return (d - 1.0f) * minRadius;
}

static float roundedRectSDF(float px, float py, float halfW, float halfH, float radius)
{
    float maxRadius = (std::min)(halfW, halfH);
    if (radius >= maxRadius)
    {
        return ellipseSDF(px, py, halfW, halfH);
    }

    float r = radius;
    float qx = fabsf(px) - halfW + r;
    float qy = fabsf(py) - halfH + r;
    float outsideX = (std::max)(qx, 0.0f);
    float outsideY = (std::max)(qy, 0.0f);
    float outsideDist = sqrtf(outsideX * outsideX + outsideY * outsideY);
    float insideDist = (std::min)((std::max)(qx, qy), 0.0f);
    return outsideDist + insideDist - r;
}

static float roundedRectSDFPerCorner(float px, float py, float halfW, float halfH,
                                      float rTL, float rTR, float rBR, float rBL)
{
    // Select corner radius based on quadrant
    float r;
    if (px < 0.0f && py < 0.0f)       r = rTL;
    else if (px >= 0.0f && py < 0.0f)  r = rTR;
    else if (px >= 0.0f && py >= 0.0f) r = rBR;
    else                                r = rBL;

    float maxRadius = (std::min)(halfW, halfH);
    if (r >= maxRadius)
    {
        return ellipseSDF(px, py, halfW, halfH);
    }

    float qx = fabsf(px) - halfW + r;
    float qy = fabsf(py) - halfH + r;
    float outsideX = (std::max)(qx, 0.0f);
    float outsideY = (std::max)(qy, 0.0f);
    float outsideDist = sqrtf(outsideX * outsideX + outsideY * outsideY);
    float insideDist = (std::min)((std::max)(qx, qy), 0.0f);
    return outsideDist + insideDist - r;
}

static void BlendColors(uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA,
                        uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1,
                        uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2,
                        float blend)
{
    float inv = 1.0f - blend;
    outR = static_cast<uint8_t>(r1 * inv + r2 * blend);
    outG = static_cast<uint8_t>(g1 * inv + g2 * blend);
    outB = static_cast<uint8_t>(b1 * inv + b2 * blend);
    outA = static_cast<uint8_t>(a1 * inv + a2 * blend);
}

static void RenderProceduralSpriteToRGBA(uint8_t* buffer, int32_t totalW, int32_t totalH,
                                          uint8_t bgR, uint8_t bgG, uint8_t bgB, uint8_t bgA,
                                          int32_t borderTop, int32_t borderRight, int32_t borderBottom, int32_t borderLeft,
                                          uint8_t btR, uint8_t btG, uint8_t btB, uint8_t btA,
                                          uint8_t brR, uint8_t brG, uint8_t brB, uint8_t brA,
                                          uint8_t bbR, uint8_t bbG, uint8_t bbB, uint8_t bbA,
                                          uint8_t blR, uint8_t blG, uint8_t blB, uint8_t blA,
                                          int32_t radiusTL, int32_t radiusTR, int32_t radiusBR, int32_t radiusBL,
                                          bool antialiased)
{
    float fRadiusTL = static_cast<float>(radiusTL);
    float fRadiusTR = static_cast<float>(radiusTR);
    float fRadiusBR = static_cast<float>(radiusBR);
    float fRadiusBL = static_cast<float>(radiusBL);

    float halfW = static_cast<float>(totalW) / 2.0f;
    float halfH = static_cast<float>(totalH) / 2.0f;
    float centerX = halfW;
    float centerY = halfH;

    float fBorderTop = static_cast<float>(borderTop);
    float fBorderRight = static_cast<float>(borderRight);
    float fBorderBottom = static_cast<float>(borderBottom);
    float fBorderLeft = static_cast<float>(borderLeft);

    float avgBorderH = (fBorderLeft + fBorderRight) / 2.0f;
    float avgBorderV = (fBorderTop + fBorderBottom) / 2.0f;
    float innerHalfW = halfW - avgBorderH;
    float innerHalfH = halfH - avgBorderV;

    // Offset inner rect center so each side gets its exact border width (CSS-like)
    float innerOffsetX = (fBorderLeft - fBorderRight) / 2.0f;
    float innerOffsetY = (fBorderTop - fBorderBottom) / 2.0f;

    // Per-corner inner radii shrink by adjacent border widths
    float innerRadiusTL = (std::max)(fRadiusTL - (std::max)(fBorderTop, fBorderLeft), 0.0f);
    float innerRadiusTR = (std::max)(fRadiusTR - (std::max)(fBorderTop, fBorderRight), 0.0f);
    float innerRadiusBR = (std::max)(fRadiusBR - (std::max)(fBorderBottom, fBorderRight), 0.0f);
    float innerRadiusBL = (std::max)(fRadiusBL - (std::max)(fBorderBottom, fBorderLeft), 0.0f);

    bool hasBorder = (borderTop > 0 || borderRight > 0 || borderBottom > 0 || borderLeft > 0);

    for (int32_t y = 0; y < totalH; y++)
    {
        for (int32_t x = 0; x < totalW; x++)
        {
            uint8_t r, g, b, a;

            float px = static_cast<float>(x) + 0.5f - centerX;
            float py = static_cast<float>(y) + 0.5f - centerY;

            if (antialiased)
            {
                float outerDist = roundedRectSDFPerCorner(px, py, halfW, halfH, fRadiusTL, fRadiusTR, fRadiusBR, fRadiusBL);
                float coverage = 1.0f - smoothstep(-0.5f, 0.5f, outerDist);

                if (coverage <= 0.0f)
                {
                    r = g = b = a = 0;
                }
                else if (!hasBorder)
                {
                    r = bgR; g = bgG; b = bgB;
                    a = static_cast<uint8_t>(static_cast<float>(bgA) * coverage);
                }
                else
                {
                    float innerDist = roundedRectSDFPerCorner(px - innerOffsetX, py - innerOffsetY, innerHalfW, innerHalfH, innerRadiusTL, innerRadiusTR, innerRadiusBR, innerRadiusBL);

                    uint8_t borderR, borderG, borderB, borderA;
                    float absX = fabsf(px);

                    if (py < -absX) {
                        borderR = btR; borderG = btG; borderB = btB; borderA = btA;
                    } else if (py > absX) {
                        borderR = bbR; borderG = bbG; borderB = bbB; borderA = bbA;
                    } else if (px > 0) {
                        borderR = brR; borderG = brG; borderB = brB; borderA = brA;
                    } else {
                        borderR = blR; borderG = blG; borderB = blB; borderA = blA;
                    }

                    float borderBlend;
                    if (innerHalfW <= 0 || innerHalfH <= 0)
                    {
                        borderBlend = 1.0f;
                    }
                    else
                    {
                        borderBlend = smoothstep(-0.5f, 0.5f, innerDist);
                    }

                    BlendColors(r, g, b, a, bgR, bgG, bgB, bgA, borderR, borderG, borderB, borderA, borderBlend);
                    a = static_cast<uint8_t>(static_cast<float>(a) * coverage);
                }
            }
            else
            {
                float outerDist = roundedRectSDFPerCorner(px, py, halfW, halfH, fRadiusTL, fRadiusTR, fRadiusBR, fRadiusBL);

                if (outerDist > 0.0f)
                {
                    r = g = b = a = 0;
                }
                else if (!hasBorder)
                {
                    r = bgR; g = bgG; b = bgB; a = bgA;
                }
                else
                {
                    float innerDist = roundedRectSDFPerCorner(px - innerOffsetX, py - innerOffsetY, innerHalfW, innerHalfH, innerRadiusTL, innerRadiusTR, innerRadiusBR, innerRadiusBL);

                    if (innerHalfW <= 0 || innerHalfH <= 0 || innerDist > 0.0f)
                    {
                        float absX = fabsf(px);

                        if (py < -absX) {
                            r = btR; g = btG; b = btB; a = btA;
                        } else if (py > absX) {
                            r = bbR; g = bbG; b = bbB; a = bbA;
                        } else if (px > 0) {
                            r = brR; g = brG; b = brB; a = brA;
                        } else {
                            r = blR; g = blG; b = blB; a = blA;
                        }
                    }
                    else
                    {
                        r = bgR; g = bgG; b = bgB; a = bgA;
                    }
                }
            }

            int idx = (y * totalW + x) * 4;
            buffer[idx + 0] = r;
            buffer[idx + 1] = g;
            buffer[idx + 2] = b;
            buffer[idx + 3] = a;
        }
    }
}

// ============================================================================
// JSON Parsing Helpers
// ============================================================================

// Bold section header, matching the grouping style used in InspectorPanel /
// ModuleManagerPanel. More prominent than SeparatorText because the label
// uses the bold font and sits above a full-width separator line.
static void SectionHeader(const char* title)
{
    ImGui::Spacing();
    ImFont* boldFont = (ImGui::GetIO().Fonts->Fonts.Size > 1) ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;
    if (boldFont) ImGui::PushFont(boldFont);
    ImGui::TextUnformatted(title);
    if (boldFont) ImGui::PopFont();
    ImGui::Separator();
}

// Group header — one level above SectionHeader. Uses the theme accent colour
// so it's visibly distinct from the neutral white subsection titles, making
// clear that everything that follows is part of this group (pair with
// ImGui::Indent() / Unindent() around the grouped content).
static void GroupHeader(const char* title)
{
    ImGui::Spacing();
    ImFont* boldFont = (ImGui::GetIO().Fonts->Fonts.Size > 1) ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;
    if (boldFont) ImGui::PushFont(boldFont);
    ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    ImGui::TextColored(accent, "%s", title);
    if (boldFont) ImGui::PopFont();
    ImGui::Separator();
}

static void ParseBorderWidth(const nlohmann::json& data, int32_t& top, int32_t& right, int32_t& bottom, int32_t& left)
{
    if (data.contains("border_width"))
    {
        const auto& bw = data["border_width"];
        if (bw.is_number())
        {
            top = right = bottom = left = bw.get<int32_t>();
        }
        else if (bw.is_array())
        {
            if (bw.size() == 1)
            {
                top = right = bottom = left = bw[0].get<int32_t>();
            }
            else if (bw.size() == 2)
            {
                top = bottom = bw[0].get<int32_t>();
                right = left = bw[1].get<int32_t>();
            }
            else if (bw.size() == 3)
            {
                top = bw[0].get<int32_t>();
                right = left = bw[1].get<int32_t>();
                bottom = bw[2].get<int32_t>();
            }
            else if (bw.size() >= 4)
            {
                top = bw[0].get<int32_t>();
                right = bw[1].get<int32_t>();
                bottom = bw[2].get<int32_t>();
                left = bw[3].get<int32_t>();
            }
        }
    }
}

static void ParseBorderRadius(const nlohmann::json& data, int32_t& tl, int32_t& tr, int32_t& br, int32_t& bl)
{
    if (data.contains("border_radius"))
    {
        const auto& br_val = data["border_radius"];
        if (br_val.is_number())
        {
            tl = tr = br = bl = br_val.get<int32_t>();
        }
        else if (br_val.is_array())
        {
            if (br_val.size() == 1)
            {
                tl = tr = br = bl = br_val[0].get<int32_t>();
            }
            else if (br_val.size() == 2)
            {
                tl = br = br_val[0].get<int32_t>();
                tr = bl = br_val[1].get<int32_t>();
            }
            else if (br_val.size() == 3)
            {
                tl = br_val[0].get<int32_t>();
                tr = bl = br_val[1].get<int32_t>();
                br = br_val[2].get<int32_t>();
            }
            else if (br_val.size() >= 4)
            {
                tl = br_val[0].get<int32_t>();
                tr = br_val[1].get<int32_t>();
                br = br_val[2].get<int32_t>();
                bl = br_val[3].get<int32_t>();
            }
        }
    }
}

static void ParseNineSlice(const nlohmann::json& data, int32_t& top, int32_t& right, int32_t& bottom, int32_t& left)
{
    top = right = bottom = left = 0;
    if (!data.contains("nine_slice")) return;
    const auto& ns = data["nine_slice"];
    if (!ns.is_array() || ns.size() < 4) return;
    top    = ns[0].get<int32_t>();
    right  = ns[1].get<int32_t>();
    bottom = ns[2].get<int32_t>();
    left   = ns[3].get<int32_t>();
}

static void ParseColor(const nlohmann::json& data, const char* key, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a)
{
    if (data.contains(key) && data[key].is_array() && data[key].size() >= 4)
    {
        r = static_cast<uint8_t>(data[key][0].get<int>());
        g = static_cast<uint8_t>(data[key][1].get<int>());
        b = static_cast<uint8_t>(data[key][2].get<int>());
        a = static_cast<uint8_t>(data[key][3].get<int>());
    }
}

// ============================================================================
// ProceduralSpriteEditor Class
// ============================================================================

class ProceduralSpriteEditor : public AssetTypeEditor
{
public:
    const char* GetTypeName() const override { return "ProceduralSprite"; }
    const char* GetDisplayName() const override { return "Procedural Sprite"; }
    const char* GetExtension() const override { return ".asset"; }

    const char* GetDefaultContent() const override
    {
        return R"({
  "type": "ProceduralSprite",
  "width": 64,
  "height": 64,
  "background_color": [255, 255, 255, 255],
  "border_width": 2,
  "border_color": [0, 0, 0, 255],
  "border_radius": 8
})";
    }

    int GetCompileTarget() const override { return 0; }  // Texture

    bool Compile(const std::string& jsonData,
                 std::vector<uint8_t>& rgba,
                 int& width, int& height) override
    {
        try
        {
            auto data = nlohmann::json::parse(jsonData);

            int32_t contentW = data.value("width", 64);
            int32_t contentH = data.value("height", 64);

            int32_t borderTop = 0, borderRight = 0, borderBottom = 0, borderLeft = 0;
            ParseBorderWidth(data, borderTop, borderRight, borderBottom, borderLeft);

            int32_t totalW = contentW + borderLeft + borderRight;
            int32_t totalH = contentH + borderTop + borderBottom;

            uint8_t bgR = 255, bgG = 255, bgB = 255, bgA = 255;
            uint8_t btR = 0, btG = 0, btB = 0, btA = 255;
            uint8_t brR = 0, brG = 0, brB = 0, brA = 255;
            uint8_t bbR = 0, bbG = 0, bbB = 0, bbA = 255;
            uint8_t blR = 0, blG = 0, blB = 0, blA = 255;

            ParseColor(data, "background_color", bgR, bgG, bgB, bgA);

            if (data.contains("border_color"))
            {
                ParseColor(data, "border_color", btR, btG, btB, btA);
                brR = btR; brG = btG; brB = btB; brA = btA;
                bbR = btR; bbG = btG; bbB = btB; bbA = btA;
                blR = btR; blG = btG; blB = btB; blA = btA;
            }

            ParseColor(data, "border_top_color", btR, btG, btB, btA);
            ParseColor(data, "border_right_color", brR, brG, brB, brA);
            ParseColor(data, "border_bottom_color", bbR, bbG, bbB, bbA);
            ParseColor(data, "border_left_color", blR, blG, blB, blA);

            int32_t radiusTL = 0, radiusTR = 0, radiusBR = 0, radiusBL = 0;
            ParseBorderRadius(data, radiusTL, radiusTR, radiusBR, radiusBL);

            bool antialiased = data.value("antialiased", false);

            rgba.resize(totalW * totalH * 4);

            RenderProceduralSpriteToRGBA(rgba.data(), totalW, totalH,
                                          bgR, bgG, bgB, bgA,
                                          borderTop, borderRight, borderBottom, borderLeft,
                                          btR, btG, btB, btA,
                                          brR, brG, brB, brA,
                                          bbR, bbG, bbB, bbA,
                                          blR, blG, blB, blA,
                                          radiusTL, radiusTR, radiusBR, radiusBL,
                                          antialiased);

            width = totalW;
            height = totalH;
            return true;
        }
        catch (const std::exception& e)
        {
            printf("[ProceduralSpriteEditor] Compile error: %s\n", e.what());
            return false;
        }
    }

    bool OnInspectorGUI(std::string& jsonData,
                        const std::string& assetPath,
                        const std::string& assetGuid) override
    {
        try
        {
            auto data = nlohmann::json::parse(jsonData);
            bool modified = false;

            // Group header — accent-coloured so the subsections below read
            // as belonging to "Procedural Sprite" without needing an indent.
            GroupHeader("Procedural Sprite");
            {
                float lw = ImGui::GetContentRegionAvail().x * 0.4f;

                // ── Dimensions ───────────────────────────────────────────
                SectionHeader("Dimensions");
                int width = data.value("width", 64);
                int height = data.value("height", 64);
                ImGui::AlignTextToFramePadding(); ImGui::Text("Width"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##width", &width, 1.0f, 1, 1024))
                {
                    data["width"] = width;
                    modified = true;
                }
                ImGui::AlignTextToFramePadding(); ImGui::Text("Height"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##height", &height, 1.0f, 1, 1024))
                {
                    data["height"] = height;
                    modified = true;
                }

                // ── Background ───────────────────────────────────────────
                SectionHeader("Background");
                float bgColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                if (data.contains("background_color") && data["background_color"].is_array() && data["background_color"].size() >= 4)
                {
                    bgColor[0] = data["background_color"][0].get<int>() / 255.0f;
                    bgColor[1] = data["background_color"][1].get<int>() / 255.0f;
                    bgColor[2] = data["background_color"][2].get<int>() / 255.0f;
                    bgColor[3] = data["background_color"][3].get<int>() / 255.0f;
                }
                ImGui::AlignTextToFramePadding(); ImGui::Text("Color"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::ColorEdit4("##bgColor", bgColor))
                {
                    data["background_color"] = {
                        static_cast<int>(bgColor[0] * 255),
                        static_cast<int>(bgColor[1] * 255),
                        static_cast<int>(bgColor[2] * 255),
                        static_cast<int>(bgColor[3] * 255)
                    };
                    modified = true;
                }

                // ── Border Width ─────────────────────────────────────────
                SectionHeader("Border Width");
                int32_t bwTop = 0, bwRight = 0, bwBottom = 0, bwLeft = 0;
                ParseBorderWidth(data, bwTop, bwRight, bwBottom, bwLeft);

                bool bwChanged = false;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Top"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##bwTop", &bwTop, 1.0f, 0, 100)) bwChanged = true;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Right"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##bwRight", &bwRight, 1.0f, 0, 100)) bwChanged = true;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Bottom"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##bwBottom", &bwBottom, 1.0f, 0, 100)) bwChanged = true;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Left"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##bwLeft", &bwLeft, 1.0f, 0, 100)) bwChanged = true;

                if (bwChanged)
                {
                    data["border_width"] = {bwTop, bwRight, bwBottom, bwLeft};
                    modified = true;
                }

                // ── Border Color ─────────────────────────────────────────
                SectionHeader("Border Color");
                float borderColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                if (data.contains("border_color") && data["border_color"].is_array() && data["border_color"].size() >= 4)
                {
                    borderColor[0] = data["border_color"][0].get<int>() / 255.0f;
                    borderColor[1] = data["border_color"][1].get<int>() / 255.0f;
                    borderColor[2] = data["border_color"][2].get<int>() / 255.0f;
                    borderColor[3] = data["border_color"][3].get<int>() / 255.0f;
                }
                ImGui::AlignTextToFramePadding(); ImGui::Text("Color"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::ColorEdit4("##borderColor", borderColor))
                {
                    data["border_color"] = {
                        static_cast<int>(borderColor[0] * 255),
                        static_cast<int>(borderColor[1] * 255),
                        static_cast<int>(borderColor[2] * 255),
                        static_cast<int>(borderColor[3] * 255)
                    };
                    data.erase("border_top_color");
                    data.erase("border_right_color");
                    data.erase("border_bottom_color");
                    data.erase("border_left_color");
                    modified = true;
                }

                // ── Border Radius ────────────────────────────────────────
                SectionHeader("Border Radius");
                int32_t brTL = 0, brTR = 0, brBR = 0, brBL = 0;
                ParseBorderRadius(data, brTL, brTR, brBR, brBL);

                bool brChanged = false;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Top Left"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##brTL", &brTL, 1.0f, 0, 200)) brChanged = true;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Top Right"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##brTR", &brTR, 1.0f, 0, 200)) brChanged = true;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Bottom Right"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##brBR", &brBR, 1.0f, 0, 200)) brChanged = true;
                ImGui::AlignTextToFramePadding(); ImGui::Text("Bottom Left"); ImGui::SameLine(lw); ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt("##brBL", &brBL, 1.0f, 0, 200)) brChanged = true;

                if (brChanged)
                {
                    data["border_radius"] = {brTL, brTR, brBR, brBL};
                    modified = true;
                }

                // ── 9-Slice ──────────────────────────────────────────────
                SectionHeader("9-Slice");
                {
                    int32_t nsTop = 0, nsRight = 0, nsBottom = 0, nsLeft = 0;
                    ParseNineSlice(data, nsTop, nsRight, nsBottom, nsLeft);
                    ImGui::Text("L:%d  R:%d  T:%d  B:%d", nsLeft, nsRight, nsTop, nsBottom);
                    ImGui::SameLine();
                    if (ImGui::Button("Edit 9-Slice..."))
                    {
                        EditorApplication::Get().RequestOpenTool(assetPath, /*cachePath*/ "");
                    }
                }

                // ── Misc ─────────────────────────────────────────────────
                SectionHeader("Misc");
                bool antialiased = data.value("antialiased", false);
                ImGui::AlignTextToFramePadding(); ImGui::Text("Antialiased"); ImGui::SameLine(lw);
                if (ImGui::Checkbox("##antialiased", &antialiased))
                {
                    data["antialiased"] = antialiased;
                    modified = true;
                }

            }

            if (modified)
            {
                jsonData = data.dump();
            }
            return modified;
        }
        catch (const std::exception& e)
        {
            printf("[ProceduralSpriteEditor] Inspector error: %s\n", e.what());
            return false;
        }
    }
};

// Auto-register
REGISTER_EDITOR(ProceduralSpriteEditor)

} // namespace DekiEditor

#endif // DEKI_EDITOR
