#pragma once

#include "engine.hpp"

namespace sharpscript {

/// Stack based interpreter: one recursive C++ call per script call, each frame
/// owning its locals and a private operand stack.
class ExecutionContext {
public:
    explicit ExecutionContext(ScriptEngine& engine);

    Value execute(ScriptFunction* function, const std::vector<Value>& arguments);
    /// Runs `function` with an explicit receiver (used by finalizers).
    Value execute(ScriptFunction* function, const std::vector<Value>& arguments, const Value& self);

    ScriptEngine& engine() { return engine_; }
    const Value& returnValue() const { return returnValue_; }

    /// Throws a script exception from native code. It propagates to the nearest
    /// matching `catch` in the calling script, or reaches the host as a
    /// ScriptRuntimeException when uncaught.
    [[noreturn]] void throwException(const Value& exception);

private:
    struct FrameInfo {
        ScriptFunction* function = nullptr;
        std::shared_ptr<ScriptModule> module;
        int callSite = -1;
    };

    /// Control transfer interrupted by a `finally` handler (IL style).
    enum class PendingActionKind { Leave, Return, Throw };
    struct PendingAction {
        PendingActionKind kind = PendingActionKind::Leave;
        int targetPc = 0;   ///< Leave destination
        int resumePc = 0;   ///< pc to continue searching finally/catch from
        Value value;        ///< return value or exception
    };

    Value invokeFunction(ScriptFunction* function, std::vector<Value>& arguments, const Value& self, int depth,
                         const std::vector<Value>* captures = nullptr);
    Value invokeNativeFunction(NativeFunction* function, const Value& self, std::vector<Value>& arguments);
    Value convertArgument(const Value& value, const ScriptType& target, ScriptFunction* function);
    Value convertValue(const Value& value, const ScriptType& target, ScriptFunction* function, int pc);
    [[noreturn]] void throwRuntime(const std::string& message, ScriptFunction* function, int pc);
    std::string buildStackTrace(ScriptFunction* function, int pc) const;
    /// Fills the exception's stack trace on first throw.
    void prepareException(Value& exception, ScriptFunction* function, int pc);
    /// Searches `function`'s catch table for a handler covering `pc`. On success
    /// rewrites `pc` to the handler entry and returns true.
    bool handleException(const Value& exception, ScriptFunction* function, int& pc,
                         std::vector<Value>& stack, std::vector<Value>& locals);
    /// Index of the innermost `finally` covering `fromPc` but not `targetPc`.
    int findFinallyBetween(ScriptFunction* function, int fromPc, int targetPc) const;
    /// True when `pc` is inside a finally handler body.
    bool insideFinallyBody(ScriptFunction* function, int pc, int& handlerIndex) const;

    ScriptEngine& engine_;
    std::vector<FrameInfo> frames_;
    std::int64_t instructionCount_ = 0;
    const char* stackBase_ = nullptr;
    Value returnValue_;
    /// Set while an exception unwinds the script frames.
    bool unwinding_ = false;
    Value pendingException_;
    ScriptFunction* pendingFunction_ = nullptr;
    int pendingPc_ = 0;
    /// Pending control transfers of the frame currently executing (swapped by
    /// nested invokeFunction calls so frames cannot corrupt each other).
    std::vector<PendingAction>* actions_ = nullptr;
};

}  // namespace sharpscript
