#include "semantic_ast.h"
#include "utils/error.hpp"

namespace ast {

void SemanticAST::analysis(const CompUnit* comp_unit) {
    pushScope();  // builtin
    for (auto& func : builtin_funcs) {
        analysis(&func, true);
    }
    pushScope();  // global

    for (const auto& item : comp_unit->items) {
        match(item, [&](const auto& subitem) { analysis(&subitem); });
    }

    if (funcs.back().count("main") == 0) {
        throw SemanticError(comp_unit->loc, "function `main` is not defined");
    }
    auto main = funcs.back()["main"];
    checkType(main, adt::construct<int()>());
}

void SemanticAST::analysis(const Decl* decl) {
    bool is_const = std::holds_alternative<ConstDecl>(*decl);
    match(*decl, [&](const auto& decl) {
        auto elem_type = calcType(decl.type, is_const);
        for (const auto& def : decl.defs) {
            registerSymbol(&def);
            if (def.dims.size()) {
                // array type
                auto elem_type = calcType(decl.type, is_const);
                for (size_t i = def.dims.size(); i > 0; i--) {
                    elem_type = adt::Array(std::move(elem_type), def.dims[i - 1].value()).toBoxed();
                }
                types[&def] = std::move(elem_type);
            } else {
                types[&def] = elem_type;
            }
        }
    });
    match(*decl, [&](const auto& decl) { analysis(&decl); });
}

void SemanticAST::analysis(const ConstDecl* decl) {
    for (const auto& def : decl->defs) {
        Type val_type;
        analysis(&def.val);
        val_type = types[&def.val];
        if (!constructable(val_type, types[&def])) {
            throw SemanticError(def.loc,
                                fmt::format("type error: cannot initialize `{}` with `{}`",
                                            types[&def], val_type));
        }
    }
}

void SemanticAST::analysis(const VarDecl* decl) {
    for (const auto& def : decl->defs) {
        Type val_type;
        if (def.val.has_value()) {
            analysis(&*def.val);
            val_type = types[&*def.val];
        } else {
            val_type = NEVER;
        }
        if (!constructable(val_type, types[&def])) {
            throw SemanticError(def.loc,
                                fmt::format("type error: cannot initialize `{}` with `{}`",
                                            types[&def], val_type));
        }
    }
}

void SemanticAST::analysis(const ConstInitVal* val) {
    match(
        val->val,
        [&](const ConstExp& exp) {
            analysis(&exp);
            types[val] = types[&exp];
        },
        [&](const std::vector<ConstInitVal>& vals) {
            Type elem_type = NEVER;
            for (const auto& val : vals) {
                analysis(&val);
                if (!adt::constructable(elem_type, types[&val]) && !adt::constructable(types[&val], elem_type)) {
                    throw SemanticError(val.loc,
                                        fmt::format("type error: array elements have incompatible types `{}` and `{}`",
                                                    elem_type, types[&val]));
                }
                elem_type = adt::constructable(elem_type, types[&val]) ? types[&val] : elem_type;
            }
            types[val] = adt::Array(std::move(elem_type), vals.size()).toBoxed();
        });
}

}  // namespace ast
