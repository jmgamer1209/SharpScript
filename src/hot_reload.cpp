#include "sharpscript/hot_reload.hpp"

namespace sharpscript {

void HotReloadManager::captureInitialValues(ScriptModule& module) {
    module.initialGlobalValues.clear();
    for (const auto& variable : module.globals) {
        module.initialGlobalValues[variable.name] = cloneValue(variable.value, 0);
    }
}

MigrationReport HotReloadManager::migrate(const ScriptModule& previous, ScriptModule& current) {
    MigrationReport report;
    for (auto& variable : current.globals) {
        auto oldIt = previous.globalsByName.find(variable.name);
        if (oldIt != previous.globalsByName.end() &&
            isCompatible(previous.globals[oldIt->second].type, variable.type)) {
            const auto& oldValue = previous.globals[oldIt->second].value;
            Value initial = defaultValue(previous.globals[oldIt->second].type, engine_.garbageCollector());
            auto initialIt = previous.initialGlobalValues.find(variable.name);
            if (initialIt != previous.initialGlobalValues.end()) initial = initialIt->second;
            variable.value = merge(oldValue, initial, variable.value, variable.type, 0);
            report.migrated.push_back(variable.name);
        } else {
            report.reset.push_back(variable.name);
        }
    }
    return report;
}

bool HotReloadManager::isCompatible(const ScriptType& oldType, const ScriptType& newType) const {
    // A class and a struct with the same name are different kinds of state.
    if (oldType.isStruct() != newType.isStruct()) return false;
    if (oldType.sameAs(newType)) return true;
    if (oldType.object != nullptr && newType.object != nullptr) {
        return oldType.object->name == newType.object->name;
    }
    return false;
}

Value HotReloadManager::merge(const Value& current, const Value& initial, const Value& fresh,
                              const ScriptType& targetType, int depth) {
    if (depth > 24) return current;
    if (deepEquals(current, initial, depth)) return migrateFresh(fresh, targetType, depth);

    auto& collector = engine_.garbageCollector();
    if (auto* newClass = dynamic_cast<ScriptClassType*>(targetType.object)) {
        if (newClass->isStruct) {
            auto oldStruct = current.asStruct();
            if (!oldStruct || oldStruct->type() == nullptr || oldStruct->type()->name != newClass->name) {
                return migrateFresh(fresh, targetType, depth);
            }
            auto oldInitial = initial.asStruct();
            auto freshStruct = fresh.asStruct();
            Value resultValue = makeDefaultStructValue(*newClass, collector);
            auto result = resultValue.asStruct();
            auto* oldType = oldStruct->type();
            for (std::size_t i = 0; i < newClass->fields.size(); ++i) {
                const auto& newField = newClass->fields[i];
                auto* oldField = oldType->findField(newField.name);
                if (oldField == nullptr) {
                    result->fields()[i] =
                        freshStruct && i < freshStruct->fields().size()
                            ? migrateFresh(freshStruct->fields()[i], newField.type, depth + 1)
                            : defaultValue(newField.type, collector);
                    continue;
                }
                const Value& oldValue = oldStruct->fields()[oldField->index];
                Value initialValue = defaultValue(newField.type, collector);
                if (oldInitial) {
                    auto* initialField = oldInitial->type()->findField(newField.name);
                    if (initialField != nullptr) initialValue = oldInitial->fields()[initialField->index];
                }
                Value freshValue = freshStruct && i < freshStruct->fields().size()
                                       ? freshStruct->fields()[i]
                                       : defaultValue(newField.type, collector);
                result->fields()[i] = merge(oldValue, initialValue, freshValue, newField.type, depth + 1);
            }
            return resultValue;
        }

        auto oldInstance = current.asInstance();
        if (!oldInstance || oldInstance->cls()->name != newClass->name) {
            return migrateFresh(fresh, targetType, depth);
        }

        auto oldInitial = initial.asInstance();
        auto freshInstance = fresh.asInstance();
        auto result = collector.make<ScriptInstance>(newClass, collector);

        for (std::size_t i = 0; i < newClass->fields.size(); ++i) {
            const auto& newField = newClass->fields[i];
            auto* oldField = oldInstance->cls()->findField(newField.name);
            if (oldField == nullptr) {
                result->fields()[i] =
                    freshInstance && i < freshInstance->fields().size()
                        ? migrateFresh(freshInstance->fields()[i], newField.type, depth + 1)
                        : defaultValue(newField.type, collector);
                continue;
            }

            const Value& oldValue = oldInstance->fields()[oldField->index];
            Value initialValue = defaultValue(newField.type, collector);
            if (oldInitial) {
                auto* initialField = oldInitial->cls()->findField(newField.name);
                if (initialField != nullptr) initialValue = oldInitial->fields()[initialField->index];
            }
            Value freshValue = freshInstance && i < freshInstance->fields().size()
                                   ? freshInstance->fields()[i]
                                   : defaultValue(newField.type, collector);
            result->fields()[i] = merge(oldValue, initialValue, freshValue, newField.type, depth + 1);
        }
        return Value::instance(result);
    }

    if (targetType.isArray()) {
        auto oldArray = current.asArray();
        if (!oldArray) return migrateFresh(fresh, targetType, depth);
        auto initialArray = initial.asArray();
        auto freshArray = fresh.asArray();
        auto elementType = *targetType.element;
        auto result = collector.make<ScriptArray>(std::make_shared<ScriptType>(elementType));
        for (std::size_t i = 0; i < oldArray->items().size(); ++i) {
            const Value& oldItem = oldArray->items()[i];
            Value initialItem = initialArray && i < initialArray->items().size()
                                    ? initialArray->items()[i]
                                    : defaultValue(elementType, collector);
            Value freshItem = freshArray && i < freshArray->items().size()
                                  ? freshArray->items()[i]
                                  : defaultValue(elementType, collector);
            result->items().push_back(merge(oldItem, initialItem, freshItem, elementType, depth + 1));
        }
        return Value::array(result);
    }

    return current;
}

Value HotReloadManager::migrateFresh(const Value& fresh, const ScriptType& targetType, int depth) {
    if (depth > 24 || fresh.isNull()) return fresh;

    auto& collector = engine_.garbageCollector();
    if (auto* newClass = dynamic_cast<ScriptClassType*>(targetType.object)) {
        if (newClass->isStruct) {
            auto source = fresh.asStruct();
            if (!source) return fresh;
            if (source->type() == newClass) return fresh;
            Value resultValue = makeDefaultStructValue(*newClass, collector);
            auto result = resultValue.asStruct();
            for (std::size_t i = 0; i < newClass->fields.size(); ++i) {
                const auto& field = newClass->fields[i];
                auto* oldField = source->type() != nullptr ? source->type()->findField(field.name) : nullptr;
                result->fields()[i] =
                    oldField != nullptr
                        ? migrateFresh(source->fields()[oldField->index], field.type, depth + 1)
                        : defaultValue(field.type, collector);
            }
            return resultValue;
        }

        auto instance = fresh.asInstance();
        if (!instance) return fresh;
        if (instance->cls() == newClass) return fresh;

        auto result = collector.make<ScriptInstance>(newClass, collector);
        for (std::size_t i = 0; i < newClass->fields.size(); ++i) {
            const auto& field = newClass->fields[i];
            auto* oldField = instance->cls()->findField(field.name);
            result->fields()[i] = oldField != nullptr
                                      ? migrateFresh(instance->fields()[oldField->index], field.type, depth + 1)
                                      : defaultValue(field.type, collector);
        }
        return Value::instance(result);
    }

    if (targetType.isArray()) {
        auto array = fresh.asArray();
        if (!array) return fresh;
        auto elementType = *targetType.element;
        auto result = collector.make<ScriptArray>(std::make_shared<ScriptType>(elementType));
        for (const auto& item : array->items()) {
            result->items().push_back(migrateFresh(item, elementType, depth + 1));
        }
        return Value::array(result);
    }

    return fresh;
}

bool HotReloadManager::deepEquals(const Value& a, const Value& b, int depth) const {
    if (depth > 24) return true;
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    if (a.kind() == ValueKind::String || b.kind() == ValueKind::String) {
        return a.kind() == ValueKind::String && b.kind() == ValueKind::String &&
               a.asString() == b.asString();
    }

    auto leftInstance = a.asInstance();
    auto rightInstance = b.asInstance();
    if (leftInstance && rightInstance) {
        if (leftInstance->cls()->name != rightInstance->cls()->name) return false;
        if (leftInstance->fields().size() != rightInstance->fields().size()) return false;
        for (std::size_t i = 0; i < leftInstance->fields().size(); ++i) {
            if (!deepEquals(leftInstance->fields()[i], rightInstance->fields()[i], depth + 1)) return false;
        }
        return true;
    }

    auto leftStruct = a.asStruct();
    auto rightStruct = b.asStruct();
    if (leftStruct && rightStruct) {
        if (leftStruct->type() == nullptr || rightStruct->type() == nullptr) return false;
        if (leftStruct->type()->name != rightStruct->type()->name) return false;
        if (leftStruct->fields().size() != rightStruct->fields().size()) return false;
        for (std::size_t i = 0; i < leftStruct->fields().size(); ++i) {
            if (!deepEquals(leftStruct->fields()[i], rightStruct->fields()[i], depth + 1)) return false;
        }
        return true;
    }

    auto leftArray = a.asArray();
    auto rightArray = b.asArray();
    if (leftArray && rightArray) {
        if (leftArray->items().size() != rightArray->items().size()) return false;
        for (std::size_t i = 0; i < leftArray->items().size(); ++i) {
            if (!deepEquals(leftArray->items()[i], rightArray->items()[i], depth + 1)) return false;
        }
        return true;
    }

    if (a.kind() == ValueKind::Native || b.kind() == ValueKind::Native) {
        return a.rawRef() == b.rawRef();
    }
    return ValueOps::areEqual(a, b);
}

Value HotReloadManager::cloneValue(const Value& value, int depth) const {
    if (depth > 24 || value.isNull()) return value;

    auto& collector = engine_.garbageCollector();
    if (auto instance = value.asInstance()) {
        auto clone = collector.make<ScriptInstance>(instance->cls(), collector);
        for (std::size_t i = 0; i < instance->fields().size(); ++i) {
            clone->fields()[i] = cloneValue(instance->fields()[i], depth + 1);
        }
        return Value::instance(clone);
    }

    if (auto structValue = value.asStruct()) {
        auto* type = structValue->type();
        if (type == nullptr) return value;
        Value cloned = makeDefaultStructValue(*type, collector);
        auto clone = cloned.asStruct();
        for (std::size_t i = 0; i < structValue->fields().size(); ++i) {
            clone->fields()[i] = cloneValue(structValue->fields()[i], depth + 1);
        }
        return cloned;
    }

    if (auto array = value.asArray()) {
        auto clone = collector.make<ScriptArray>(array->elementType());
        for (const auto& item : array->items()) {
            clone->items().push_back(cloneValue(item, depth + 1));
        }
        return Value::array(clone);
    }

    return value;
}

}  // namespace sharpscript
