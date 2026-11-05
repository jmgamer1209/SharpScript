#include "sharpscript/context.hpp"

#include "sharpscript/bytecode.hpp"
#include "sharpscript/exception.hpp"
#include "sharpscript/unicode.hpp"

namespace sharpscript {
namespace {

/// Maps interpreter diagnostics to the built-in exception classes so scripts
/// can catch e.g. `IndexOutOfRangeException` for an out-of-range access.
const char* exceptionTypeNameFor(const std::string& message) {
    if (message.find("null reference") != std::string::npos ||
        message.find("null exception") != std::string::npos) {
        return "NullReferenceException";
    }
    if (message.find("out of range") != std::string::npos) return "IndexOutOfRangeException";
    if (message.find("division by zero") != std::string::npos ||
        message.find("modulo by zero") != std::string::npos) {
        return "DivideByZeroException";
    }
    if (message.find("cannot convert") != std::string::npos ||
        message.find("cannot cast") != std::string::npos) {
        return "InvalidCastException";
    }
    if (message.find("maximum call depth") != std::string::npos ||
        message.find("instruction limit") != std::string::npos) {
        return "StackOverflowException";
    }
    if (message.find("argument") != std::string::npos) return "ArgumentException";
    return "Exception";
}

bool tryConvert(const Value& value, const ScriptType& target, Value& result, std::string& error,
                GcCollector& collector) {
    if (target.isVar()) {
        result = value;
        return true;
    }
    if (target.isVoid()) {
        result = Value::voidValue();
        return true;
    }
    if (target.isString()) {
        if (value.kind() == ValueKind::String) {
            result = value;
            return true;
        }
        if (value.isNull()) {
            result = Value::null();
            return true;
        }
        error = "cannot convert '" + value.typeName() + "' to 'string'";
        return false;
    }
    if (target.isArray()) {
        if (value.isNull()) {
            result = Value::null();
            return true;
        }
        auto array = value.asArray();
        if (array && array->elementType() && array->elementType()->sameAs(*target.element)) {
            result = value;
            return true;
        }
        error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
        return false;
    }
    if (target.isDelegate()) {
        if (value.isNull()) {
            result = Value::null();
            return true;
        }
        auto delegate = value.asDelegate();
        if (delegate && delegate->type() == target.object) {
            result = value;
            return true;
        }
        error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
        return false;
    }
    if (target.isStruct()) {
        auto source = value.asStruct();
        if (source && source->type() == target.object) {
            // Value semantics: every store/argument/return gets an independent copy.
            result = cloneStructValue(value, collector);
            return true;
        }
        error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
        return false;
    }
    if (auto* cls = dynamic_cast<ScriptClassType*>(target.object)) {
        if (value.isNull()) {
            result = Value::null();
            return true;
        }
        auto instance = value.asInstance();
        if (instance && cls->isAssignableFrom(instance->cls())) {
            result = value;
            return true;
        }
        error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
        return false;
    }
    if (target.object != nullptr) {
        if (value.isNull()) {
            result = Value::null();
            return true;
        }
        if (value.kind() == ValueKind::Native) {
            result = value;
            return true;
        }
        error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
        return false;
    }
    if (value.isNull()) {
        error = "cannot convert 'null' to '" + target.name + "'";
        return false;
    }
    // bool/char are not numeric kinds, but identical types still convert trivially.
    if (target.primitive == Primitive::Bool && value.kind() == ValueKind::Bool) {
        result = value;
        return true;
    }
    if (target.primitive == Primitive::Char && value.kind() == ValueKind::Char) {
        result = value;
        return true;
    }
    if (!isNumericKind(value.kind())) {
        error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
        return false;
    }

    switch (target.primitive) {
        case Primitive::Bool: result = Value::boolean(value.asBool()); return true;
        case Primitive::Int8: result = Value::i8(static_cast<std::int8_t>(value.asI64())); return true;
        case Primitive::Int16: result = Value::i16(static_cast<std::int16_t>(value.asI64())); return true;
        case Primitive::Int32: result = Value::i32(static_cast<std::int32_t>(value.asI64())); return true;
        case Primitive::Int64: result = Value::i64(value.asI64()); return true;
        case Primitive::UInt8: result = Value::u8(static_cast<std::uint8_t>(value.asU64())); return true;
        case Primitive::UInt16: result = Value::u16(static_cast<std::uint16_t>(value.asU64())); return true;
        case Primitive::UInt32: result = Value::u32(static_cast<std::uint32_t>(value.asU64())); return true;
        case Primitive::UInt64: result = Value::u64(value.asU64()); return true;
        case Primitive::Float32: result = Value::f32(static_cast<float>(value.asF64())); return true;
        case Primitive::Float64: result = Value::f64(value.asF64()); return true;
        case Primitive::Char:
            result = Value::character(static_cast<char16_t>(value.asU64()));
            return true;
        default:
            error = "cannot convert '" + value.typeName() + "' to '" + target.name + "'";
            return false;
    }
}

/// Field storage of a script object or a boxed struct.
std::vector<Value>* fieldsOf(Value& value) {
    if (auto instance = value.asInstance()) return &instance->fields();
    if (auto structValue = value.asStruct()) return &structValue->fields();
    return nullptr;
}

Value castValue(const Value& value, const ScriptType& target) {
    if (value.isNull()) return Value::null();
    if (target.isString()) return value.kind() == ValueKind::String ? value : Value::null();
    if (target.isArray()) {
        auto array = value.asArray();
        return array && array->elementType() && array->elementType()->sameAs(*target.element)
                   ? value
                   : Value::null();
    }
    if (target.isDelegate()) {
        auto delegate = value.asDelegate();
        return delegate && delegate->type() == target.object ? value : Value::null();
    }
    if (auto* cls = dynamic_cast<ScriptClassType*>(target.object)) {
        auto instance = value.asInstance();
        return instance && cls->isAssignableFrom(instance->cls()) ? value : Value::null();
    }
    if (target.object != nullptr) {
        auto* targetNative = dynamic_cast<NativeObjectType*>(target.object);
        auto info = value.asNative<ScriptExceptionInfo>();
        if (info != nullptr && targetNative != nullptr) {
            bool matches = info->type != nullptr && targetNative->isAssignableFrom(info->type);
            return matches ? value : Value::null();
        }
        return value.kind() == ValueKind::Native ? value : Value::null();
    }
    return Value::null();
}

bool isTypeOf(const Value& value, const ScriptType& target) {
    if (value.isNull()) return false;
    if (target.isString()) return value.kind() == ValueKind::String;
    if (target.isArray()) {
        auto array = value.asArray();
        return array && array->elementType() && array->elementType()->sameAs(*target.element);
    }
    if (target.isDelegate()) {
        auto delegate = value.asDelegate();
        return delegate && delegate->type() == target.object;
    }
    if (auto* cls = dynamic_cast<ScriptClassType*>(target.object)) {
        auto instance = value.asInstance();
        return instance && cls->isAssignableFrom(instance->cls());
    }
    if (target.object != nullptr) {
        auto* targetNative = dynamic_cast<NativeObjectType*>(target.object);
        auto info = value.asNative<ScriptExceptionInfo>();
        if (info != nullptr && targetNative != nullptr) {
            return info->type != nullptr && targetNative->isAssignableFrom(info->type);
        }
        return value.kind() == ValueKind::Native;
    }
    return false;
}

}  // namespace

ExecutionContext::ExecutionContext(ScriptEngine& engine) : engine_(engine) {}

Value ExecutionContext::execute(ScriptFunction* function, const std::vector<Value>& arguments) {
    return execute(function, arguments, Value::null());
}

Value ExecutionContext::execute(ScriptFunction* function, const std::vector<Value>& arguments,
                                const Value& self) {
    frames_.clear();
    instructionCount_ = 0;
    unwinding_ = false;
    pendingException_ = Value::null();
    pendingFunction_ = nullptr;
    pendingPc_ = 0;
    actions_ = nullptr;
    char stackMarker = 0;
    stackBase_ = &stackMarker;
    auto args = arguments;
    try {
        returnValue_ = invokeFunction(function, args, self, 0);
    } catch (const ScriptAbort&) {
        // The exception was not caught by any script handler.
    }
    if (unwinding_) {
        Value exception = pendingException_;
        unwinding_ = false;
        pendingException_ = Value::null();
        auto info = exception.asNative<ScriptExceptionInfo>();
        std::string message = info ? utf16ToUtf8(info->message) : exception.display();
        ScriptFunction* origin = pendingFunction_ != nullptr ? pendingFunction_ : function;
        int line = origin->lineAt(pendingPc_);
        std::string trace = info ? utf16ToUtf8(info->stackTrace) : std::string();
        throw ScriptRuntimeException(message, origin->file, line, trace);
    }
    return returnValue_;
}

Value ExecutionContext::invokeFunction(ScriptFunction* function, std::vector<Value>& arguments,
                                       const Value& self, int depth, const std::vector<Value>* captures) {
    if (function == nullptr) throw std::runtime_error("attempted to call a null function");
    if (depth > engine_.settings.maxCallDepth) {
        throwRuntime("maximum call depth exceeded (possible infinite recursion)", function, 0);
    }
    if (stackBase_ != nullptr && engine_.settings.maxStackBytes > 0) {
        char stackMarker = 0;
        std::ptrdiff_t used = stackBase_ - &stackMarker;
        if (used > 0 && used > static_cast<std::ptrdiff_t>(engine_.settings.maxStackBytes)) {
            throwRuntime("maximum call depth exceeded (native stack budget)", function, 0);
        }
    }
    std::vector<PendingAction> frameActions;
    auto* savedActions = actions_;
    actions_ = &frameActions;
    struct ActionsRestore {
        std::vector<PendingAction>** slot;
        std::vector<PendingAction>* saved;
        ~ActionsRestore() { *slot = saved; }
    } actionsRestore{&actions_, savedActions};

    if (function->native != nullptr) {
        return invokeNativeFunction(function->native, self, arguments);
    }
    if (function->isAbstract || function->code.empty()) {
        throwRuntime("cannot call abstract function '" + function->qualifiedName() + "'", function, 0);
    }

    std::vector<Value> locals(static_cast<std::size_t>(std::max(1, function->localSlotCount)));
    int slot = 0;
    if (!function->isStatic) locals[static_cast<std::size_t>(slot++)] = self;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i >= function->parameters.size()) break;
        const auto& parameter = function->parameters[i];
        if (parameter.isRef()) {
            // The call site already built a storage reference; never copy it.
            if (!arguments[i].asRef()) {
                throwRuntime("parameter '" + parameter.name + "' requires a reference argument", function, 0);
            }
            locals[static_cast<std::size_t>(slot + static_cast<int>(i))] = arguments[i];
            continue;
        }
        locals[static_cast<std::size_t>(slot + static_cast<int>(i))] =
            convertArgument(arguments[i], parameter.type, function);
    }
    if (captures != nullptr) {
        // Closure cells occupy the slots after the declared parameters.
        for (std::size_t i = 0; i < captures->size(); ++i) {
            const int targetSlot = slot + static_cast<int>(function->parameters.size() + i);
            if (targetSlot < static_cast<int>(locals.size())) {
                locals[static_cast<std::size_t>(targetSlot)] = (*captures)[i];
            }
        }
    }

    std::vector<Value> stack;
    stack.reserve(static_cast<std::size_t>(std::max(16, function->maxStack + 16)));
    int pc = 0;

    auto module = function->module->shared_from_this();
    frames_.push_back(FrameInfo{function, module, -1});
    std::size_t frameIndex = frames_.size() - 1;

    struct FrameGuard {
        std::vector<FrameInfo>& frames;
        ~FrameGuard() {
            if (!frames.empty()) frames.pop_back();
        }
    } guard{frames_};

    auto push = [&stack](const Value& value) { stack.push_back(value); };
    auto pop = [&]() -> Value {
        if (stack.empty()) throwRuntime("operand stack underflow", function, pc - 1);
        Value value = stack.back();
        stack.pop_back();
        return value;
    };
    auto popArguments = [&](int count) -> std::vector<Value> {
        if (count < 0 || static_cast<int>(stack.size()) < count) {
            throwRuntime("operand stack underflow while reading arguments", function, pc - 1);
        }
        std::vector<Value> values(static_cast<std::size_t>(count));
        for (int i = count - 1; i >= 0; --i) values[static_cast<std::size_t>(i)] = pop();
        return values;
    };

    while (true) {
        ++instructionCount_;
        if (engine_.settings.maxInstructionsPerExecution > 0 &&
            (instructionCount_ & 0x3FFF) == 0 &&
            instructionCount_ > engine_.settings.maxInstructionsPerExecution) {
            throwRuntime("instruction limit exceeded", function, pc - 1);
        }

        std::uint32_t word = function->code[static_cast<std::size_t>(pc)];
        auto decoded = decodeInstruction(word);
        OpCode op = decoded.op;
        int arg = decoded.arg;
        ++pc;

        try {
        switch (op) {
            case OpCode::Nop: break;
            case OpCode::PushNull: push(Value::null()); break;
            case OpCode::PushTrue: push(Value::boolean(true)); break;
            case OpCode::PushFalse: push(Value::boolean(false)); break;
            case OpCode::PushI32Imm: push(Value::i32(signExtend24(arg))); break;
            case OpCode::PushConst: push(function->constants[static_cast<std::size_t>(arg)]); break;
            case OpCode::Pop: pop(); break;
            case OpCode::Dup:
                if (stack.empty()) throwRuntime("operand stack underflow (dup)", function, pc - 1);
                push(stack.back());
                break;
            case OpCode::Dup2: {
                if (stack.size() < 2) throwRuntime("operand stack underflow (dup2)", function, pc - 1);
                Value first = stack[stack.size() - 2];
                Value second = stack[stack.size() - 1];
                push(first);
                push(second);
                break;
            }
            case OpCode::Swap: {
                if (stack.size() < 2) throwRuntime("operand stack underflow (swap)", function, pc - 1);
                std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
                break;
            }
            case OpCode::LoadLocal: push(locals[static_cast<std::size_t>(arg)]); break;
            case OpCode::StoreLocal: locals[static_cast<std::size_t>(arg)] = pop(); break;
            case OpCode::LoadGlobal: push(function->module->globals[static_cast<std::size_t>(arg)].value); break;
            case OpCode::StoreGlobal: {
                auto& variable = function->module->globals[static_cast<std::size_t>(arg)];
                variable.value = convertArgument(pop(), variable.type, function);
                break;
            }
            case OpCode::LoadField: {
                FieldInfo* field = function->fieldRefs[static_cast<std::size_t>(arg)];
                Value receiver = pop();
                std::vector<Value>* fields = fieldsOf(receiver);
                if (fields == nullptr) {
                    throwRuntime("null reference while reading field '" + field->name + "'", function, pc - 1);
                }
                push((*fields)[static_cast<std::size_t>(field->index)]);
                break;
            }
            case OpCode::StoreField: {
                FieldInfo* field = function->fieldRefs[static_cast<std::size_t>(arg)];
                Value value = pop();
                Value receiver = pop();
                std::vector<Value>* fields = fieldsOf(receiver);
                if (fields == nullptr) {
                    throwRuntime("null reference while writing field '" + field->name + "'", function, pc - 1);
                }
                (*fields)[static_cast<std::size_t>(field->index)] =
                    convertArgument(value, field->type, function);
                break;
            }
            case OpCode::LoadThis: push(self); break;
            case OpCode::NewCell: {
                Value initial = pop();
                push(Value::cellValue(engine_.garbageCollector().make<ScriptCell>(std::move(initial))));
                break;
            }
            case OpCode::LoadCell: {
                Value cellValue = pop();
                auto cell = cellValue.asCell();
                if (!cell) throwRuntime("captured variable cell is invalid", function, pc - 1);
                push(cell->value);
                break;
            }
            case OpCode::StoreCell: {
                Value cellValue = pop();
                Value value = pop();
                auto cell = cellValue.asCell();
                if (!cell) throwRuntime("captured variable cell is invalid", function, pc - 1);
                cell->value = value;
                break;
            }
            case OpCode::MakeRefCell: {
                Value cellValue = pop();
                auto cell = cellValue.asCell();
                if (!cell) throwRuntime("invalid reference target", function, pc - 1);
                auto ref = engine_.garbageCollector().make<ScriptRef>();
                ref->refKind = RefKind::Cell;
                ref->cell = cell;
                push(Value::refValue(ref));
                break;
            }
            case OpCode::MakeRefGlobal: {
                auto module = function->module->shared_from_this();
                auto ref = engine_.garbageCollector().make<ScriptRef>();
                ref->refKind = RefKind::Global;
                ref->global = &module->globals[static_cast<std::size_t>(arg)].value;
                ref->moduleOwner = module;
                push(Value::refValue(ref));
                break;
            }
            case OpCode::MakeRefField: {
                FieldInfo* field = function->fieldRefs[static_cast<std::size_t>(arg)];
                Value receiver = pop();
                auto ref = engine_.garbageCollector().make<ScriptRef>();
                ref->refKind = RefKind::Field;
                ref->owner = receiver;
                ref->index = field->index;
                push(Value::refValue(ref));
                break;
            }
            case OpCode::MakeRefElem: {
                Value indexValue = pop();
                Value arrayValue = pop();
                auto ref = engine_.garbageCollector().make<ScriptRef>();
                ref->refKind = RefKind::Elem;
                ref->owner = arrayValue;
                ref->index = indexValue.asI32();
                push(Value::refValue(ref));
                break;
            }
            case OpCode::LoadRef: {
                auto ref = pop().asRef();
                if (!ref) throwRuntime("invalid reference", function, pc - 1);
                push(ref->load());
                break;
            }
            case OpCode::StoreRef: {
                auto ref = pop().asRef();
                Value value = pop();
                if (!ref) throwRuntime("invalid reference", function, pc - 1);
                ref->store(value);
                break;
            }
            case OpCode::LoadElem: {
                int index = pop().asI32();
                Value target = pop();
                if (auto array = target.asArray()) {
                    if (index < 0 || index >= static_cast<int>(array->items().size())) {
                        throwRuntime("array index " + std::to_string(index) + " is out of range (length " +
                                         std::to_string(array->items().size()) + ")",
                                     function, pc - 1);
                    }
                    push(array->items()[static_cast<std::size_t>(index)]);
                } else if (target.kind() == ValueKind::String) {
                    const auto& text = target.asString();
                    if (index < 0 || index >= static_cast<int>(text.size())) {
                        throwRuntime("string index " + std::to_string(index) + " is out of range (length " +
                                         std::to_string(text.size()) + ")",
                                     function, pc - 1);
                    }
                    push(Value::character(text[static_cast<std::size_t>(index)]));
                } else if (target.isNull()) {
                    throwRuntime("null reference: cannot index a null string", function, pc - 1);
                } else {
                    throwRuntime("cannot index a value of kind '" + target.typeName() + "'", function, pc - 1);
                }
                break;
            }
            case OpCode::StoreElem: {
                Value value = pop();
                int index = pop().asI32();
                auto array = pop().asArray();
                if (!array) throwRuntime("null reference while writing an array element", function, pc - 1);
                if (index < 0 || index >= static_cast<int>(array->items().size())) {
                    throwRuntime("array index " + std::to_string(index) + " is out of range (length " +
                                     std::to_string(array->items().size()) + ")",
                                 function, pc - 1);
                }
                array->items()[static_cast<std::size_t>(index)] =
                    convertArgument(value, *array->elementType(), function);
                break;
            }
            case OpCode::Length: {
                Value target = pop();
                if (auto array = target.asArray()) {
                    push(Value::i32(static_cast<std::int32_t>(array->items().size())));
                } else if (target.kind() == ValueKind::String) {
                    // UTF-16 code units, matching System.String.Length.
                    push(Value::i32(static_cast<std::int32_t>(target.asString().size())));
                } else if (target.isNull()) {
                    throwRuntime("null reference: cannot read Length of a null string", function, pc - 1);
                } else {
                    throwRuntime("length is not defined for '" + target.typeName() + "'", function, pc - 1);
                }
                break;
            }

            case OpCode::AddI32:
            case OpCode::SubI32:
            case OpCode::MulI32:
            case OpCode::DivI32:
            case OpCode::ModI32:
            case OpCode::BAndI32:
            case OpCode::BOrI32:
            case OpCode::BXorI32:
            case OpCode::BShlI32:
            case OpCode::BShrI32:
            case OpCode::BShrUI32: {
                Value b = pop();
                Value a = pop();
                try {
                    switch (op) {
                        case OpCode::AddI32: push(ValueOps::addI32(a, b)); break;
                        case OpCode::SubI32: push(ValueOps::subI32(a, b)); break;
                        case OpCode::MulI32: push(ValueOps::mulI32(a, b)); break;
                        case OpCode::DivI32: push(ValueOps::divI32(a, b)); break;
                        case OpCode::ModI32: push(ValueOps::modI32(a, b)); break;
                        case OpCode::BAndI32: push(ValueOps::bAndI32(a, b)); break;
                        case OpCode::BOrI32: push(ValueOps::bOrI32(a, b)); break;
                        case OpCode::BXorI32: push(ValueOps::bXorI32(a, b)); break;
                        case OpCode::BShlI32: push(ValueOps::bShlI32(a, b)); break;
                        case OpCode::BShrI32: push(ValueOps::bShrI32(a, b)); break;
                        default: push(ValueOps::bShrUI32(a, b)); break;
                    }
                } catch (const std::exception& ex) {
                    throwRuntime(ex.what(), function, pc - 1);
                }
                break;
            }
            case OpCode::NegI32: push(ValueOps::negI32(pop())); break;
            case OpCode::BNotI32: push(ValueOps::bNotI32(pop())); break;

            case OpCode::AddI64:
            case OpCode::SubI64:
            case OpCode::MulI64:
            case OpCode::DivI64:
            case OpCode::ModI64:
            case OpCode::BAndI64:
            case OpCode::BOrI64:
            case OpCode::BXorI64:
            case OpCode::BShlI64:
            case OpCode::BShrI64:
            case OpCode::BShrUI64: {
                Value b = pop();
                Value a = pop();
                try {
                    switch (op) {
                        case OpCode::AddI64: push(ValueOps::addI64(a, b)); break;
                        case OpCode::SubI64: push(ValueOps::subI64(a, b)); break;
                        case OpCode::MulI64: push(ValueOps::mulI64(a, b)); break;
                        case OpCode::DivI64: push(ValueOps::divI64(a, b)); break;
                        case OpCode::ModI64: push(ValueOps::modI64(a, b)); break;
                        case OpCode::BAndI64: push(ValueOps::bAndI64(a, b)); break;
                        case OpCode::BOrI64: push(ValueOps::bOrI64(a, b)); break;
                        case OpCode::BXorI64: push(ValueOps::bXorI64(a, b)); break;
                        case OpCode::BShlI64: push(ValueOps::bShlI64(a, b)); break;
                        case OpCode::BShrI64: push(ValueOps::bShrI64(a, b)); break;
                        default: push(ValueOps::bShrUI64(a, b)); break;
                    }
                } catch (const std::exception& ex) {
                    throwRuntime(ex.what(), function, pc - 1);
                }
                break;
            }
            case OpCode::NegI64: push(ValueOps::negI64(pop())); break;
            case OpCode::BNotI64: push(ValueOps::bNotI64(pop())); break;

            case OpCode::AddF32:
            case OpCode::SubF32:
            case OpCode::MulF32:
            case OpCode::DivF32:
            case OpCode::ModF32: {
                Value b = pop();
                Value a = pop();
                switch (op) {
                    case OpCode::AddF32: push(ValueOps::addF32(a, b)); break;
                    case OpCode::SubF32: push(ValueOps::subF32(a, b)); break;
                    case OpCode::MulF32: push(ValueOps::mulF32(a, b)); break;
                    case OpCode::DivF32: push(ValueOps::divF32(a, b)); break;
                    default: push(ValueOps::modF32(a, b)); break;
                }
                break;
            }
            case OpCode::NegF32: push(ValueOps::negF32(pop())); break;

            case OpCode::AddF64:
            case OpCode::SubF64:
            case OpCode::MulF64:
            case OpCode::DivF64:
            case OpCode::ModF64: {
                Value b = pop();
                Value a = pop();
                switch (op) {
                    case OpCode::AddF64: push(ValueOps::addF64(a, b)); break;
                    case OpCode::SubF64: push(ValueOps::subF64(a, b)); break;
                    case OpCode::MulF64: push(ValueOps::mulF64(a, b)); break;
                    case OpCode::DivF64: push(ValueOps::divF64(a, b)); break;
                    default: push(ValueOps::modF64(a, b)); break;
                }
                break;
            }
            case OpCode::NegF64: push(ValueOps::negF64(pop())); break;
            case OpCode::AddStr: {
                Value b = pop();
                Value a = pop();
                push(ValueOps::addString(a, b));
                break;
            }
            case OpCode::Not: push(ValueOps::logicalNot(pop())); break;

            case OpCode::IncI32:
                locals[static_cast<std::size_t>(arg)] = Value::i32(locals[static_cast<std::size_t>(arg)].asI32() + 1);
                break;
            case OpCode::DecI32:
                locals[static_cast<std::size_t>(arg)] = Value::i32(locals[static_cast<std::size_t>(arg)].asI32() - 1);
                break;
            case OpCode::IncI64:
                locals[static_cast<std::size_t>(arg)] = Value::i64(locals[static_cast<std::size_t>(arg)].asI64() + 1);
                break;
            case OpCode::DecI64:
                locals[static_cast<std::size_t>(arg)] = Value::i64(locals[static_cast<std::size_t>(arg)].asI64() - 1);
                break;
            case OpCode::IncF32:
                locals[static_cast<std::size_t>(arg)] = Value::f32(static_cast<float>(locals[static_cast<std::size_t>(arg)].asF64() + 1));
                break;
            case OpCode::DecF32:
                locals[static_cast<std::size_t>(arg)] = Value::f32(static_cast<float>(locals[static_cast<std::size_t>(arg)].asF64() - 1));
                break;
            case OpCode::IncF64:
                locals[static_cast<std::size_t>(arg)] = Value::f64(locals[static_cast<std::size_t>(arg)].asF64() + 1);
                break;
            case OpCode::DecF64:
                locals[static_cast<std::size_t>(arg)] = Value::f64(locals[static_cast<std::size_t>(arg)].asF64() - 1);
                break;

            case OpCode::CmpEQ:
            case OpCode::CmpNE:
            case OpCode::CmpLT:
            case OpCode::CmpLE:
            case OpCode::CmpGT:
            case OpCode::CmpGE: {
                Value b = pop();
                Value a = pop();
                bool result = false;
                if (op == OpCode::CmpEQ) result = ValueOps::areEqual(a, b);
                else if (op == OpCode::CmpNE) result = !ValueOps::areEqual(a, b);
                else if (op == OpCode::CmpLT) result = ValueOps::compare(a, b) < 0;
                else if (op == OpCode::CmpLE) result = ValueOps::compare(a, b) <= 0;
                else if (op == OpCode::CmpGT) result = ValueOps::compare(a, b) > 0;
                else result = ValueOps::compare(a, b) >= 0;
                push(Value::boolean(result));
                break;
            }

            case OpCode::Jmp: pc = arg; break;
            case OpCode::Jz:
                if (!pop().asBool()) pc = arg;
                break;
            case OpCode::Jnz:
                if (pop().asBool()) pc = arg;
                break;

            case OpCode::ConvI8: push(convertValue(pop(), primI8(), function, pc - 1)); break;
            case OpCode::ConvI16: push(convertValue(pop(), primI16(), function, pc - 1)); break;
            case OpCode::ConvI32: push(convertValue(pop(), primI32(), function, pc - 1)); break;
            case OpCode::ConvI64: push(convertValue(pop(), primI64(), function, pc - 1)); break;
            case OpCode::ConvU8: push(convertValue(pop(), primU8(), function, pc - 1)); break;
            case OpCode::ConvU16: push(convertValue(pop(), primU16(), function, pc - 1)); break;
            case OpCode::ConvU32: push(convertValue(pop(), primU32(), function, pc - 1)); break;
            case OpCode::ConvU64: push(convertValue(pop(), primU64(), function, pc - 1)); break;
            case OpCode::ConvF32: push(convertValue(pop(), primF32(), function, pc - 1)); break;
            case OpCode::ConvF64: push(convertValue(pop(), primF64(), function, pc - 1)); break;
            case OpCode::ConvChar: push(convertValue(pop(), primChar(), function, pc - 1)); break;
            case OpCode::ConvBool: push(Value::boolean(pop().asBool())); break;
            case OpCode::ConvStr: push(Value::string(pop().toScriptString())); break;

            case OpCode::Call: {
                auto* target = function->functionRefs[static_cast<std::size_t>(arg)];
                auto callArgs = popArguments(static_cast<int>(target->parameters.size()));
                Value receiver = target->isStatic ? Value::null() : pop();
                frames_[frameIndex].callSite = pc - 1;
                Value result = invokeFunction(target, callArgs, receiver, depth + 1);
                if (unwinding_) {
                    int site = pc - 1;
                    if (!handleException(pendingException_, function, site, stack, locals)) {
                        return Value::voidValue();
                    }
                    unwinding_ = false;
                    pendingException_ = Value::null();
                    pc = site;
                    continue;
                }
                if (!target->returnType.isVoid()) push(result);
                break;
            }
            case OpCode::CallVirt: {
                auto site = function->virtualRefs[static_cast<std::size_t>(arg)];
                auto callArgs = popArguments(site.parameterCount);
                Value receiver = pop();
                auto instance = receiver.asInstance();
                if (!instance) throwRuntime("virtual call on a null reference", function, pc - 1);
                auto& vtable = site.isInterface ? instance->cls()->interfaceVtable : instance->cls()->vtable;
                if (site.slot < 0 || site.slot >= static_cast<int>(vtable.size())) {
                    throwRuntime("invalid virtual method slot", function, pc - 1);
                }
                auto* target = vtable[static_cast<std::size_t>(site.slot)];
                if (target == nullptr) {
                    throwRuntime("interface method is not implemented", function, pc - 1);
                }
                frames_[frameIndex].callSite = pc - 1;
                Value result = invokeFunction(target, callArgs, receiver, depth + 1);
                if (unwinding_) {
                    int handlerSite = pc - 1;
                    if (!handleException(pendingException_, function, handlerSite, stack, locals)) {
                        return Value::voidValue();
                    }
                    unwinding_ = false;
                    pendingException_ = Value::null();
                    pc = handlerSite;
                    continue;
                }
                if (!target->returnType.isVoid()) push(result);
                break;
            }
            case OpCode::CallNative: {
                auto* target = function->nativeRefs[static_cast<std::size_t>(arg)];
                auto callArgs = popArguments(static_cast<int>(target->parameterTypes.size()));
                Value receiver = target->isStatic ? Value::null() : pop();
                frames_[frameIndex].callSite = pc - 1;
                Value result = invokeNativeFunction(target, receiver, callArgs);
                if (!target->returnType.isVoid()) push(result);
                break;
            }
            case OpCode::CallNativeVariadic: {
                auto* target = function->nativeRefs[static_cast<std::size_t>(arg)];
                int count = pop().asI32();
                auto callArgs = popArguments(count);
                Value receiver = target->isStatic ? Value::null() : pop();
                frames_[frameIndex].callSite = pc - 1;
                Value result = invokeNativeFunction(target, receiver, callArgs);
                if (!target->returnType.isVoid()) push(result);
                break;
            }

            case OpCode::NewDelegate: {
                const auto& info = function->delegateInfos[static_cast<std::size_t>(arg)];
                std::vector<Value> captures;
                captures.reserve(static_cast<std::size_t>(info.captureCount));
                for (int i = 0; i < info.captureCount; ++i) captures.push_back(pop());
                std::reverse(captures.begin(), captures.end());
                Value target = pop();
                auto delegate = engine_.garbageCollector().make<ScriptDelegate>(info.type);
                delegate->entries().push_back(
                    ScriptDelegate::Entry{target, info.function, std::move(captures)});
                delegate->setModuleOwner(function->module->shared_from_this());
                push(Value::delegateValue(delegate));
                break;
            }

            case OpCode::CallDelegate: {
                const int argc = arg;
                auto callArgs = popArguments(argc);
                Value callable = pop();
                auto delegate = callable.asDelegate();
                if (!delegate) throwRuntime("null reference (delegate invocation)", function, pc - 1);
                auto* delegateType = delegate->type();
                if (delegateType == nullptr) throwRuntime("invalid delegate value", function, pc - 1);

                Value result = Value::voidValue();
                bool jumpedToHandler = false;
                for (const auto& entry : delegate->entries()) {
                    auto entryArgs = callArgs;
                    Value value =
                        invokeFunction(entry.function, entryArgs, entry.target, depth + 1, &entry.captures);
                    if (unwinding_) {
                        int site = pc - 1;
                        if (!handleException(pendingException_, function, site, stack, locals)) {
                            return Value::voidValue();
                        }
                        unwinding_ = false;
                        pendingException_ = Value::null();
                        pc = site;
                        jumpedToHandler = true;
                        break;
                    }
                    result = value;
                }
                if (jumpedToHandler) continue;
                if (!delegateType->returnType.isVoid()) push(result);
                break;
            }

            case OpCode::DelegateAdd:
            case OpCode::DelegateSub: {
                Value right = pop();
                Value left = pop();
                if (left.isNull()) {
                    push(op == OpCode::DelegateAdd ? right : left);
                    break;
                }
                if (right.isNull()) {
                    push(left);
                    break;
                }
                auto leftDelegate = left.asDelegate();
                auto rightDelegate = right.asDelegate();
                if (!leftDelegate || !rightDelegate || leftDelegate->type() != rightDelegate->type()) {
                    throwRuntime("delegate operands must have the same delegate type", function, pc - 1);
                }
                if (op == OpCode::DelegateAdd) {
                    auto combined = engine_.garbageCollector().make<ScriptDelegate>(leftDelegate->type());
                    combined->entries() = leftDelegate->entries();
                    combined->entries().insert(combined->entries().end(), rightDelegate->entries().begin(),
                                               rightDelegate->entries().end());
                    combined->setModuleOwner(function->module->shared_from_this());
                    push(Value::delegateValue(combined));
                } else {
                    auto removed = engine_.garbageCollector().make<ScriptDelegate>(leftDelegate->type());
                    removed->entries() = leftDelegate->entries();
                    auto& entries = removed->entries();
                    const auto& rhs = rightDelegate->entries();
                    bool erased = false;
                    if (rhs.size() <= entries.size()) {
                        for (std::size_t start = entries.size() - rhs.size() + 1; start-- > 0;) {
                            bool matches = true;
                            for (std::size_t i = 0; i < rhs.size(); ++i) {
                                if (entries[start + i].function != rhs[i].function ||
                                    !ValueOps::areEqual(entries[start + i].target, rhs[i].target)) {
                                    matches = false;
                                    break;
                                }
                            }
                            if (matches) {
                                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(start),
                                              entries.begin() +
                                                  static_cast<std::ptrdiff_t>(start + rhs.size()));
                                erased = true;
                                break;
                            }
                        }
                    }
                    removed->setModuleOwner(function->module->shared_from_this());
                    push(erased || !entries.empty() ? Value::delegateValue(removed) : Value::null());
                }
                break;
            }

            case OpCode::NewObj: {
                const auto& type = function->typeRefs[static_cast<std::size_t>(arg)];
                auto* cls = dynamic_cast<ScriptClassType*>(type.object);
                if (cls == nullptr) throwRuntime("NewObj expects a script class or struct", function, pc - 1);
                if (cls->isStruct) {
                    push(makeDefaultStructValue(*cls, engine_.garbageCollector()));
                } else {
                    push(Value::instance(
                        engine_.garbageCollector().make<ScriptInstance>(cls, engine_.garbageCollector())));
                }
                break;
            }
            case OpCode::NewArray: {
                const auto& elementType = function->typeRefs[static_cast<std::size_t>(arg)];
                int count = pop().asI32();
                if (count < 0 || count > engine_.settings.maxArrayLength) {
                    throwRuntime("invalid array length " + std::to_string(count), function, pc - 1);
                }
                auto array = engine_.garbageCollector().make<ScriptArray>(
                    std::make_shared<ScriptType>(elementType), static_cast<std::size_t>(count));
                for (int i = 0; i < count; ++i) {
                    array->items().push_back(defaultValue(elementType, engine_.garbageCollector()));
                }
                push(Value::array(array));
                break;
            }
            case OpCode::CloneStruct: {
                Value value = pop();
                if (value.kind() == ValueKind::Struct) {
                    value = cloneStructValue(value, engine_.garbageCollector());
                }
                push(value);
                break;
            }
            case OpCode::Cast: {
                const auto& target = function->typeRefs[static_cast<std::size_t>(arg)];
                push(castValue(pop(), target));
                break;
            }
            case OpCode::IsType: {
                const auto& target = function->typeRefs[static_cast<std::size_t>(arg)];
                Value value = pop();
                push(Value::boolean(isTypeOf(value, target)));
                break;
            }

            case OpCode::Throw: {
                Value exception = pop();
                if (exception.isNull()) {
                    exception = engine_.createException("NullReferenceException", "throw null");
                }
                prepareException(exception, function, pc - 1);
                pendingException_ = exception;
                pendingFunction_ = function;
                pendingPc_ = pc - 1;
                unwinding_ = true;
                throw ScriptAbort{};
            }

            case OpCode::Leave: {
                const int site = pc - 1;
                const int target = arg;
                const int index = findFinallyBetween(function, site, target);
                if (index < 0) {
                    pc = target;
                    break;
                }
                const auto& handler = function->handlers[static_cast<std::size_t>(index)];
                if (stack.size() > static_cast<std::size_t>(handler.stackDepth)) {
                    stack.resize(static_cast<std::size_t>(handler.stackDepth));
                }
                if (actions_ == nullptr) throwRuntime("'leave' outside of a frame", function, site);
                PendingAction action;
                action.kind = PendingActionKind::Leave;
                action.targetPc = target;
                action.resumePc = handler.tryEnd;
                actions_->push_back(action);
                pc = handler.handlerPc;
                break;
            }

            case OpCode::EndFinally: {
                if (actions_ == nullptr || actions_->empty()) {
                    throwRuntime("'endfinally' without a pending control transfer", function, pc - 1);
                }
                PendingAction action = actions_->back();
                actions_->pop_back();
                if (action.kind == PendingActionKind::Return) return action.value;

                if (action.kind == PendingActionKind::Leave) {
                    const int next = findFinallyBetween(function, action.resumePc, action.targetPc);
                    if (next < 0) {
                        pc = action.targetPc;
                        break;
                    }
                    const auto& handler = function->handlers[static_cast<std::size_t>(next)];
                    action.resumePc = handler.tryEnd;
                    actions_->push_back(action);
                    if (stack.size() > static_cast<std::size_t>(handler.stackDepth)) {
                        stack.resize(static_cast<std::size_t>(handler.stackDepth));
                    }
                    pc = handler.handlerPc;
                    break;
                }

                // Exception path: keep unwinding after the finally completed.
                int resume = action.resumePc;
                if (handleException(action.value, function, resume, stack, locals)) {
                    pc = resume;
                    break;
                }
                pendingException_ = action.value;
                pendingFunction_ = function;
                pendingPc_ = resume;
                unwinding_ = true;
                return Value::voidValue();
            }

            case OpCode::Ret: {
                Value result = function->returnType.isVoid() ? Value::voidValue() : pop();
                returnValue_ = result;
                return result;
            }

            default:
                throwRuntime("unimplemented opcode", function, pc - 1);
        }
        } catch (const ScriptAbort&) {
            int site = pc - 1;
            if (handleException(pendingException_, function, site, stack, locals)) {
                unwinding_ = false;
                pendingException_ = Value::null();
                pc = site;
                continue;
            }
            // Keep the flag set: the caller checks it right after the call.
            return Value::voidValue();
        }
    }
}

