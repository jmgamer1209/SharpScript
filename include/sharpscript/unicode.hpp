#pragma once

#include <cstddef>
#include <string>

namespace sharpscript {

/// UTF-8 <-> UTF-16 conversion helpers. Script strings use UTF-16 code units
/// (like .NET System.String), while source files, diagnostics and console output
/// are UTF-8.
std::u16string utf8ToUtf16(const std::string& text);
std::string utf16ToUtf8(const std::u16string& text);

/// Appends a Unicode code point as one or two UTF-16 code units.
void appendCodePoint(std::u16string& out, char32_t codePoint);

/// Decodes one UTF-8 code point starting at index and advances index.
/// Returns false if the sequence was invalid (a U+FFFD replacement is returned).
bool decodeUtf8(const std::string& text, std::size_t& index, char32_t& codePoint);

}  // namespace sharpscript
