#pragma once

#include "arranger/Style.h"

#include <cstdint>
#include <span>
#include <string>

namespace arr::detail {

// Bounds-checked big-endian reader over a byte span; overruns throw StyleError.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}

    size_t position() const { return pos_; }
    void seek(size_t pos) { require(pos, 0); pos_ = pos; }
    size_t remaining() const { return data_.size() - pos_; }
    bool atEnd() const { return pos_ >= data_.size(); }
    std::span<const uint8_t> data() const { return data_; }

    uint8_t u8() { require(pos_, 1); return data_[pos_++]; }
    uint8_t peek() const { require(pos_, 1); return data_[pos_]; }

    uint16_t u16()
    {
        uint16_t hi = u8();
        return uint16_t((hi << 8) | u8());
    }

    uint32_t u32()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v = (v << 8) | u8();
        return v;
    }

    // MIDI variable-length quantity.
    uint32_t varLen()
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80))
                return v;
        }
        throw StyleError("invalid variable-length quantity");
    }

    std::span<const uint8_t> bytes(size_t n)
    {
        require(pos_, n);
        auto s = data_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

    std::string ascii(size_t n)
    {
        auto b = bytes(n);
        return std::string(b.begin(), b.end());
    }

    void skip(size_t n) { bytes(n); }

private:
    void require(size_t at, size_t n) const
    {
        if (at > data_.size() || n > data_.size() - at)
            throw StyleError("unexpected end of data");
    }

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

} // namespace arr::detail
