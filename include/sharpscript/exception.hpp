#pragma once

#include "value.hpp"

namespace sharpscript {

/// Runtime payload behind a script exception value. The script-visible class is
/// a native type (`Exception` or one of its built-in subclasses); `type` points
/// at that type so `catch` clauses can match the hierarchy at runtime.
struct ScriptExceptionInfo {
    NativeObjectType* type = nullptr;
    std::u16string message;
    std::u16string stackTrace;
};

/// Internal, payload-free signal used to abort the instruction currently being
/// executed. The interpreter catches it inside the frame, stores the pending
/// exception and either jumps to a handler or returns to the caller, which
/// keeps unwinding without nesting C++ catch blocks.
struct ScriptAbort {};

}  // namespace sharpscript
