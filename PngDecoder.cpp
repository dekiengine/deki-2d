#include "PngDecoder.h"

#include <deki/providers/Buffer.h>

#include <cstring>

namespace Deki2D
{

namespace Png
{

namespace
{

uint32_t Be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

bool IsType(const uint8_t* type, const char* name)
{
    return std::memcmp(type, name, 4) == 0;
}

constexpr uint8_t kSignature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

// ----------------------------------------------------------------- chunks

struct Chunk
{
    uint32_t length = 0;
    const uint8_t* type = nullptr;
    const uint8_t* data = nullptr;
};

// The chunks after the signature, one at a time. The CRCs are not checked:
// the picture data has the zlib checksum, and a broken colour table gives
// wrong colours, never a read outside the file.
class ChunkReader
{
public:
    ChunkReader(const uint8_t* data, size_t size)
        : m_Data(data),
          m_Size(size)
    {
    }

    bool Next(Chunk& chunk)
    {
        if (m_Size - m_At < 12)
        {
            return false;
        }
        const uint32_t length = Be32(m_Data + m_At);
        if (length > m_Size - m_At - 12)
        {
            return false;
        }
        chunk.length = length;
        chunk.type = m_Data + m_At + 4;
        chunk.data = m_Data + m_At + 8;
        m_At += 12 + static_cast<size_t>(length);
        return true;
    }

private:
    const uint8_t* m_Data;
    size_t m_Size;
    size_t m_At = 8;
};

// The bytes of the IDAT chunks, which together are one zlib stream. They
// come one after another; the stream ends at the first chunk that is not one.
class IdatBytes
{
public:
    IdatBytes(ChunkReader& chunks, const Chunk& first)
        : m_Chunks(chunks),
          m_Chunk(first)
    {
    }