Value ExecutionContext::invokeNativeFunction(NativeFunction* function, const Value& self,
                                             std::vector<Value>& arguments) {
    if (!function->invoke) throw std::runtime_error("native function has no implementation");

    std::vector<Value> converted(arguments.size());
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i < function->parameterTypes.size()) {
            converted[i] = convertArgument(arguments[i], function->parameterTypes[i], nullptr);
        } else {
            converted[i] = arguments[i];
        }
    }

    try {
        return function->invoke(*this, self, converted);
    } catch (const ScriptRuntimeException& ex) {
        // Host code can throw the C++ runtime exception directly; make it a
        // script exception as well so the surrounding script can catch it.
        if (!frames_.empty()) {
            auto* current = frames_.back().function;
            int site = frames_.back().callSite >= 0 ? frames_.back().callSite : 0;
            throwRuntime(ex.what(), current, site);
        }
        throw;
    } catch (const ScriptNullReferenceException& ex) {
        if (!frames_.empty()) {
            auto* current = frames_.back().function;
            int site = frames_.back().callSite >= 0 ? frames_.back().callSite : 0;
            throwRuntime(ex.what(), current, site);
        }
        throw;
    } catch (const std::exception& ex) {
        if (!frames_.empty()) {
            auto* current = frames_.back().function;
            int site = frames_.back().callSite >= 0 ? frames_.back().callSite : 0;
            throwRuntime("native function '" + function->name + "' failed: " + ex.what(), current, site);
        }
        throw;
    }
}

