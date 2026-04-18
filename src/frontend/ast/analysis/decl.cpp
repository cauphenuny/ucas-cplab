#include "backend/ir/type.hpp"
#include "frontend/ast/ast.hpp"
#include "semantic_ast.h"
#include "utils/diagnosis.hpp"
#include "utils/match.hpp"
#include "utils/traits.hpp"
#include "utils/tui.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ast {

void SemanticAST::analysis(const CompUnit* comp_unit) {
    pushScope();  // builtin
    for (auto& func : BUILTIN_FUNCS) {
        analysis(&func, true);
    }
    pushScope();  // global

    for (const auto& item : comp_unit->items) {
        match(item, [&](const auto& subitem) { analysis(&subitem); });
    }

    if (funcs.count("main") == 0) {
        throw SemanticError(comp_unit->loc, "function `main` is not defined");
    }
    auto [main, _] = funcs["main"];
    auto expected_type = ir::type::construct<int()>();
    if (!(types[main] == expected_type)) {
        throw SemanticError(main->loc,
                            fmt::format("function `main` must have type `{}`, but got `{}`",
                                        expected_type, types[main]));
    }
}

void SemanticAST::analysis(const Decl* decl) {
    match(*decl, [&](const auto& decl) {
        auto elem_type = calcType(decl.type);
        for (const auto& def : decl.defs) {
            registerVariable(&def);
            if (def.dims.size()) {
                // array type
                auto elem_type = calcType(decl.type);
                for (size_t i = def.dims.size(); i > 0; i--) {
                    elem_type =
                        ir::type::Array(std::move(elem_type), def.dims[i - 1].value()).toBoxed();
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
                                fmt::format("type error: cannot initialize `{}` with `{}`" DIM
                                            " (at {})" NONE,
                                            types[&def], val_type, def));
        }
        this->readonly_defs.insert(&def);
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
                                fmt::format("type error: cannot initialize `{}` with `{}`" DIM
                                            " (at {})" NONE,
                                            types[&def], val_type, def));
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
                if (!ir::type::constructable(elem_type, types[&val]) &&
                    !ir::type::constructable(types[&val], elem_type)) {
                    throw SemanticError(
                        val.loc,
                        fmt::format(
                            "type error: array elements have incompatible types `{}` and `{}`" DIM
                            " (at {})" NONE,
                            elem_type, types[&val], val));
                }
                elem_type =
                    ir::type::constructable(elem_type, types[&val]) ? types[&val] : elem_type;
            }
            types[val] = ir::type::Array(std::move(elem_type), vals.size()).toBoxed();
        });
}

}  // namespace ast
