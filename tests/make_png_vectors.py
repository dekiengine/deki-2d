"""Writes PngVectors.h: PNG files of every colour type and bit depth, with
each kind of deflate block and each row filter, and the RGBA pixels each one
must read as. The pixels are made here and encoded here (Python's zlib does
the deflate), so the reader is checked against an encoder it shares nothing
with.

    python make_png_vectors.py > PngVectors.h
"""
import struct
import zlib

W, H = 17, 11


def chunk(kind, data):
    body = kind + data
    return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filtered(rows, bpp):
    """Each row with a filter byte in front, the filter going round 0..4."""
    out = bytearray()
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        kind = y % 5
        out.append(kind)
        for i, value in enumerate(row):
            left = row[i - bpp] if i >= bpp else 0
            up = prev[i]
            upleft = prev[i - bpp] if i >= bpp else 0
            predict = [0, left, up, (left + up) // 2, paeth(left, up, upleft)][kind]
            out.append((value - predict) & 0xFF)
        prev = row
    return bytes(out)


def compress(raw, how):
    if how == 'stored':
        return zlib.compress(raw, 0)
    if how == 'fixed':
        c = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
        return c.compress(raw) + c.flush()
    return zlib.compress(raw, 9)  # dynamic codes


def png(colour, depth, rows, bpp, how='dynamic', extra=b'', palette=None, split=0):
    header = struct.pack('>IIBBBBB', W, H, depth, colour, 0, 0, 0)
    data = compress(filtered(rows, bpp), how)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
    if palette is not None:
        out += chunk(b'PLTE', palette)
    out += extra
    if split:
        for i in range(0, len(data), split):
            out += chunk(b'IDAT', data[i:i + split])
    else:
        out += chunk(b'IDAT', data)
    return out + chunk(b'IEND', b'')


def pack_bits(samples, depth):
    out = bytearray()
    acc = 0
    n = 0
    for s in samples:
        acc = (acc << depth) | s
        n += depth
        if n == 8:
            out.append(acc)
            acc, n = 0, 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


def value(x, y, c, top):
    return (x * 37 + y * 101 + c * 59 + x * y * 7) % (top + 1)


cases = []


def add(name, data, rgba):
    cases.append((name, data, rgba))


# RGBA 8 and 16, three kinds of block.
for how in ('dynamic', 'fixed', 'stored'):
    rows, rgba = [], bytearray()
    for y in range(H):
        row = bytearray()
        for x in range(W):
            px = [value(x, y, c, 255) for c in range(4)]
            row += bytes(px)
            rgba += bytes(px)
        rows.append(bytes(row))
    add(f'rgba8_{how}', png(6, 8, rows, 4, how), rgba)

rows, rgba = [], bytearray()
for y in range(H):
    row = bytearray()
    for x in range(W):
        px = [value(x, y, c, 65535) for c in range(4)]
        row += b''.join(struct.pack('>H', v) for v in px)
        rgba += bytes(v >> 8 for v in px)
    rows.append(bytes(row))
add('rgba16', png(6, 16, rows, 8), rgba)

# RGB 8 and 16, and RGB 8 with a transparent colour, in IDAT chunks of 7 bytes.
for depth in (8, 16):
    rows, rgba = [], bytearray()
    for y in range(H):
        row = bytearray()
        for x in range(W):
            px = [value(x, y, c, 255 if depth == 8 else 65535) for c in range(3)]
            row += bytes(px) if depth == 8 else b''.join(struct.pack('>H', v) for v in px)
            rgba += bytes(px if depth == 8 else [v >> 8 for v in px]) + b'\xff'
        rows.append(bytes(row))
    add(f'rgb{depth}', png(2, depth, rows, 3 * depth // 8), rgba)

key = (value(3, 2, 0, 255), value(3, 2, 1, 255), value(3, 2, 2, 255))
rows, rgba = [], bytearray()
for y in range(H):
    row = bytearray()
    for x in range(W):
        px = [value(x, y, c, 255) for c in range(3)]
        row += bytes(px)
        rgba += bytes(px) + (b'\x00' if tuple(px) == key else b'\xff')
    rows.append(bytes(row))
add('rgb8_key_split', png(2, 8, rows, 3, extra=chunk(b'tRNS', struct.pack('>HHH', *key)), split=7), rgba)

# Grey at every depth, with a transparent grey at 8 bits.
for depth in (1, 2, 4, 8, 16):
    top = (1 << depth) - 1
    rows, rgba = [], bytearray()
    for y in range(H):
        samples = [value(x, y, 0, top) for x in range(W)]
        rows.append(pack_bits(samples, depth) if depth < 8 else
                    (bytes(samples) if depth == 8 else b''.join(struct.pack('>H', s) for s in samples)))
        for s in samples:
            g = s >> 8 if depth == 16 else s * 255 // top
            rgba += bytes([g, g, g, 255])
    add(f'grey{depth}', png(0, depth, rows, 2 if depth == 16 else 1), rgba)

grey_key = value(5, 5, 0, 255)
rows, rgba = [], bytearray()
for y in range(H):
    samples = [value(x, y, 0, 255) for x in range(W)]
    rows.append(bytes(samples))
    for s in samples:
        rgba += bytes([s, s, s, 0 if s == grey_key else 255])
add('grey8_key', png(0, 8, rows, 1, extra=chunk(b'tRNS', struct.pack('>H', grey_key))), rgba)

# Grey and alpha, 8 and 16.
for depth in (8, 16):
    top = (1 << depth) - 1
    rows, rgba = [], bytearray()
    for y in range(H):
        row = bytearray()
        for x in range(W):
            g, a = value(x, y, 0, top), value(x, y, 3, top)
            row += bytes([g, a]) if depth == 8 else struct.pack('>HH', g, a)
            g8, a8 = (g, a) if depth == 8 else (g >> 8, a >> 8)
            rgba += bytes([g8, g8, g8, a8])
        rows.append(bytes(row))
    add(f'greyalpha{depth}', png(4, depth, rows, 2 * depth // 8), rgba)

# Palette at every depth, some entries see-through.
for depth in (1, 2, 4, 8):
    count = min(1 << depth, 200)
    palette = bytes((i * 53 + c * 91) % 256 for i in range(count) for c in range(3))
    alphas = bytes((i * 71) % 256 for i in range(count // 2))
    rows, rgba = [], bytearray()
    for y in range(H):
        samples = [value(x, y, 0, count - 1) for x in range(W)]
        rows.append(pack_bits(samples, depth) if depth < 8 else bytes(samples))
        for s in samples:
            rgba += palette[s * 3:s * 3 + 3] + bytes([alphas[s] if s < len(alphas) else 255])
    add(f'palette{depth}', png(3, depth, rows, 1, palette=palette, extra=chunk(b'tRNS', alphas)), rgba)


def c_bytes(data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in data[i:i + 24]) + ',')
    return '\n'.join(lines)


print('// Made by make_png_vectors.py: PNG files and the RGBA pixels each must read as.')
print('#pragma once')
print('#include <cstddef>')
print('#include <cstdint>')
print('')
print('namespace PngVectors')
print('{')
print(f'constexpr uint32_t kWidth = {W};')
print(f'constexpr uint32_t kHeight = {H};')
for name, data, rgba in cases:
    assert len(rgba) == W * H * 4, name
    print(f'constexpr uint8_t {name}_png[] = {{\n{c_bytes(data)}\n}};')
    print(f'constexpr uint8_t {name}_rgba[] = {{\n{c_bytes(rgba)}\n}};')
print('struct Case')
print('{')
print('    const char* name;')
print('    const uint8_t* png;')
print('    size_t size;')
print('    const uint8_t* rgba;')
print('};')
print('constexpr Case kCases[] = {')
for name, _, _ in cases:
    print(f'    {{ "{name}", {name}_png, sizeof({name}_png), {name}_rgba }},')
print('};')
print('}  // namespace PngVectors')