Value ExecutionContext::convertArgument(const Value& value, const ScriptType& target,
                                        ScriptFunction* function) {
    Value result;
    std::string error;
    if (tryConvert(value, target, result, error, engine_.garbageCollector())) return result;
    if (function != nullptr) throwRuntime(error, function, 0);
    ScriptFunction* current = frames_.empty() ? nullptr : frames_.back().function;
    if (current != nullptr) throwRuntime(error, current, frames_.back().callSite);
    throw ScriptRuntimeException(error, "", 0, "");
}

Value ExecutionContext::convertValue(const Value& value, const ScriptType& target,
                                     ScriptFunction* function, int pc) {
    Value result;
    std::string error;
    if (tryConvert(value, target, result, error, engine_.garbageCollector())) return result;
    throwRuntime(error, function, pc);
}

void ExecutionContext::throwRuntime(const std::string& message, ScriptFunction* function, int pc) {
    Value exception = engine_.createException(exceptionTypeNameFor(message), message);
    prepareException(exception, function, pc);
    pendingException_ = exception;
    pendingFunction_ = function;
    pendingPc_ = pc;
    unwinding_ = true;
    throw ScriptAbort{};
}

void ExecutionContext::throwException(const Value& exception) {
    if (frames_.empty()) {
        auto info = exception.asNative<ScriptExceptionInfo>();
        throw ScriptRuntimeException(info ? utf16ToUtf8(info->message) : exception.display(), "", 0,
                                     info ? utf16ToUtf8(info->stackTrace) : std::string());
    }
    ScriptFunction* current = frames_.back().function;
    int pc = frames_.back().callSite;
    if (pc < 0) pc = 0;
    Value value = exception;
    prepareException(value, current, pc);
    pendingException_ = value;
    pendingFunction_ = current;
    pendingPc_ = pc;
    unwinding_ = true;
    throw ScriptAbort{};
}

