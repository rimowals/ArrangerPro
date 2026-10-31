#include "Oc31.h"

#include "arranger/Style.h"

namespace arr::detail {

// Block layout:
//   bytes 0..3   "OC31" (or "OC32")
//   bytes 4..7   uncompressed size, little endian
//   bytes 8..n-2 compressed stream
//   byte  n-1    XOR check byte over the uncompressed data (not verified here)

bool looksLikeOc31(std::span<const uint8_t> p)
{
    return p.size() >= 9 && p[0] == 'O' && p[1] == 'C' && p[2] == '3' && (p[3] == '1' || p[3] == '2');
}

std::vector<uint8_t> decompressOc31(std::span<const uint8_t> payload)
{
    if (!looksLikeOc31(payload))
        throw StyleError("OC31 header missing");

    const size_t size = size_t(payload[4]) | (size_t(payload[5]) << 8) | (size_t(payload[6]) << 16) | (size_t(payload[7]) << 24);
    const auto src = payload.subspan(8, payload.size() - 9);
    std::vector<uint8_t> dst(size);
    size_t s = 0, d = 0;

    auto in = [&](size_t i) -> uint8_t {
        if (i >= src.size())
            throw StyleError("OC31: compressed stream truncated");
        return src[i];
    };
    auto be16 = [&](size_t i) { return (int(in(i)) << 8) | in(i + 1); };

    auto literal = [&](size_t len) {
        if (s + len > src.size() || d + len > dst.size())
            throw StyleError("OC31: literal run out of range");
        std::copy(src.begin() + s, src.begin() + s + len, dst.begin() + d);
        s += len;
        d += len;
    };

    // A back-reference, followed by 0..3 literal bytes encoded in the low two bits
    // of the last compressed byte consumed.
    auto backRef = [&](size_t offset, size_t length) {
        if (offset + 1 > d || d + length > dst.size())
            throw StyleError("OC31: back-reference out of range");
        size_t from = d - offset - 1;
        while (length-- > 0)
            dst[d++] = dst[from++];
        literal(in(s - 1) & 3);
    };

    while (s < src.size())
    {
        const int flag = in(s++);

        if (flag >= 0x40)
        {
            int tmp = (flag << 8) | in(s++);
            backRef((tmp >> 2) & 0x7FF, size_t(tmp >> 13) + 1);
        }
        else if (flag >= 0x10)
        {
            // 0x20..0x3F: near references; 0x10..0x1F: far references (+16K or +32K).
            const bool far = flag < 0x20;
            const size_t base = far ? 0x4000 + ((flag & 8) ? 0x4000 : 0) : 0;
            const int tmp = far ? (flag & 7) : (flag & 0x1F);
            size_t length;
            if (tmp == 0)
                length = size_t(in(s++)) + (far ? 0xA : 0x22);
            else if (tmp == 1)
            {
                length = size_t(be16(s));
                s += 2;
            }
            else
                length = size_t(tmp) + 2;
            size_t offset = size_t(be16(s) >> 2) + base;
            s += 2;
            backRef(offset, length);
        }
        else if (flag >= 2)
            literal(size_t(flag) + 2);
        else if (flag == 0)
            literal(size_t(in(s++)) + 0x12);
        else // flag == 1
        {
            size_t len = size_t(be16(s));
            s += 2;
            if (len == 0 && d == size)
                break;
            literal(len);
        }
    }

    if (d != size)
        throw StyleError("OC31: decompressed size mismatch");
    return dst;
}

} // namespace arr::detail
