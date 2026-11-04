#pragma once

#include "module.hpp"

namespace sharpscript {

/// Outcome of comparing two generations for in-place method-body patching.
///
/// Patching keeps every existing `ScriptFunction`/`ScriptType` object identity,
/// so call sites, delegate entries and vtable slots keep working while the
/// bodies are replaced. That is only safe when the *structure* of the module is
/// unchanged, which is what `planMethodReload` verifies.
struct MethodReloadPlan {
    /// True when every check passed and the plan may be applied.
    bool canPatchInline = false;
    /// `(old, new)` pairs whose bodies will be copied into the old functions.
    std::vector<std::pair<ScriptFunction*, ScriptFunction*>> patchPairs;
    /// Functions that exist only in the new generation; they are attached to the
    /// previous module so newly compiled call sites can find them.
    std::vector<ScriptFunction*> added;
    /// Human readable reasons that force a full module reload.
    std::vector<std::string> blockingReasons;

    bool blocked() const { return !blockingReasons.empty(); }
    std::size_t patchedCount() const { return patchPairs.size(); }
};

/// Compares two generations without modifying either of them.
MethodReloadPlan planMethodReload(const ScriptModule& previous, const ScriptModule& next);

/// Applies a plan in place. `next` is kept alive through
/// `ScriptModule::patchedDependency` because attached functions and any new
/// bytecode tables still own storage inside it.
void applyMethodReload(ScriptModule& previous, ScriptModule& next, const MethodReloadPlan& plan);

}  // namespace sharpscript