void ExecutionContext::prepareException(Value& exception, ScriptFunction* function, int pc) {
    if (exception.isNull()) {
        exception = engine_.createException("NullReferenceException", "throw null");
    }
    auto info = exception.asNative<ScriptExceptionInfo>();
    if (!info) {
        // Defensive: anything that is not an Exception value is wrapped.
        exception = engine_.createException("Exception", exception.display());
        info = exception.asNative<ScriptExceptionInfo>();
    }
    if (info->stackTrace.empty() && function != nullptr) {
        info->stackTrace = utf8ToUtf16(buildStackTrace(function, pc));
    }
}

bool ExecutionContext::handleException(const Value& exception, ScriptFunction* function, int& pc,
                                       std::vector<Value>& stack, std::vector<Value>& locals) {
    const auto& handlers = function->handlers;
    if (handlers.empty()) return false;
    if (pc < 0 || pc >= static_cast<int>(function->code.size())) return false;

    auto info = exception.asNative<ScriptExceptionInfo>();

    // Innermost clause wins; unmatched catch clauses are ignored so the search
    // falls through to an enclosing catch or to this region's finally.
    int selected = -1;
    for (std::size_t i = 0; i < handlers.size(); ++i) {
        const auto& handler = handlers[i];
        if (pc < handler.tryStart || pc >= handler.tryEnd) continue;
        if (handler.kind == HandlerKind::Catch && handler.hasCatchType) {
            auto* target = dynamic_cast<NativeObjectType*>(handler.catchType.object);
            auto* thrownType = info != nullptr ? info->type : nullptr;
            if (target == nullptr || thrownType == nullptr || !target->isAssignableFrom(thrownType)) {
                continue;
            }
        }
        if (selected < 0 || handler.tryStart > handlers[static_cast<std::size_t>(selected)].tryStart ||
            (handler.tryStart == handlers[static_cast<std::size_t>(selected)].tryStart &&
             handler.tryEnd < handlers[static_cast<std::size_t>(selected)].tryEnd)) {
            selected = static_cast<int>(i);
        }
    }

    // An exception escaping a finally body abandons the transfer it interrupted,
    // unless the exception is caught inside that same finally body.
    int escapedFinally = -1;
    insideFinallyBody(function, pc, escapedFinally);
    if (escapedFinally >= 0 && actions_ != nullptr && !actions_->empty()) {
        bool caughtInside = false;
        if (selected >= 0) {
            const auto& finallyHandler = handlers[static_cast<std::size_t>(escapedFinally)];
            const auto& chosen = handlers[static_cast<std::size_t>(selected)];
            caughtInside = chosen.kind == HandlerKind::Catch &&
                           chosen.tryStart >= finallyHandler.handlerPc &&
                           chosen.tryEnd <= finallyHandler.handlerEnd;
        }
        if (!caughtInside) actions_->pop_back();
    }

    if (selected < 0) return false;

    const auto& handler = handlers[static_cast<std::size_t>(selected)];
    if (stack.size() > static_cast<std::size_t>(handler.stackDepth)) {
        stack.resize(static_cast<std::size_t>(handler.stackDepth));
    }

    if (handler.kind == HandlerKind::Finally) {
        if (actions_ == nullptr) return false;
        PendingAction action;
        action.kind = PendingActionKind::Throw;
        action.value = exception;
        action.resumePc = handler.tryEnd;
        actions_->push_back(action);
        pc = handler.handlerPc;
        return true;
    }

    if (handler.exceptionSlot >= 0 && handler.exceptionSlot < static_cast<int>(locals.size())) {
        locals[static_cast<std::size_t>(handler.exceptionSlot)] = exception;
    }
    pc = handler.handlerPc;
    return true;
}

