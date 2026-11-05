#pragma once

#include "function.hpp"

namespace sharpscript {

/// One source file of a module. AngelScript calls these "sections": they share
/// the module's global namespace, so `player.cs` can use a class declared in
/// `main.cs` without any import.
struct ScriptSection {
    /// Name used in diagnostics; empty falls back to the module's source file.
    std::string file;
    std::string source;
    /// Added to reported lines so a fragment lifted from a larger file keeps
    /// its original numbering.
    int lineOffset = 0;

    ScriptSection() = default;
    ScriptSection(std::string file, std::string source, int lineOffset = 0)
        : file(std::move(file)), source(std::move(source)), lineOffset(lineOffset) {}
};

/// A compiled script unit. Reloading creates a new generation; running contexts
/// keep the old shared_ptr alive until their frames return.
class ScriptModule : public std::enable_shared_from_this<ScriptModule> {
public:
    ScriptModule(ScriptEngine* engine, std::string name, std::string source, std::string sourceFile, int generation)
        : engine(engine),
          name(std::move(name)),
          source(std::move(source)),
          sourceFile(std::move(sourceFile)),
          generation(generation) {}

    ScriptEngine* engine;
    std::string name;
    /// Concatenation of every section, in order.
    std::string source;
    std::string sourceFile;
    int generation = 1;
    double compileTimeMs = 0.0;

    /// Sections this module was built from, in order (kept so reloads reuse them).
    std::vector<ScriptSection> sections;
    /// Absolute line at which each section starts (1-based, parallel to `sections`).
    std::vector<int> sectionStartLines;

    /// Index of the section a concatenated line belongs to (-1 when unknown).
    int sectionOfLine(int line) const {
        int found = sectionStartLines.empty() ? -1 : 0;
        for (std::size_t i = 0; i < sectionStartLines.size(); ++i) {
            if (sectionStartLines[i] > line) break;
            found = static_cast<int>(i);
        }
        return found;
    }
    std::string fileOfLine(int line) const {
        const int index = sectionOfLine(line);
        if (index < 0 || index >= static_cast<int>(sections.size())) return sourceFile;
        return sections[static_cast<std::size_t>(index)].file;
    }

    std::vector<std::unique_ptr<ScriptFunction>> functions;
    std::unordered_map<std::string, std::vector<ScriptFunction*>> functionsByName;
    std::vector<std::unique_ptr<ScriptClassType>> classes;
    std::unordered_map<std::string, ScriptClassType*> classesByName;
    std::vector<std::unique_ptr<ScriptDelegateType>> delegates;
    std::unordered_map<std::string, ScriptDelegateType*> delegatesByName;
    std::vector<GlobalVariable> globals;
    std::unordered_map<std::string, int> globalsByName;

    ScriptFunction* globalInit = nullptr;
    ScriptFunction* entryPoint = nullptr;
    bool obsolete = false;

    /// Set when a reload patched method bodies in place instead of swapping the
    /// module: the discarded generation still owns the attached functions and
    /// the code copied from it, so it must stay alive with this module.
    std::shared_ptr<ScriptModule> patchedDependency;

    /// Snapshot taken right after module initialization; used by the reloader to
    /// distinguish edited initializers from state mutated by script code.
    std::unordered_map<std::string, Value> initialGlobalValues;

    ScriptFunction* findFunction(const std::string& functionName, std::size_t arity) const {
        auto it = functionsByName.find(functionName);
        if (it == functionsByName.end()) return nullptr;
        for (auto* function : it->second) {
            if (function->parameters.size() == arity) return function;
        }
        return nullptr;
    }

    ScriptClassType* findClass(const std::string& className) const {
        auto it = classesByName.find(className);
        return it == classesByName.end() ? nullptr : it->second;
    }

    std::string toString() const {
        return "module '" + name + "' gen " + std::to_string(generation);
    }
};

}  // namespace sharpscript
