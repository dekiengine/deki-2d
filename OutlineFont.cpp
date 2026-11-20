#include "OutlineFont.h"

#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Deki2D
{

namespace
{

// How many straight pieces a curve is drawn with, from how far (in pixels)
// its middle bends away from a straight line: enough that no piece strays
// more than about a tenth of a pixel.
int32_t CurvePieces(float bend)
{
    return std::clamp(1 + static_cast<int32_t>(std::sqrt(bend * 2.0f)), 1, 64);
}

}  // namespace

bool OutlineFont::IsOutlineFile(const uint8_t* data, size_t size)
{
    return data && size >= sizeof(OutlineFontHeader) && memcmp(data, "DVEC", 4) == 0;
}

OutlineFont* OutlineFont::FromFileData(const uint8_t* data, size_t size)
{
    if (!IsOutlineFile(data, size))
    {
        DEKI_LOG_ERROR("OutlineFont: not a .dvec file");
        return nullptr;
    }
    OutlineFontHeader header;
    memcpy(&header, data, sizeof(header));
    if (header.version != 1 || header.unitsPerEm == 0)
    {
        DEKI_LOG_ERROR("OutlineFont: unsupported version %u", static_cast<unsigned>(header.version));
        return nullptr;
    }

    // Every table must fit in the file; counts come from the file and are
    // checked before they are multiplied out.
    const uint64_t codepointsAt = sizeof(OutlineFontHeader);
    const uint64_t glyphsAt = codepointsAt + uint64_t{ header.glyphCount } * sizeof(uint32_t);
    const uint64_t contoursAt = glyphsAt + uint64_t{ header.glyphCount } * sizeof(OutlineGlyph);
    const uint64_t pointsAt = contoursAt + uint64_t{ header.contourCount } * sizeof(uint16_t);
    const uint64_t end = pointsAt + uint64_t{ header.pointCount } * sizeof(OutlinePoint);
    if (end > size || header.glyphCount == 0)
    {
        DEKI_LOG_ERROR("OutlineFont: the file is shorter than its tables");
        return nullptr;
    }

    OutlineFont* font = new OutlineFont();
    if (!font->m_Data.Allocate(static_cast<size_t>(end), Deki::Memory::External))
    {
        DEKI_LOG_ERROR("OutlineFont: no room for a %u byte font", static_cast<unsigned>(end));
        delete font;
        return nullptr;
    }
    memcpy(font->m_Data.Data(), data, static_cast<size_t>(end));
    const uint8_t* base = font->m_Data.Data();
    font->m_Header = header;
    font->m_Codepoints = reinterpret_cast<const uint32_t*>(base + codepointsAt);
    font->m_Glyphs = reinterpret_cast<const OutlineGlyph*>(base + glyphsAt);
    font->m_ContourEnds = reinterpret_cast<const uint16_t*>(base + contoursAt);
    font->m_Points = reinterpret_cast<const OutlinePoint*>(base + pointsAt);

    // Each glyph's runs stay inside the tables, its contours end in order
    // inside its points, and the codepoints are sorted for the search.
    for (uint32_t i = 0; i < header.glyphCount; ++i)
    {
        const OutlineGlyph& glyph = font->m_Glyphs[i];
        bool good = uint64_t{ glyph.firstContour } + glyph.contourCount <= header.contourCount &&
                    (i == 0 || font->m_Codepoints[i - 1] < font->m_Codepoints[i]) && glyph.xMin <= glyph.xMax &&
                    glyph.yMin <= glyph.yMax;
        uint32_t last = 0;
        for (uint16_t c = 0; good && c < glyph.contourCount; ++c)
        {
            const uint32_t contourEnd = font->m_ContourEnds[glyph.firstContour + c];
            good = (c == 0 || contourEnd > last) && uint64_t{ glyph.firstPoint } + contourEnd < header.pointCount;
            last = contourEnd;
        }
        if (!good)
        {
            DEKI_LOG_ERROR("OutlineFont: glyph %u does not hold together", static_cast<unsigned>(i));
            delete font;
            return nullptr;
        }
    }
    return font;
}

void OutlineFont::AddLine(float x0, float y0, float x1, float y1) const
{
    // The signed area each pixel gains, accumulated along rows afterwards: a
    // line going down adds, one going up takes away.
    if (y0 == y1)
    {
        return;
    }
    float direction = 1.0f;
    if (y0 > y1)
    {
        direction = -1.0f;
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const float maxX = static_cast<float>(m_AccWidth);
    const float maxY = static_cast<float>(m_AccHeight);
    // Outside the image above or below: nothing; at the sides it is kept in
    // by clamping, as the glyph's box holds it.
    if (y1 <= 0.0f || y0 >= maxY)
    {
        return;
    }
    const float slope = (x1 - x0) / (y1 - y0);
    if (y0 < 0.0f)
    {
        x0 -= y0 * slope;
        y0 = 0.0f;
    }
    if (y1 > maxY)
    {
        y1 = maxY;
    }
    const int32_t stride = m_AccWidth + 2;
    float* acc = m_Accumulation.Data();
    float x = x0;
    for (int32_t row = static_cast<int32_t>(y0); row < m_AccHeight && static_cast<float>(row) < y1; ++row)
    {
        float* line = acc + static_cast<size_t>(row) * stride;
        const float rowY = static_cast<float>(row);
        const float dy = std::min(rowY + 1.0f, y1) - std::max(rowY, y0);
        const float xNext = x + slope * dy;
        const float d = dy * direction;
        float left = std::clamp(std::min(x, xNext), 0.0f, maxX);
        float right = std::clamp(std::max(x, xNext), 0.0f, maxX);
        const float leftFloor = std::floor(left);
        const int32_t leftI = static_cast<int32_t>(leftFloor);
        const float rightCeil = std::ceil(right);
        const int32_t rightI = static_cast<int32_t>(rightCeil);
        if (rightI <= leftI + 1)
        {
            // Within one pixel: split by where its middle crosses.
            const float middle = 0.5f * (left + right) - leftFloor;
            line[leftI] += d - d * middle;
            line[leftI + 1] += d * middle;
        }
        else
        {
            const float s = 1.0f / (right - left);
            const float leftFrac = left - leftFloor;
            const float a0 = 0.5f * s * (1.0f - leftFrac) * (1.0f - leftFrac);
            const float rightFrac = right - rightCeil + 1.0f;
            const float aEnd = 0.5f * s * rightFrac * rightFrac;
            line[leftI] += d * a0;
            if (rightI == leftI + 2)
            {
                line[leftI + 1] += d * (1.0f - a0 - aEnd);
            }
            else
            {
                const float a1 = s * (1.5f - leftFrac);
                line[leftI + 1] += d * (a1 - a0);
                for (int32_t xi = leftI + 2; xi < rightI - 1; ++xi)
                {
                    line[xi] += d * s;
                }
                const float a2 = a1 + static_cast<float>(rightI - leftI - 3) * s;
                line[rightI - 1] += d * (1.0f - a2 - aEnd);
            }
            line[rightI] += d * aEnd;
        }
        x = xNext;
    }
}

void OutlineFont::AddQuadratic(float x0, float y0, float cx, float cy, float x1, float y1) const
{
    const float dx = x0 - 2.0f * cx + x1;
    const float dy = y0 - 2.0f * cy + y1;
    const int32_t pieces = CurvePieces(std::sqrt(dx * dx + dy * dy));
    float px = x0;
    float py = y0;
    for (int32_t i = 1; i <= pieces; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(pieces);
        const float u = 1.0f - t;
        const float nx = u * u * x0 + 2.0f * u * t * cx + t * t * x1;
        const float ny = u * u * y0 + 2.0f * u * t * cy + t * t * y1;
        AddLine(px, py, nx, ny);
        px = nx;
        py = ny;
    }
}

void OutlineFont::AddCubic(float x0, float y0, float c1x, float c1y, float c2x, float c2y, float x1, float y1) const
{
    const float ax = x0 - 2.0f * c1x + c2x;
    const float ay = y0 - 2.0f * c1y + c2y;
    const float bx = c1x - 2.0f * c2x + x1;
    const float by = c1y - 2.0f * c2y + y1;
    const float deviation = std::max(std::sqrt(ax * ax + ay * ay), std::sqrt(bx * bx + by * by));
    const int32_t pieces = CurvePieces(deviation * 1.5f);
    float px = x0;
    float py = y0;
    for (int32_t i = 1; i <= pieces; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(pieces);
        const float u = 1.0f - t;
        const float nx = u * u * u * x0 + 3.0f * u * u * t * c1x + 3.0f * u * t * t * c2x + t * t * t * x1;
        const float ny = u * u * u * y0 + 3.0f * u * u * t * c1y + 3.0f * u * t * t * c2y + t * t * t * y1;
        AddLine(px, py, nx, ny);
        px = nx;
        py = ny;
    }
}

bool OutlineFont::Rasterize(uint32_t index, float scaleX, float scaleY, float originX, float originY, uint8_t* image,
                            int32_t width, int32_t height, int32_t stride) const
{
    if (index >= m_Header.glyphCount || !image || width <= 0 || height <= 0)
    {
        return false;
    }
    const size_t cells = static_cast<size_t>(width + 2) * static_cast<size_t>(height);
    if (m_Accumulation.Count() < cells && !m_Accumulation.Allocate(cells, Deki::Memory::External))
    {
        DEKI_LOG_ERROR("OutlineFont: no room to draw a %dx%d glyph", static_cast<int>(width), static_cast<int>(height));
        return false;
    }
    m_AccWidth = width;
    m_AccHeight = height;
    std::fill(m_Accumulation.Data(), m_Accumulation.Data() + cells, 0.0f);

    const OutlineGlyph& glyph = m_Glyphs[index];
    const OutlinePoint* points = m_Points + glyph.firstPoint;
    const auto toX = [&](const OutlinePoint& p) { return originX + static_cast<float>(p.x) * scaleX; };
    const auto toY = [&](const OutlinePoint& p) { return originY - static_cast<float>(p.y) * scaleY; };

    uint32_t start = 0;
    for (uint16_t c = 0; c < glyph.contourCount; ++c)
    {
        const uint32_t end = m_ContourEnds[glyph.firstContour + c];
        const uint32_t count = end - start + 1;
        const auto at = [&](uint32_t i) -> const OutlinePoint& { return points[start + i % count]; };
        const auto isOn = [&](uint32_t i) { return at(i).kind == static_cast<uint8_t>(OutlinePointKind::On); };

        // Start on a point on the outline. A TrueType contour of control
        // points alone starts halfway between its first two, and then every
        // point is walked; otherwise all but the start.
        uint32_t first = 0;
        while (first < count && !isOn(first))
        {
            ++first;
        }
        float startX;
        float startY;
        uint32_t walk;
        if (first == count)
        {
            startX = 0.5f * (toX(at(0)) + toX(at(1)));
            startY = 0.5f * (toY(at(0)) + toY(at(1)));
            first = 0;
            walk = count;
        }
        else
        {
            startX = toX(at(first));
            startY = toY(at(first));
            walk = count - 1;
        }

        float x = startX;
        float y = startY;
        // Control points waiting for the point their curve ends on.
        int32_t pending = 0;
        const OutlinePoint* control[2] = { nullptr, nullptr };
        for (uint32_t step = 1; step <= walk + 1; ++step)
        {
            const bool closing = step == walk + 1;
            const OutlinePoint* p = closing ? nullptr : &at(first + step);
            const auto kind = p ? static_cast<OutlinePointKind>(p->kind) : OutlinePointKind::On;
            const float px = p ? toX(*p) : startX;
            const float py = p ? toY(*p) : startY;
            if (kind == OutlinePointKind::On)
            {
                if (pending == 0)
                {
                    AddLine(x, y, px, py);
                }
                else if (pending == 1)
                {
                    AddQuadratic(x, y, toX(*control[0]), toY(*control[0]), px, py);
                }
                else
                {
                    AddCubic(x, y, toX(*control[0]), toY(*control[0]), toX(*control[1]), toY(*control[1]), px, py);
                }
                pending = 0;
                x = px;
                y = py;
            }
            else if (kind == OutlinePointKind::Quadratic)
            {
                // Two quadratic control points in a row: the outline passes
                // halfway between them.
                if (pending == 1)
                {
                    const float midX = 0.5f * (toX(*control[0]) + px);
                    const float midY = 0.5f * (toY(*control[0]) + py);
                    AddQuadratic(x, y, toX(*control[0]), toY(*control[0]), midX, midY);
                    x = midX;
                    y = midY;
                }
                control[0] = p;
                pending = 1;
            }
            else if (pending < 2)
            {
                control[pending++] = p;
            }
        }
        start = end + 1;
    }

    // Sum along each row: the running total is how much of each pixel is
    // inside, whichever way the outline winds.
    const int32_t accStride = width + 2;
    for (int32_t row = 0; row < height; ++row)
    {
        const float* line = m_Accumulation.Data() + static_cast<size_t>(row) * accStride;
        uint8_t* out = image + static_cast<size_t>(row) * stride;
        float sum = 0.0f;
        for (int32_t col = 0; col < width; ++col)
        {
            sum += line[col];
            const float coverage = std::min(1.0f, std::fabs(sum));
            const uint8_t value = static_cast<uint8_t>(coverage * 255.0f + 0.5f);
            out[col] = std::max(out[col], value);
        }
    }
    return true;
}

}  // namespace Deki2D
