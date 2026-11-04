#pragma once

#include "engine.hpp"

namespace sharpscript {

struct MigrationReport {
    std::vector<std::string> migrated;
    std::vector<std::string> reset;
};

/// Migrates module state across a hot reload.
///
/// Merge rule: if a global still holds the value produced by its initializer,
/// the edited initializer from the new generation wins. Otherwise the live value
/// is preserved and script objects/arrays are merged field by field, so adding,
/// removing or reordering fields does not lose runtime state.
class HotReloadManager {
public:
    explicit HotReloadManager(ScriptEngine& engine) : engine_(engine) {}

    void captureInitialValues(ScriptModule& module);
    MigrationReport migrate(const ScriptModule& previous, ScriptModule& current);

private:
    bool isCompatible(const ScriptType& oldType, const ScriptType& newType) const;
    Value merge(const Value& current, const Value& initial, const Value& fresh,
                const ScriptType& targetType, int depth);
    Value migrateFresh(const Value& fresh, const ScriptType& targetType, int depth);
    bool deepEquals(const Value& a, const Value& b, int depth) const;
    Value cloneValue(const Value& value, int depth) const;

    ScriptEngine& engine_;
};

}  // namespace sharpscript
