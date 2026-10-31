#include "ShapeComponent.h"

#include <deki/Engine.h>
#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>
#include "deki-rendering/Fingerprint.h"
#include "deki-rendering/QuadBlit.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Deki2D
{

namespace
{

// Empty pixels baked around a shape on each side. The soft edge reaches half a
// pixel past the shape, and landing between screen pixels can move it one more.
constexpr int32_t kBakePad = 2;

}  // namespace

ShapeComponent::~ShapeComponent() = default;

void ShapeComponent::SetSize(float newWidth, float newHeight)
{
    width = newWidth;
    height = newHeight;
}

void ShapeComponent::SetArc(float newStartAngle, float newEndAngle)
{
    startAngle = newStartAngle;
    endAngle = newEndAngle;
}

ShapeMaskParams ShapeComponent::MaskParams(float pixelsPerMeter) const
{
    ShapeMaskParams p;
    p.kind = kind;
    p.width = width * pixelsPerMeter;
    p.height = height * pixelsPerMeter;
    p.cornerRadius = cornerRadius * pixelsPerMeter;
    p.lineWidth = lineWidth * pixelsPerMeter;
    p.startAngle = startAngle;
    p.endAngle = endAngle;
    p.roundCaps = roundCaps;
    p.direction = direction;
    return p;
}

bool ShapeComponent::Reserve(int32_t w, int32_t h)
{
    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (m_Mask && m_Mask.Count() >= need)
    {
        return true;
    }
    // Allocate() frees the old mask first, so the two never exist at once.
    if (!m_Mask.Allocate(need, Deki::Memory::External))
    {
        DEKI_LOG_WARNING("ShapeComponent: no room for a %dx%d mask (%u bytes); not drawing it", (int)w, (int)h,
                         (unsigned)need);
        m_HasBake = false;
        return false;
    }
    return true;
}

uint8_t* ShapeComponent::BeginCustomMask(int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0 || !Reserve(w, h))
    {
        return nullptr;
    }
    m_MaskW = w;
    m_MaskH = h;
    m_MaskPPM = 0.0f;
    std::memset(m_Mask.Data(), 0, static_cast<size_t>(w) * static_cast<size_t>(h));
    return m_Mask.Data();
}

void ShapeComponent::EndCustomMask()
{
    m_HasBake = m_Mask && m_MaskW > 0 && m_MaskH > 0;
    ++m_BakeVersion;
}

uint64_t ShapeComponent::ComputeBakeKey(int32_t w, int32_t h, float pixelsPerMeter) const
{
    DekiRendering::Fingerprint f;
    f.Add(static_cast<uint32_t>(kind));
    f.Add(w);
    f.Add(h);
    f.Add(width * pixelsPerMeter);
    f.Add(height * pixelsPerMeter);
    f.Add(cornerRadius * pixelsPerMeter);
    f.Add(lineWidth * pixelsPerMeter);
    f.Add(startAngle);
    f.Add(endAngle);
    f.Add(static_cast<uint32_t>(roundCaps));
    f.Add(static_cast<uint32_t>(direction));
    return f.Value();
}

bool ShapeComponent::RenderContent(const Deki::Object* owner, QuadBlit::Source& outSource, float& outPivotX,
                                   float& outPivotY, uint8_t& outTintR, uint8_t& outTintG, uint8_t& outTintB,
                                   uint8_t& outTintA)
{
    if (!owner || width <= 0.0f || height <= 0.0f || color.a == 0)
    {
        return false;
    }

    if (kind != ShapeKind::Custom)
    {
        // Baked at the density it is drawn at, so the edges stay smooth when
        // the editor zooms in, but never past twice the view's area: a big
        // shape mostly off the view is drawn a little magnified instead.
        const float artPPM = Deki::EngineSettings::Global().pixelsPerMeter;
        const DekiRendering::DrawView& view = DekiRendering::CurrentDrawView();
        float bakePPM = view.pixelsPerMeter > 0.0f ? view.pixelsPerMeter : artPPM;
        const double viewArea = static_cast<double>(view.width) * view.height;
        const double bakeArea = static_cast<double>(width) * bakePPM * height * bakePPM;
        if (viewArea > 0.0 && bakeArea > viewArea * 2.0)
        {
            bakePPM *= static_cast<float>(std::sqrt(viewArea * 2.0 / bakeArea));
        }
        // The shape keeps its exact size in pixels, fractions included, and
        // the mask is drawn at the bake's own scale. A mask scaled to fit a
        // rounded size would scale both ways, so a fill that only grows wider
        // (a slider's) would also jump a pixel taller and shorter as it moved.
        // An even mask keeps the shape's centre on a pixel corner.
        const float shapeW = width * bakePPM;
        const float shapeH = height * bakePPM;
        const int32_t w = std::max<int32_t>(2, static_cast<int32_t>(std::ceil(shapeW * 0.5f)) * 2);
        const int32_t h = std::max<int32_t>(2, static_cast<int32_t>(std::ceil(shapeH * 0.5f)) * 2);

        const uint64_t key = ComputeBakeKey(w, h, bakePPM);
        if (!m_HasBake || key != m_BakeKey || m_MaskPPM != bakePPM)
        {
            const int32_t maskW = w + kBakePad * 2;
            const int32_t maskH = h + kBakePad * 2;
            if (!Reserve(maskW, maskH))
            {
                return false;
            }
            ShapeMaskParams params = MaskParams(bakePPM);
            BakeShapeMask(m_Mask.Data(), maskW, maskH, maskW, params);
            m_MaskW = maskW;
            m_MaskH = maskH;
            m_MaskPPM = bakePPM;
            m_BakeKey = key;
            m_HasBake = true;
            ++m_BakeVersion;
        }
    }

    if (!m_HasBake || m_MaskW <= 0 || m_MaskH <= 0)
    {
        return false;
    }

    outSource = QuadBlit::MakeSource(m_Mask.Data(), m_MaskW, m_MaskH, QuadBlit::PixelLayout::Alpha8(),
                                     false  // ownsPixels: the component keeps its mask
    );
    outSource.stride = m_MaskW;
    // A baked mask, padding and all, is drawn at the scale it was baked at; a
    // custom mask covers the shape's size.
    outSource.pixelsPerMeter = m_MaskPPM > 0.0f ? m_MaskPPM : static_cast<float>(m_MaskW) / width;

    outPivotX = 0.5f;
    outPivotY = 0.5f;

    outTintR = color.r;
    outTintG = color.g;
    outTintB = color.b;
    outTintA = color.a;
    return true;
}

}  // namespace Deki2D