int ExecutionContext::findFinallyBetween(ScriptFunction* function, int fromPc, int targetPc) const {
    int best = -1;
    for (std::size_t i = 0; i < function->handlers.size(); ++i) {
        const auto& handler = function->handlers[i];
        if (handler.kind != HandlerKind::Finally) continue;
        if (fromPc < handler.tryStart || fromPc >= handler.tryEnd) continue;
        if (targetPc >= handler.tryStart && targetPc < handler.tryEnd) continue;
        if (best < 0 ||
            handler.tryStart > function->handlers[static_cast<std::size_t>(best)].tryStart ||
            (handler.tryStart == function->handlers[static_cast<std::size_t>(best)].tryStart &&
             handler.tryEnd < function->handlers[static_cast<std::size_t>(best)].tryEnd)) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool ExecutionContext::insideFinallyBody(ScriptFunction* function, int pc, int& handlerIndex) const {
    int best = -1;
    for (std::size_t i = 0; i < function->handlers.size(); ++i) {
        const auto& handler = function->handlers[i];
        if (handler.kind != HandlerKind::Finally) continue;
        if (pc < handler.handlerPc || pc >= handler.handlerEnd) continue;
        if (best < 0 || handler.handlerPc > function->handlers[static_cast<std::size_t>(best)].handlerPc) {
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return false;
    handlerIndex = best;
    return true;
}

std::string ExecutionContext::buildStackTrace(ScriptFunction* function, int pc) const {
    std::ostringstream out;
    out << "  at " << function->qualifiedName() << " ("
        << (function->file.empty() ? "<script>" : function->file) << ":" << function->lineAt(pc) << ")";

    for (int i = static_cast<int>(frames_.size()) - 1; i >= 0; --i) {
        const auto& frame = frames_[static_cast<std::size_t>(i)];
        if (frame.function == function) continue;
        out << "\n  at " << frame.function->qualifiedName() << " ("
            << (frame.function->file.empty() ? "<script>" : frame.function->file) << ":"
            << frame.function->lineAt(frame.callSite) << ")";
    }
    return out.str();
}

}  // namespace sharpscript
