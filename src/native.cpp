#include <cctype>

#include "sharpscript/engine.hpp"
#include "sharpscript/unicode.hpp"

namespace sharpscript {
namespace {

const std::u16string& selfString(const Value& self) {
    if (self.isNull()) throw ScriptNullReferenceException("string instance is null");
    return self.asString();
}

const std::u16string& argumentString(const Value& argument, const char* name) {
    if (argument.isNull()) {
        throw std::runtime_error(std::string("ArgumentNullException: ") + name);
    }
    return argument.asString();
}

bool isCSharpWhiteSpace(char16_t c) {
    return (c >= 0x0009 && c <= 0x000D) || c == 0x0020 || c == 0x0085 || c == 0x00A0 ||
           c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}

char16_t upperChar(char16_t c) {
    if (c >= u'a' && c <= u'z') return static_cast<char16_t>(c - 32);
    if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7) return static_cast<char16_t>(c - 32);
    if (c == 0x00FF) return 0x0178;
    return c;
}

char16_t lowerChar(char16_t c) {
    if (c >= u'A' && c <= u'Z') return static_cast<char16_t>(c + 32);
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return static_cast<char16_t>(c + 32);
    if (c == 0x0178) return 0x00FF;
    return c;
}

std::u16string toUpperText(const std::u16string& text) {
    std::u16string result;
    result.reserve(text.size());
    for (char16_t c : text) result.push_back(upperChar(c));
    return result;
}

std::u16string toLowerText(const std::u16string& text) {
    std::u16string result;
    result.reserve(text.size());
    for (char16_t c : text) result.push_back(lowerChar(c));
    return result;
}

std::u16string trimText(const std::u16string& text) {
    std::size_t first = 0;
    while (first < text.size() && isCSharpWhiteSpace(text[first])) ++first;
    std::size_t last = text.size();
    while (last > first && isCSharpWhiteSpace(text[last - 1])) --last;
    return text.substr(first, last - first);
}

std::u16string replaceAll(const std::u16string& text, const std::u16string& from,
                          const std::u16string& to) {
    if (from.empty()) {
        throw std::runtime_error("ArgumentException: String cannot be of zero length.");
    }
    std::u16string result;
    std::size_t position = 0;
    while (position <= text.size()) {
        auto found = text.find(from, position);
        if (found == std::u16string::npos) {
            result.append(text, position, text.size() - position);
            break;
        }
        result.append(text, position, found - position);
        result.append(to);
        position = found + from.size();
    }
    return result;
}

ScriptType stringArrayType(ScriptEngine& engine) { return engine.getArrayType(primString()); }

Value makeStringArray(ScriptEngine& engine, std::vector<std::u16string> parts) {
    auto elementType = std::make_shared<ScriptType>(primString());
    auto array = engine.garbageCollector().make<ScriptArray>(elementType, parts.size());
    for (auto& part : parts) array->items().push_back(Value::string(std::move(part)));
    return Value::array(array);
}

}  // namespace

