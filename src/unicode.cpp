#include "sharpscript/unicode.hpp"

#include <cstdint>

namespace sharpscript {
namespace {

void appendUtf8(std::string& out, char32_t codePoint) {
    if (codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

}  // namespace

void appendCodePoint(std::u16string& out, char32_t codePoint) {
    if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
        out.push_back(static_cast<char16_t>(0xFFFD));
        return;
    }
    if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char16_t>(codePoint));
        return;
    }
    codePoint -= 0x10000;
    out.push_back(static_cast<char16_t>(0xD800 + (codePoint >> 10)));
    out.push_back(static_cast<char16_t>(0xDC00 + (codePoint & 0x3FF)));
}

bool decodeUtf8(const std::string& text, std::size_t& index, char32_t& codePoint) {
    if (index >= text.size()) return false;

    auto byte = [&](std::size_t offset) -> std::uint32_t {
        return static_cast<unsigned char>(text[index + offset]);
    };

    std::uint32_t first = byte(0);
    if (first < 0x80) {
        codePoint = first;
        ++index;
        return true;
    }

    int length = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0) == 0xC0) {
        length = 2;
        value = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        length = 3;
        value = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        length = 4;
        value = first & 0x07;
    } else {
        ++index;
        codePoint = 0xFFFD;
        return false;
    }

    if (index + static_cast<std::size_t>(length) > text.size()) {
        ++index;
        codePoint = 0xFFFD;
        return false;
    }

    for (int i = 1; i < length; ++i) {
        std::uint32_t next = byte(static_cast<std::size_t>(i));
        if ((next & 0xC0) != 0x80) {
            ++index;
            codePoint = 0xFFFD;
            return false;
        }
        value = (value << 6) | (next & 0x3F);
    }

    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        index += static_cast<std::size_t>(length);
        codePoint = 0xFFFD;
        return false;
    }

    index += static_cast<std::size_t>(length);
    codePoint = value;
    return true;
}

std::u16string utf8ToUtf16(const std::string& text) {
    std::u16string result;
    result.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        char32_t codePoint = 0;
        decodeUtf8(text, index, codePoint);
        appendCodePoint(result, codePoint);
    }
    return result;
}

std::string utf16ToUtf8(const std::u16string& text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        char32_t codePoint = text[i];
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 1 < text.size()) {
            char32_t low = text[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            } else {
                codePoint = 0xFFFD;
            }
        } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
            codePoint = 0xFFFD;
        }
        appendUtf8(result, codePoint);
    }
    return result;
}

}  // namespace sharpscript
