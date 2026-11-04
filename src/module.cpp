#include "sharpscript/module.hpp"

namespace sharpscript {

std::string ScriptFunction::signature() const {
    std::ostringstream out;
    out << returnType.name << " " << qualifiedName() << "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i > 0) out << ", ";
        out << parameters[i].type.name << " " << parameters[i].name;
    }
    out << ")";
    return out.str();
}

}  // namespace sharpscript