void ScriptEngine::initializeStringMethods() {
    auto& type = createNativeType("string", false);
    stringMethods_ = &type;

    const ScriptType intType = primI32();
    const ScriptType boolType = primBool();
    const ScriptType charType = primChar();
    const ScriptType stringType = primString();
    const ScriptType stringArray = stringArrayType(*this);

    auto add = [&type](const std::string& name, const ScriptType& returnType,
                       std::vector<ScriptType> parameterTypes, NativeCall call) {
        type.addMethod(name, returnType, std::move(parameterTypes), std::move(call), false);
    };

    // ---------------------------------------------------------- Substring
    add("substring", stringType, {intType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            std::int32_t start = args[0].asI32();
            if (start < 0 || static_cast<std::size_t>(start) > text.size()) {
                throw std::runtime_error("ArgumentOutOfRangeException: startIndex");
            }
            return Value::string(text.substr(static_cast<std::size_t>(start)));
        });

    add("substring", stringType, {intType, intType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            std::int32_t start = args[0].asI32();
            std::int32_t length = args[1].asI32();
            if (start < 0 || length < 0 || static_cast<std::size_t>(start) > text.size() ||
                static_cast<std::size_t>(start) + static_cast<std::size_t>(length) > text.size()) {
                throw std::runtime_error("ArgumentOutOfRangeException: startIndex/length");
            }
            return Value::string(text.substr(static_cast<std::size_t>(start),
                                             static_cast<std::size_t>(length)));
        });

    // ---------------------------------------------------------- IndexOf
    add("indexOf", intType, {stringType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& needle = argumentString(args[0], "value");
            auto position = text.find(needle);
            return Value::i32(position == std::u16string::npos ? -1
                                                               : static_cast<std::int32_t>(position));
        });

    add("indexOf", intType, {stringType, intType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& needle = argumentString(args[0], "value");
            std::int32_t start = args[1].asI32();
            if (start < 0 || static_cast<std::size_t>(start) > text.size()) {
                throw std::runtime_error("ArgumentOutOfRangeException: startIndex");
            }
            auto position = text.find(needle, static_cast<std::size_t>(start));
            return Value::i32(position == std::u16string::npos ? -1
                                                               : static_cast<std::int32_t>(position));
        });

    add("indexOf", intType, {charType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            auto position = text.find(args[0].asChar());
            return Value::i32(position == std::u16string::npos ? -1
                                                               : static_cast<std::int32_t>(position));
        });

    add("lastIndexOf", intType, {stringType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& needle = argumentString(args[0], "value");
            auto position = text.rfind(needle);
            return Value::i32(position == std::u16string::npos ? -1
                                                               : static_cast<std::int32_t>(position));
        });

    add("lastIndexOf", intType, {charType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            auto position = text.rfind(args[0].asChar());
            return Value::i32(position == std::u16string::npos ? -1
                                                               : static_cast<std::int32_t>(position));
        });

    // ---------------------------------------------------------- Contains
    add("contains", boolType, {stringType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& needle = argumentString(args[0], "value");
            return Value::boolean(text.find(needle) != std::u16string::npos);
        });

    add("contains", boolType, {charType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            return Value::boolean(text.find(args[0].asChar()) != std::u16string::npos);
        });

    // ---------------------------------------------------------- StartsWith / EndsWith
    add("startsWith", boolType, {stringType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& prefix = argumentString(args[0], "value");
            return Value::boolean(prefix.size() <= text.size() &&
                                  text.compare(0, prefix.size(), prefix) == 0);
        });

    add("startsWith", boolType, {charType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            return Value::boolean(!text.empty() && text[0] == args[0].asChar());
        });

    add("endsWith", boolType, {stringType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& suffix = argumentString(args[0], "value");
            return Value::boolean(suffix.size() <= text.size() &&
                                  text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
        });

    add("endsWith", boolType, {charType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            return Value::boolean(!text.empty() && text.back() == args[0].asChar());
        });

    // ---------------------------------------------------------- Case / Trim
    add("toUpper", stringType, {},
        [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            return Value::string(toUpperText(selfString(self)));
        });

    add("toLower", stringType, {},
        [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            return Value::string(toLowerText(selfString(self)));
        });

    add("trim", stringType, {},
        [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            return Value::string(trimText(selfString(self)));
        });

    // ---------------------------------------------------------- Replace
    add("replace", stringType, {stringType, stringType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& from = argumentString(args[0], "oldValue");
            const auto& to = argumentString(args[1], "newValue");
            return Value::string(replaceAll(text, from, to));
        });

    add("replace", stringType, {charType, charType},
        [](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            std::u16string result = selfString(self);
            char16_t from = args[0].asChar();
            char16_t to = args[1].asChar();
            for (char16_t& c : result) {
                if (c == from) c = to;
            }
            return Value::string(std::move(result));
        });

    // ---------------------------------------------------------- Misc
    add("isEmpty", boolType, {},
        [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            return Value::boolean(selfString(self).empty());
        });

    add("toString", stringType, {},
        [](ExecutionContext&, const Value& self, const std::vector<Value>&) {
            return Value::string(selfString(self));
        });

    // ---------------------------------------------------------- Split
    add("split", stringArray, {stringType},
        [this](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            const auto& separator = argumentString(args[0], "separator");
            std::vector<std::u16string> parts;
            if (separator.empty()) {
                parts.push_back(text);
            } else {
                std::size_t start = 0;
                while (true) {
                    auto position = text.find(separator, start);
                    if (position == std::u16string::npos) {
                        parts.push_back(text.substr(start));
                        break;
                    }
                    parts.push_back(text.substr(start, position - start));
                    start = position + separator.size();
                }
            }
            return makeStringArray(*this, std::move(parts));
        });

    add("split", stringArray, {charType},
        [this](ExecutionContext&, const Value& self, const std::vector<Value>& args) {
            const auto& text = selfString(self);
            char16_t separator = args[0].asChar();
            std::vector<std::u16string> parts;
            std::size_t start = 0;
            while (true) {
                auto position = text.find(separator, start);
                if (position == std::u16string::npos) {
                    parts.push_back(text.substr(start));
                    break;
                }
                parts.push_back(text.substr(start, position - start));
                start = position + 1;
            }
            return makeStringArray(*this, std::move(parts));
        });
}

}  // namespace sharpscript
