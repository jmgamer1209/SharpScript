#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sharpscript {

/// Parameter passing mode (`ref` / `out` / `in`).
enum class ParamModifier { None, Ref, Out, In };

/// Variance of a generic type parameter (`delegate T Factory<out T>()`).
enum class Variance { None, In, Out };

class ScriptType;
class ScriptObjectType;
class ScriptClassType;
class NativeObjectType;
class NativeFunction;
class ScriptFunction;
class ScriptModule;
class ScriptEngine;
class ExecutionContext;
class ScriptInstance;
class ScriptArray;
class ScriptStructValue;

enum class DiagnosticSeverity { Info, Warning, Error };

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    std::string file;
    int line = 1;
    int column = 1;

    std::string toString() const {
        std::ostringstream out;
        out << (file.empty() ? "<script>" : file) << "(" << line << "," << column << "): "
            << (severity == DiagnosticSeverity::Error ? "error"
                : severity == DiagnosticSeverity::Warning ? "warning" : "info")
            << " " << code << ": " << message;
        return out.str();
    }
};

/// Where one source section starts inside the concatenated module text.
/// Reported line = (line - startLine + 1) + lineOffset.
struct SectionRange {
    int startLine = 1;
    std::string file;
    int lineOffset = 0;
};

using SectionMap = std::vector<SectionRange>;

class DiagnosticBag {
public:
    explicit DiagnosticBag(std::string file = {}) : file_(std::move(file)) {}

    /// Switches reporting to multi-section mode: lines are translated back to
    /// the section that produced them (name + lineOffset).
    void setSections(std::shared_ptr<const SectionMap> sections) { sections_ = std::move(sections); }

    void error(const std::string& code, const std::string& message, int line, int column) {
        items_.push_back({DiagnosticSeverity::Error, code, message, fileFor(line), lineIn(line), column});
    }
    void warning(const std::string& code, const std::string& message, int line, int column) {
        items_.push_back({DiagnosticSeverity::Warning, code, message, fileFor(line), lineIn(line), column});
    }
    void add(const Diagnostic& diagnostic) { items_.push_back(diagnostic); }
    void addRange(const std::vector<Diagnostic>& diagnostics) {
        items_.insert(items_.end(), diagnostics.begin(), diagnostics.end());
    }

    bool hasErrors() const {
        for (const auto& item : items_) {
            if (item.severity == DiagnosticSeverity::Error) return true;
        }
        return false;
    }
    std::size_t errorCount() const {
        std::size_t count = 0;
        for (const auto& item : items_) {
            if (item.severity == DiagnosticSeverity::Error) ++count;
        }
        return count;
    }

    const std::vector<Diagnostic>& items() const { return items_; }
    std::vector<Diagnostic>& items() { return items_; }

    std::string format() const {
        std::ostringstream out;
        for (const auto& item : items_) out << item.toString() << "\n";
        return out.str();
    }

private:
    std::string fileFor(int line) const {
        const SectionRange* range = rangeFor(line);
        return range != nullptr ? range->file : file_;
    }
    int lineIn(int line) const {
        const SectionRange* range = rangeFor(line);
        if (range == nullptr) return line;
        return line - range->startLine + 1 + range->lineOffset;
    }
    const SectionRange* rangeFor(int line) const {
        if (!sections_ || sections_->empty()) return nullptr;
        const SectionRange* found = &sections_->front();
        for (const auto& range : *sections_) {
            if (range.startLine > line) break;
            found = &range;
        }
        return found;
    }

    std::string file_;
    std::shared_ptr<const SectionMap> sections_;
    std::vector<Diagnostic> items_;
};

/// Thrown for script level failures (null dereference, division by zero, ...).
class ScriptRuntimeException : public std::runtime_error {
public:
    ScriptRuntimeException(std::string message, std::string file, int line, std::string stackTrace)
        : std::runtime_error(std::move(message)),
          fileName_(std::move(file)),
          line_(line),
          stackTrace_(std::move(stackTrace)) {}

    const std::string& fileName() const { return fileName_; }
    int line() const { return line_; }
    const std::string& stackTrace() const { return stackTrace_; }

    std::string format() const {
        std::ostringstream out;
        out << (fileName_.empty() ? "<script>" : fileName_) << "(" << line_ << "): runtime error: "
            << what();
        if (!stackTrace_.empty()) out << "\n" << stackTrace_;
        return out.str();
    }

private:
    std::string fileName_;
    int line_;
    std::string stackTrace_;
};

/// Thrown by native string methods when invoked on a null string reference,
/// mirroring .NET's NullReferenceException.
class ScriptNullReferenceException : public std::runtime_error {
public:
    explicit ScriptNullReferenceException(const std::string& message)
        : std::runtime_error("null reference: " + message) {}
};

class ScriptCompileException : public std::runtime_error {
public:
    explicit ScriptCompileException(std::vector<Diagnostic> diagnostics)
        : std::runtime_error("script compilation failed"), diagnostics_(std::move(diagnostics)) {}

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    std::vector<Diagnostic> diagnostics_;
};

/// Slow but deterministic monotonic clock used for compile/reload timings.
inline double millisecondsSince(const std::chrono::steady_clock::time_point& start) {
    auto duration = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(duration).count();
}

}  // namespace sharpscript