    // The next byte, or -1 at the end.
    int Next()
    {
        while (m_At >= m_Chunk.length)
        {
            if (m_Ended || !m_Chunks.Next(m_Chunk) || !IsType(m_Chunk.type, "IDAT"))
            {
                m_Ended = true;
                return -1;
            }
            m_At = 0;
        }
        return m_Chunk.data[m_At++];
    }

private:
    ChunkReader& m_Chunks;
    Chunk m_Chunk;
    uint32_t m_At = 0;
    bool m_Ended = false;
};

// ------------------------------------------------------------- the header

struct Header
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t depth = 0;
    uint8_t colour = 0;
    uint8_t interlace = 0;
};

uint32_t ChannelsOf(uint8_t colour)
{
    switch (colour)
    {
        case 0: return 1;  // grey
        case 2: return 3;  // RGB
        case 3: return 1;  // palette index
        case 4: return 2;  // grey and alpha
        case 6: return 4;  // RGBA
        default: return 0;
    }
}

bool DepthAllowed(uint8_t colour, uint8_t depth)
{
    switch (colour)
    {
        case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
        case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
        case 2:
        case 4:
        case 6: return depth == 8 || depth == 16;
        default: return false;
    }
}

Result ReadHeader(const uint8_t* data, size_t size, Header& out)
{
    if (!data || size < 8 || std::memcmp(data, kSignature, 8) != 0)
    {
        return Result::NotPng;
    }
    ChunkReader chunks(data, size);
    Chunk chunk;
    if (!chunks.Next(chunk) || !IsType(chunk.type, "IHDR") || chunk.length != 13)
    {
        return Result::Corrupt;
    }
    out.width = Be32(chunk.data);
    out.height = Be32(chunk.data + 4);
    out.depth = chunk.data[8];
    out.colour = chunk.data[9];
    out.interlace = chunk.data[12];
    if (out.width == 0 || out.height == 0 || !DepthAllowed(out.colour, out.depth) || chunk.data[10] != 0 ||
        chunk.data[11] != 0 || out.interlace > 1)
    {
        return Result::Corrupt;
    }
    if (out.width > kMaxSide || out.height > kMaxSide)
    {
        return Result::TooLarge;
    }
    return out.interlace == 0 ? Result::Ok : Result::Unsupported;
}

// ---------------------------------------------------------------- inflate

// A canonical Huffman code, as RFC 1951 builds it from code lengths: how many
// codes there are of each length, and the symbols in code order.
struct Huffman
{
    uint16_t count[16];
    uint16_t symbol[288];
};

// Builds `h` from `n` code lengths. False for a set of lengths that is over
// full, or incomplete when more than one code is used.
bool BuildHuffman(Huffman& h, const uint8_t* lengths, int n)
{
    std::memset(h.count, 0, sizeof(h.count));
    for (int s = 0; s < n; ++s)
    {
        h.count[lengths[s]]++;
    }
    if (h.count[0] == n)
    {
        return true;  // no codes: only fails if one is ever read
    }
    int left = 1;
    for (int len = 1; len < 16; ++len)
    {
        left <<= 1;
        left -= h.count[len];
        if (left < 0)
        {
            return false;
        }
    }
    uint16_t offsets[16];
    offsets[1] = 0;
    for (int len = 1; len < 15; ++len)
    {
        offsets[len + 1] = static_cast<uint16_t>(offsets[len] + h.count[len]);
    }
    for (int s = 0; s < n; ++s)
    {
        if (lengths[s] != 0)
        {
            h.symbol[offsets[lengths[s]]++] = static_cast<uint16_t>(s);
        }
    }
    // Incomplete is allowed only for a single code (RFC 1951, 3.2.7).
    return left == 0 || n - h.count[0] == 1;
}

constexpr uint16_t kLengthBase[29] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
constexpr uint8_t kLengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
constexpr uint16_t kDistanceBase[30] = { 1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                         33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
constexpr uint8_t kDistanceExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

constexpr uint32_t kWindow = 32768;

// The picture's rows as the inflater hands them out: each byte goes into the
// row being filled, and a full row is unfiltered and passed on.
class RowBuilder;

// Inflates a zlib stream from `in`, giving every byte to `rows`.
class Inflater
{
public:
    Inflater(IdatBytes& in, RowBuilder& rows, uint8_t* window)
        : m_In(in),
          m_Rows(rows),
          m_Window(window)
    {
    }

    Result Run();

private:
    // `need` bits, least significant first; sets m_Broken at the end.
    uint32_t Bits(int need)
    {
        uint32_t value = m_BitBuffer;
        while (m_BitCount < need)
        {
            const int byte = m_In.Next();
            if (byte < 0)
            {
                m_Broken = true;
                return 0;
            }
            value |= static_cast<uint32_t>(byte) << m_BitCount;
            m_BitCount += 8;
        }
        m_BitBuffer = need == 32 ? 0 : value >> need;
        m_BitCount -= need;
        return need == 32 ? value : value & ((1u << need) - 1);
    }

    int Decode(const Huffman& h)
    {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len)
        {
            code |= static_cast<int>(Bits(1));
            if (m_Broken)
            {
                return -1;
            }
            const int count = h.count[len];
            if (code - count < first)
            {
                return h.symbol[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;  // a code no table has
    }

    void Put(uint8_t byte);
    bool Stored();
    bool Codes(const Huffman& lengths, const Huffman& distances);
    bool Fixed();
    bool Dynamic();

    IdatBytes& m_In;
    RowBuilder& m_Rows;
    uint8_t* m_Window;
    uint32_t m_WindowAt = 0;
    uint64_t m_Written = 0;
    uint32_t m_BitBuffer = 0;
    int m_BitCount = 0;
    bool m_Broken = false;
    uint32_t m_AdlerA = 1;
    uint32_t m_AdlerB = 0;
    uint32_t m_AdlerRun = 0;  // bytes since the sums were last reduced
};

// ------------------------------------------------------------------- rows

class RowBuilder
{
public:
    RowBuilder(const Header& header, const uint8_t* palette, uint32_t paletteSize, const uint8_t* alphas,
               uint32_t alphaCount, const uint8_t* key, uint32_t outWidth, uint32_t outHeight, const RowSink& sink)
        : m_Header(header),
          m_Palette(palette),
          m_PaletteSize(paletteSize),
          m_Alphas(alphas),
          m_AlphaCount(alphaCount),
          m_Key(key),
          m_OutWidth(outWidth),
          m_OutHeight(outHeight),
          m_Sink(sink)
    {
        const uint32_t bits = ChannelsOf(header.colour) * header.depth;
        m_Stride = (static_cast<uint32_t>(header.width) * bits + 7) / 8;
        m_Bpp = bits >= 8 ? bits / 8 : 1;
        m_Scaled = outWidth != header.width || outHeight != header.height;
    }

    bool Allocate()
    {
        m_Current = Deki::Buffer<uint8_t>(m_Stride, Deki::Memory::External);
        m_Previous = Deki::Buffer<uint8_t>(m_Stride, Deki::Memory::External);
        m_Pixels = Deki::Buffer<uint8_t>(static_cast<size_t>(m_Header.width) * 4, Deki::Memory::External);
        if (!m_Current || !m_Previous || !m_Pixels)
        {
            return false;
        }
        std::memset(m_Previous.Data(), 0, m_Stride);
        if (m_Scaled)
        {
            m_Sums = Deki::Buffer<uint64_t>(static_cast<size_t>(m_OutWidth) * 4, Deki::Memory::External);
            m_ColumnCounts = Deki::Buffer<uint32_t>(m_OutWidth, Deki::Memory::External);
            m_Out = Deki::Buffer<uint8_t>(static_cast<size_t>(m_OutWidth) * 4, Deki::Memory::External);
            if (!m_Sums || !m_ColumnCounts || !m_Out)
            {
                return false;
            }
            std::memset(m_Sums.Data(), 0, m_Sums.Count() * sizeof(uint64_t));
            std::memset(m_ColumnCounts.Data(), 0, m_ColumnCounts.Count() * sizeof(uint32_t));
            for (uint32_t x = 0; x < m_Header.width; ++x)
            {
                m_ColumnCounts.Data()[OutColumn(x)]++;
            }
        }
        return true;
    }

    // One byte of the inflated stream. Bytes after the last row are ignored.
    void Put(uint8_t byte)
    {
        if (m_Row >= m_Header.height || m_Bad)
        {
            return;
        }
        if (m_At == 0)
        {
            m_Filter = byte;
        }
        else
        {
            m_Current.Data()[m_At - 1] = byte;
        }
        if (++m_At == m_Stride + 1)
        {
            m_At = 0;
            FinishRow();
        }
    }

    bool Done() const { return m_Row >= m_Header.height && !m_Bad; }
    bool Bad() const { return m_Bad; }

private:
    uint32_t OutColumn(uint32_t x) const
    {
        return static_cast<uint32_t>(static_cast<uint64_t>(x) * m_OutWidth / m_Header.width);
    }

    uint32_t OutRow(uint32_t y) const
    {
        return static_cast<uint32_t>(static_cast<uint64_t>(y) * m_OutHeight / m_Header.height);
    }

    void FinishRow()
    {
        if (!Unfilter())
        {
            m_Bad = true;
            return;
        }
        if (!ToRgba())
        {
            m_Bad = true;
            return;
        }
        if (!m_Scaled)
        {
            m_Sink(m_Row, m_Pixels.Data(), m_Header.width);
        }
        else
        {
            Accumulate();
        }
        std::swap(m_Current, m_Previous);
        ++m_Row;
        if (m_Scaled && (m_Row == m_Header.height || OutRow(m_Row) != OutRow(m_Row - 1)))
        {
            EmitBand(OutRow(m_Row - 1));
        }
    }

    bool Unfilter()
    {
        uint8_t* cur = m_Current.Data();
        const uint8_t* prev = m_Previous.Data();
        const uint32_t bpp = m_Bpp;
        switch (m_Filter)
        {
            case 0: return true;
            case 1:
                for (uint32_t i = bpp; i < m_Stride; ++i)
                {
                    cur[i] = static_cast<uint8_t>(cur[i] + cur[i - bpp]);
                }
                return true;
            case 2:
                for (uint32_t i = 0; i < m_Stride; ++i)
                {
                    cur[i] = static_cast<uint8_t>(cur[i] + prev[i]);
                }
                return true;
            case 3:
                for (uint32_t i = 0; i < m_Stride; ++i)
                {
                    const uint32_t left = i >= bpp ? cur[i - bpp] : 0;
                    cur[i] = static_cast<uint8_t>(cur[i] + ((left + prev[i]) >> 1));
                }
                return true;
            case 4:
                for (uint32_t i = 0; i < m_Stride; ++i)
                {
                    const int a = i >= bpp ? cur[i - bpp] : 0;
                    const int b = prev[i];
                    const int c = i >= bpp ? prev[i - bpp] : 0;
                    const int p = a + b - c;
                    const int pa = p > a ? p - a : a - p;
                    const int pb = p > b ? p - b : b - p;
                    const int pc = p > c ? p - c : c - p;
                    const int predictor = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    cur[i] = static_cast<uint8_t>(cur[i] + predictor);
                }
                return true;
            default: return false;
        }
    }

    // Sample `x` of a row of samples `depth` bits each (depth 1, 2, 4 or 8).
    static uint32_t Sample(const uint8_t* row, uint32_t x, uint32_t depth)
    {
        if (depth == 8)
        {
            return row[x];
        }
        const uint32_t bit = x * depth;
        const uint32_t shift = 8 - depth - (bit & 7);
        return (row[bit >> 3] >> shift) & ((1u << depth) - 1);
    }

    bool ToRgba()
    {
        const uint8_t* row = m_Current.Data();
        uint8_t* out = m_Pixels.Data();
        const uint32_t depth = m_Header.depth;
        const uint32_t width = m_Header.width;
        switch (m_Header.colour)
        {
            case 0:  // grey
                for (uint32_t x = 0; x < width; ++x)
                {
                    uint32_t value;
                    uint32_t full;
                    if (depth == 16)
                    {
                        full = (static_cast<uint32_t>(row[x * 2]) << 8) | row[x * 2 + 1];
                        value = row[x * 2];
                    }
                    else
                    {
                        full = Sample(row, x, depth);
                        value = full * 255 / ((1u << depth) - 1);
                    }
                    const bool clear = m_Key && full == ((static_cast<uint32_t>(m_Key[0]) << 8) | m_Key[1]);
                    out[x * 4] = out[x * 4 + 1] = out[x * 4 + 2] = static_cast<uint8_t>(value);
                    out[x * 4 + 3] = clear ? 0 : 255;
                }
                return true;
            case 2:  // RGB
                for (uint32_t x = 0; x < width; ++x)
                {
                    bool clear = m_Key != nullptr;
                    for (uint32_t c = 0; c < 3; ++c)
                    {
                        uint32_t full;
                        if (depth == 16)
                        {
                            full = (static_cast<uint32_t>(row[x * 6 + c * 2]) << 8) | row[x * 6 + c * 2 + 1];
                            out[x * 4 + c] = row[x * 6 + c * 2];
                        }
                        else
                        {
                            full = row[x * 3 + c];
                            out[x * 4 + c] = row[x * 3 + c];
                        }
                        clear = clear && full == ((static_cast<uint32_t>(m_Key[c * 2]) << 8) | m_Key[c * 2 + 1]);
                    }
                    out[x * 4 + 3] = clear ? 0 : 255;
                }
                return true;
            case 3:  // palette
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint32_t index = Sample(row, x, depth);
                    if (index >= m_PaletteSize)
                    {
                        return false;
                    }
                    out[x * 4] = m_Palette[index * 3];
                    out[x * 4 + 1] = m_Palette[index * 3 + 1];
                    out[x * 4 + 2] = m_Palette[index * 3 + 2];
                    out[x * 4 + 3] = index < m_AlphaCount ? m_Alphas[index] : 255;
                }
                return true;
            case 4:  // grey and alpha
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint32_t step = depth == 16 ? 4 : 2;
                    const uint8_t grey = row[x * step];
                    const uint8_t alpha = row[x * step + step / 2];
                    out[x * 4] = out[x * 4 + 1] = out[x * 4 + 2] = grey;
                    out[x * 4 + 3] = alpha;
                }
                return true;
            case 6:  // RGBA
                if (depth == 8)
                {
                    std::memcpy(out, row, static_cast<size_t>(width) * 4);
                }
                else
                {
                    for (uint32_t i = 0; i < width * 4; ++i)
                    {
                        out[i] = row[i * 2];
                    }
                }
                return true;
            default: return false;
        }
    }

    // Adds the row to the sums of the output row it falls in, each colour
    // weighted by its alpha so a clear pixel adds no colour.
    void Accumulate()
    {
        const uint8_t* px = m_Pixels.Data();
        uint64_t* sums = m_Sums.Data();
        for (uint32_t x = 0; x < m_Header.width; ++x)
        {
            uint64_t* s = sums + static_cast<size_t>(OutColumn(x)) * 4;
            const uint32_t alpha = px[x * 4 + 3];
            s[0] += static_cast<uint64_t>(px[x * 4]) * alpha;
            s[1] += static_cast<uint64_t>(px[x * 4 + 1]) * alpha;
            s[2] += static_cast<uint64_t>(px[x * 4 + 2]) * alpha;
            s[3] += alpha;
        }
        ++m_BandRows;
    }

    void EmitBand(uint32_t outY)
    {
        uint64_t* sums = m_Sums.Data();
        uint8_t* out = m_Out.Data();
        for (uint32_t x = 0; x < m_OutWidth; ++x)
        {
            uint64_t* s = sums + static_cast<size_t>(x) * 4;
            const uint64_t pixels = static_cast<uint64_t>(m_ColumnCounts.Data()[x]) * m_BandRows;
            const uint64_t alpha = s[3];
            if (alpha == 0 || pixels == 0)
            {
                out[x * 4] = out[x * 4 + 1] = out[x * 4 + 2] = out[x * 4 + 3] = 0;
            }
            else
            {
                out[x * 4] = static_cast<uint8_t>((s[0] + alpha / 2) / alpha);
                out[x * 4 + 1] = static_cast<uint8_t>((s[1] + alpha / 2) / alpha);
                out[x * 4 + 2] = static_cast<uint8_t>((s[2] + alpha / 2) / alpha);
                out[x * 4 + 3] = static_cast<uint8_t>((alpha + pixels / 2) / pixels);
            }
            s[0] = s[1] = s[2] = s[3] = 0;
        }
        m_BandRows = 0;
        m_Sink(outY, out, m_OutWidth);
    }

    const Header& m_Header;
    const uint8_t* m_Palette;
    uint32_t m_PaletteSize;
    const uint8_t* m_Alphas;
    uint32_t m_AlphaCount;
    const uint8_t* m_Key;  // the transparent colour of a grey or RGB picture, 16 bits a channel
    uint32_t m_OutWidth;
    uint32_t m_OutHeight;
    const RowSink& m_Sink;

    uint32_t m_Stride = 0;
    uint32_t m_Bpp = 1;
    bool m_Scaled = false;
    Deki::Buffer<uint8_t> m_Current;
    Deki::Buffer<uint8_t> m_Previous;
    Deki::Buffer<uint8_t> m_Pixels;
    Deki::Buffer<uint64_t> m_Sums;
    Deki::Buffer<uint32_t> m_ColumnCounts;
    Deki::Buffer<uint8_t> m_Out;
    uint32_t m_BandRows = 0;
    uint32_t m_Row = 0;
    uint32_t m_At = 0;
    uint8_t m_Filter = 0;
    bool m_Bad = false;
};

void Inflater::Put(uint8_t byte)
{
    m_Window[m_WindowAt] = byte;
    m_WindowAt = (m_WindowAt + 1) & (kWindow - 1);
    ++m_Written;
    m_AdlerA += byte;
    m_AdlerB += m_AdlerA;
    if (++m_AdlerRun == 5552)
    {
        m_AdlerA %= 65521;
        m_AdlerB %= 65521;
        m_AdlerRun = 0;
    }
    m_Rows.Put(byte);
}

bool Inflater::Stored()
{
    // Stored blocks start on a byte.
    m_BitBuffer = 0;
    m_BitCount = 0;
    const uint32_t length = Bits(16);
    const uint32_t check = Bits(16);
    if (m_Broken || (length ^ 0xFFFF) != check)
    {
        return false;
    }
    for (uint32_t i = 0; i < length; ++i)
    {
        const int byte = m_In.Next();
        if (byte < 0)
        {
            return false;
        }
        Put(static_cast<uint8_t>(byte));
    }
    return true;
}

bool Inflater::Codes(const Huffman& lengths, const Huffman& distances)
{
    for (;;)
    {
        if (m_Rows.Bad())
        {
            return false;
        }
        int symbol = Decode(lengths);
        if (symbol < 0)
        {
            return false;
        }
        if (symbol < 256)
        {
            Put(static_cast<uint8_t>(symbol));
            continue;
        }
        if (symbol == 256)
        {
            return true;
        }
        symbol -= 257;
        if (symbol >= 29)
        {
            return false;
        }
        const uint32_t length = kLengthBase[symbol] + Bits(kLengthExtra[symbol]);
        const int code = Decode(distances);
        if (code < 0 || code >= 30)
        {
            return false;
        }
        const uint32_t distance = kDistanceBase[code] + Bits(kDistanceExtra[code]);
        if (m_Broken || distance > m_Written || distance > kWindow)
        {
            return false;
        }
        for (uint32_t i = 0; i < length; ++i)
        {
            Put(m_Window[(m_WindowAt - distance) & (kWindow - 1)]);
        }
    }
}

// The codes of a fixed-code block (RFC 1951, 3.2.6), built once.
struct FixedCodes
{
    Huffman lengths;
    Huffman distances;

    FixedCodes()
    {
        uint8_t l[288];
        int s = 0;
        for (; s < 144; ++s)
        {
            l[s] = 8;
        }
        for (; s < 256; ++s)
        {
            l[s] = 9;
        }
        for (; s < 280; ++s)
        {
            l[s] = 7;
        }
        for (; s < 288; ++s)
        {
            l[s] = 8;
        }
        BuildHuffman(lengths, l, 288);
        for (s = 0; s < 30; ++s)
        {
            l[s] = 5;
        }
        BuildHuffman(distances, l, 30);
    }
};

bool Inflater::Fixed()
{
    static const FixedCodes codes;
    return Codes(codes.lengths, codes.distances);
}

bool Inflater::Dynamic()
{
    static constexpr uint8_t kOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    const uint32_t lengthCount = Bits(5) + 257;
    const uint32_t distanceCount = Bits(5) + 1;
    const uint32_t codeCount = Bits(4) + 4;
    if (m_Broken || lengthCount > 286 || distanceCount > 30)
    {
        return false;
    }
    uint8_t lengths[320];
    std::memset(lengths, 0, sizeof(lengths));
    for (uint32_t i = 0; i < codeCount; ++i)
    {
        lengths[kOrder[i]] = static_cast<uint8_t>(Bits(3));
    }
    Huffman codeLengths;
    if (m_Broken || !BuildHuffman(codeLengths, lengths, 19))
    {
        return false;
    }
    uint32_t at = 0;
    while (at < lengthCount + distanceCount)
    {
        int symbol = Decode(codeLengths);
        if (symbol < 0)
        {
            return false;
        }
        if (symbol < 16)
        {
            lengths[at++] = static_cast<uint8_t>(symbol);
            continue;
        }
        uint8_t value = 0;
        uint32_t repeat;
        if (symbol == 16)
        {
            if (at == 0)
            {
                return false;
            }
            value = lengths[at - 1];
            repeat = 3 + Bits(2);
        }
        else if (symbol == 17)
        {
            repeat = 3 + Bits(3);
        }
        else
        {
            repeat = 11 + Bits(7);
        }
        if (m_Broken || at + repeat > lengthCount + distanceCount)
        {
            return false;
        }
        while (repeat--)
        {
            lengths[at++] = value;
        }
    }
    if (lengths[256] == 0)
    {
        return false;  // no end-of-block code
    }
    Huffman literals;
    Huffman distances;
    if (!BuildHuffman(literals, lengths, static_cast<int>(lengthCount)) ||
        !BuildHuffman(distances, lengths + lengthCount, static_cast<int>(distanceCount)))
    {
        return false;
    }
    return Codes(literals, distances);
}

Result Inflater::Run()
{
    const uint32_t cmf = Bits(8);
    const uint32_t flags = Bits(8);
    if (m_Broken || (cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flags) % 31 != 0 || (flags & 0x20) != 0)
    {
        return Result::Corrupt;
    }
    bool last = false;
    while (!last)
    {
        last = Bits(1) != 0;
        const uint32_t type = Bits(2);
        bool ok;
        switch (type)
        {
            case 0: ok = Stored(); break;
            case 1: ok = Fixed(); break;
            case 2: ok = Dynamic(); break;
            default: ok = false; break;
        }
        if (!ok || m_Broken)
        {
            return Result::Corrupt;
        }
    }
    // The Adler-32 of everything inflated, on the next byte boundary.
    m_BitBuffer = 0;
    m_BitCount = 0;
    uint32_t adler = 0;
    for (int i = 0; i < 4; ++i)
    {
        const int byte = m_In.Next();
        if (byte < 0)
        {
            return Result::Corrupt;
        }
        adler = (adler << 8) | static_cast<uint32_t>(byte);
    }
    return adler == (((m_AdlerB % 65521) << 16) | (m_AdlerA % 65521)) ? Result::Ok : Result::Corrupt;
}

}  // namespace

Result ReadInfo(const uint8_t* data, size_t size, Info& out)
{
    Header header;
    const Result result = ReadHeader(data, size, header);
    if (result != Result::Ok && result != Result::Unsupported)
    {
        return result;
    }
    out.width = header.width;
    out.height = header.height;
    out.hasAlpha = header.colour == 4 || header.colour == 6;
    // A transparent colour says so in tRNS, before the picture data.
    ChunkReader chunks(data, size);
    Chunk chunk;
    while (chunks.Next(chunk) && !IsType(chunk.type, "IDAT"))
    {
        if (IsType(chunk.type, "tRNS"))
        {
            out.hasAlpha = true;
        }
    }
    return result;
}

void FitSize(uint32_t width, uint32_t height, uint32_t maxWidth, uint32_t maxHeight, uint32_t& outWidth,
             uint32_t& outHeight)
{
    outWidth = width;
    outHeight = height;
    const bool tooWide = maxWidth != 0 && width > maxWidth;
    const bool tooTall = maxHeight != 0 && height > maxHeight;
    if (!tooWide && !tooTall)
    {
        return;
    }
    // The tighter of the two limits decides, as a fraction: width/height
    // times maxWidth/width or maxHeight/height, rounded, at least 1.
    const bool byWidth =
        tooWide && (!tooTall || static_cast<uint64_t>(maxWidth) * height <= static_cast<uint64_t>(maxHeight) * width);
    if (byWidth)
    {
        outWidth = maxWidth;
        outHeight = static_cast<uint32_t>((static_cast<uint64_t>(height) * maxWidth + width / 2) / width);
    }
    else
    {
        outHeight = maxHeight;
        outWidth = static_cast<uint32_t>((static_cast<uint64_t>(width) * maxHeight + height / 2) / height);
    }
    outWidth = outWidth == 0 ? 1 : outWidth;
    outHeight = outHeight == 0 ? 1 : outHeight;
}

Result Decode(const uint8_t* data, size_t size, uint32_t maxWidth, uint32_t maxHeight, const RowSink& row,
              uint32_t& outWidth, uint32_t& outHeight)
{
    Header header;
    const Result read = ReadHeader(data, size, header);
    if (read != Result::Ok)
    {
        return read;
    }

    // The colour table and the transparency, both before the picture data.
    ChunkReader chunks(data, size);
    Chunk chunk;
    chunks.Next(chunk);  // IHDR
    const uint8_t* palette = nullptr;
    uint32_t paletteSize = 0;
    const uint8_t* alphas = nullptr;
    uint32_t alphaCount = 0;
    uint8_t key[6] = {};
    bool hasKey = false;
    bool found = false;
    while (chunks.Next(chunk))
    {
        if (IsType(chunk.type, "IDAT"))
        {
            found = true;
            break;
        }
        if (IsType(chunk.type, "IEND"))
        {
            break;
        }
        if (IsType(chunk.type, "PLTE"))
        {
            if (chunk.length % 3 != 0 || chunk.length == 0 || chunk.length > 768)
            {
                return Result::Corrupt;
            }
            palette = chunk.data;
            paletteSize = chunk.length / 3;
        }
        else if (IsType(chunk.type, "tRNS"))
        {
            if (header.colour == 3)
            {
                alphas = chunk.data;
                alphaCount = chunk.length;
            }
            else if (header.colour == 0 && chunk.length == 2)
            {
                std::memcpy(key, chunk.data, 2);
                hasKey = true;
            }
            else if (header.colour == 2 && chunk.length == 6)
            {
                std::memcpy(key, chunk.data, 6);
                hasKey = true;
            }
        }
    }
    if (!found || (header.colour == 3 && !palette))
    {
        return Result::Corrupt;
    }
    if (alphaCount > paletteSize)
    {
        alphaCount = paletteSize;
    }

    FitSize(header.width, header.height, maxWidth, maxHeight, outWidth, outHeight);
    RowBuilder rows(header, palette, paletteSize, alphas, alphaCount, hasKey ? key : nullptr, outWidth, outHeight, row);
    Deki::Buffer<uint8_t> window(kWindow, Deki::Memory::External);
    if (!window || !rows.Allocate())
    {
        return Result::NoMemory;
    }
    IdatBytes bytes(chunks, chunk);
    Inflater inflater(bytes, rows, window.Data());
    const Result result = inflater.Run();
    if (result != Result::Ok)
    {
        return result;
    }
    return rows.Done() ? Result::Ok : Result::Corrupt;
}

const char* ResultName(Result result)
{
    switch (result)
    {
        case Result::Ok: return "ok";
        case Result::NotPng: return "not a PNG";
        case Result::Unsupported: return "an interlaced PNG, which is not read";
        case Result::Corrupt: return "a broken PNG";
        case Result::TooLarge: return "a picture larger than 8192 pixels a side";
        case Result::NoMemory: return "not enough memory";
    }
    return "?";
}

}  // namespace Png

}  // namespace Deki2D
