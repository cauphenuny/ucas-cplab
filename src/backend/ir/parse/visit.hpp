#include "backend/ir/ir.hpp"
#include "utils/error.hpp"

#include <IRBaseVisitor.h>
#include <IRLexer.h>
#include <IRParser.h>
#include <any>
#include <cstdlib>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ir {
struct IRConstructVisitor : public IRBaseVisitor {
    IRConstructVisitor() = default;

    std::any visitProgram(IRParser::ProgramContext* ctx) override {
        for (auto g : ctx->globalDecl()) visitGlobalDecl(g);
        for (auto f : ctx->funcDecl()) visitFuncDecl(f);
        return std::any();
    }

    std::any visitGlobalDecl(IRParser::GlobalDeclContext* ctx) override {
        auto name = ctx->ID()->getText();
        auto type = parseType(ctx->type());
        std::optional<ConstexprValue> init;
        if (ctx->constexpr_()) init = parseConstexpr(ctx->constexpr_());
        auto alloc = std::make_unique<Alloc>(name, std::move(type), std::move(init));
        Alloc* alloc_ptr = alloc.get();
        program_.addGlobal(std::move(alloc));
        globals_[name] = alloc_ptr;
        return std::any();
    }

    std::any visitFuncDecl(IRParser::FuncDeclContext* ctx) override {
        auto name = ctx->ID()->getText();
        adt::TypeBox ret_type = ctx->type() ? parseType(ctx->type()) : adt::construct<void>();

        std::vector<std::unique_ptr<Alloc>> params;
        if (ctx->paramList()) {
            for (auto p : ctx->paramList()->param()) {
                auto pname = p->ID()->getText();
                auto ptype = parseType(p->type());
                params.push_back(std::make_unique<Alloc>(pname, ptype));
            }
        }

        auto func_ptr = std::make_unique<Func>(ret_type, name, std::move(params));
        Func* func_raw = func_ptr.get();
        program_.addFunc(std::move(func_ptr));
        funcs_[name] = func_raw;

        // set current function while visiting its inner nodes
        cur_func_ = func_raw;
        // locals
        for (auto ld : ctx->localDecl()) visitLocalDecl(ld);
        // blocks
        for (auto b : ctx->block()) visitBlock(b);

        cur_func_ = nullptr;
        cur_block_ = nullptr;
        blocks_by_label_.clear();
        return std::any();
    }

    std::any visitLocalDecl(IRParser::LocalDeclContext* ctx) override {
        if (!cur_func_) throw SemanticError(Location{}, "local declaration outside function");
        auto name = ctx->ID()->getText();
        auto type = parseType(ctx->type());
        std::optional<ConstexprValue> init;
        if (ctx->constexpr_()) init = parseConstexpr(ctx->constexpr_());
        auto alloc = std::make_unique<Alloc>(name, type, std::move(init));
        Alloc* alloc_raw = alloc.get();
        cur_func_->addLocal(std::move(alloc));
        locals_[name] = alloc_raw;
        return std::any();
    }

    std::any visitBlock(IRParser::BlockContext* ctx) override {
        if (!cur_func_) throw SemanticError(Location{}, "block outside function");
        auto label = ctx->label()->getText();
        Block* blk = ensureBlock(label);
        cur_block_ = blk;
        for (auto inst : ctx->inst()) visitInst(inst);
        if (ctx->exit()) {
            visitExit(ctx->exit());
        }
        cur_block_ = nullptr;
        return std::any();
    }

    std::any visitInst(IRParser::InstContext* ctx) override {
        if (!cur_block_) throw SemanticError(Location{}, "instruction outside block");

        // store: var '[' value ']' '=' value ';' (no type)
        if (!ctx->type()) {
            auto varctx = ctx->var(0);
            auto indexVal = parseValue(ctx->value(0));
            auto rhs = parseValue(ctx->value(1));
            LeftValue target = parseLeftValue(varctx, adt::TypeBox());
            BinaryInst inst{InstOp::STORE, std::move(target), std::move(indexVal), std::move(rhs)};
            cur_block_->add(std::move(inst));
            return std::any();
        }

        // typed instructions (assignment/call/load/binop)
        if (ctx->ID()) {
            // call: var ':' type '=' ID '(' (argList)? ')'
            auto lhsVar = ctx->var(0);
            auto resultType = parseType(ctx->type());
            LeftValue result = parseLeftValue(lhsVar, resultType);
            std::string funcName = ctx->ID()->getText();
            NamedValue funcNamed;
            if (funcs_.count(funcName)) {
                funcNamed.def = funcs_[funcName];
                funcNamed.type = funcs_[funcName]->ret_type;
            } else {
                throw SemanticError(Location{}, fmt::format("unknown function '{}'", funcName));
            }
            std::vector<Value> args;
            if (ctx->argList()) {
                for (auto v : ctx->argList()->value()) args.push_back(parseValue(v));
            }
            CallInst call{std::move(result), funcNamed, std::move(args)};
            cur_block_->add(std::move(call));
            return std::any();
        }

        // other typed forms
        auto lhsVar = ctx->var(0);
        auto resultType = parseType(ctx->type());
        LeftValue result = parseLeftValue(lhsVar, resultType);

        if (ctx->binop()) {
            auto lhsVal = parseValue(ctx->value(0));
            auto rhsVal = parseValue(ctx->value(1));
            InstOp op = parseBinop(ctx->binop()->getText());
            BinaryInst bin{op, std::move(result), std::move(lhsVal), std::move(rhsVal)};
            cur_block_->add(std::move(bin));
            return std::any();
        }

        // load: value '[' value ']'
        if (ctx->value().size() == 2) {
            auto baseVal = parseValue(ctx->value(0));
            auto idxVal = parseValue(ctx->value(1));
            BinaryInst load{InstOp::LOAD, std::move(result), std::move(baseVal), std::move(idxVal)};
            cur_block_->add(std::move(load));
            return std::any();
        }

        // unary or simple move
        auto rhsVal = parseValue(ctx->value(0));
        // crude operator detection from text
        auto text = ctx->getText();
        auto eqpos = text.find('=');
        UnaryInstOp uop = UnaryInstOp::MOV;
        if (eqpos != std::string::npos && eqpos + 1 < text.size()) {
            char c = text[eqpos + 1];
            if (c == '!')
                uop = UnaryInstOp::NOT;
            else if (c == '-')
                uop = UnaryInstOp::NEG;
        }
        UnaryInst uni{uop, std::move(result), std::move(rhsVal)};
        cur_block_->add(std::move(uni));
        return std::any();
    }

    std::any visitExit(IRParser::ExitContext* ctx) override {
        if (ctx->RETURN()) {
            if (ctx->value()) {
                Value v = parseValue(ctx->value());
                cur_block_->setExit(ReturnExit{std::move(v)});
            } else {
                cur_block_->setExit(ReturnExit{ConstexprValue{}});
            }
        } else if (ctx->BRANCH()) {
            Value cond = parseValue(ctx->value());
            auto tlabel = ctx->label(0)->getText();
            auto flabel = ctx->label(1)->getText();
            Block* tblk = ensureBlock(tlabel);
            Block* fblk = ensureBlock(flabel);
            cur_block_->setExit(BranchExit{std::move(cond), tblk, fblk});
        } else if (ctx->JUMP()) {
            auto tlabel = ctx->label(0)->getText();
            Block* tblk = ensureBlock(tlabel);
            cur_block_->setExit(JumpExit{tblk});
        }
        return std::any();
    }

    std::any visitValue(IRParser::ValueContext* ctx) override {
        return std::any();
    }
    std::any visitType(IRParser::TypeContext* ctx) override {
        return std::any();
    }
    std::any visitConstexpr(IRParser::ConstexprContext* ctx) override {
        return std::any();
    }

    ir::Program takeProgram() && {
        return std::move(program_);
    }

private:
    ir::Program program_;
    Func* cur_func_{nullptr};
    Block* cur_block_{nullptr};

    std::unordered_map<std::string, Alloc*> globals_;
    std::unordered_map<std::string, Func*> funcs_;
    std::unordered_map<std::string, Alloc*> locals_;
    std::unordered_map<std::string, Block*> blocks_by_label_;

    Block* ensureBlock(const std::string& label) {
        if (blocks_by_label_.count(label)) return blocks_by_label_[label];
        auto b = cur_func_->newBlock(label);
        blocks_by_label_[label] = b;
        return b;
    }

    adt::TypeBox parseType(IRParser::TypeContext* ctx) {
        if (!ctx) throw SemanticError(Location{}, "missing type");
        bool is_const = ctx->CONST() != nullptr;
        if (ctx->INT()) {
            adt::Int t;
            t.immutable = is_const;
            return std::move(t).toBoxed();
        }
        if (ctx->FLOAT()) {
            adt::Float t;
            t.immutable = is_const;
            return std::move(t).toBoxed();
        }
        if (ctx->DOUBLE()) {
            adt::Double t;
            t.immutable = is_const;
            return std::move(t).toBoxed();
        }
        if (ctx->BOOL()) {
            adt::Bool t;
            t.immutable = is_const;
            return std::move(t).toBoxed();
        }

        // array: '[' type ';' INT_LITERAL ']'
        if (ctx->INT_LITERAL()) {
            auto elem = parseType(ctx->type(0));
            size_t size = static_cast<size_t>(std::stoul(ctx->INT_LITERAL()->getText()));
            adt::Array a(elem, size);
            a.immutable = is_const;
            return std::move(a).toBoxed();
        }

        // pointer: '&' '[' type ']'
        auto text = ctx->getText();
        if (!text.empty() && text[0] == '&') {
            auto elem = parseType(ctx->type(0));
            adt::Pointer p(elem);
            p.immutable = is_const;
            return std::move(p).toBoxed();
        }

        // sum vs product
        if (text.find('|') != std::string::npos) {
            std::vector<adt::TypeBox> items;
            for (size_t i = 0; i < ctx->type().size(); ++i)
                items.push_back(parseType(ctx->type(i)));
            adt::Sum s(std::move(items));
            s.immutable = is_const;
            return std::move(s).toBoxed();
        }

        // product
        adt::Product p;
        for (auto tctx : ctx->type()) p.append(parseType(tctx));
        p.immutable = is_const;
        return std::move(p).toBoxed();
    }

    Value parseValue(IRParser::ValueContext* ctx) {
        if (ctx->var()) {
            auto v = ctx->var();
            if (v->temp()) {
                size_t id = static_cast<size_t>(std::stoul(v->temp()->INT_LITERAL()->getText()));
                if (!cur_func_) throw SemanticError(Location{}, "temp used outside function");
                const auto& temps = cur_func_->temps();
                if (id >= temps.size())
                    throw SemanticError(Location{}, fmt::format("temp ${} not defined", id));
                return LeftValue(TempValue{.type = temps[id], .id = id});
            } else {
                auto name = v->ID()->getText();
                // look in params
                if (cur_func_) {
                    for (const auto& p : cur_func_->params) {
                        if (p->name == name) return LeftValue(NamedValue{p->type, p.get()});
                    }
                    for (const auto& l : cur_func_->locals()) {
                        if (l->name == name) return LeftValue(NamedValue{l->type, l.get()});
                    }
                }
                if (globals_.count(name))
                    return LeftValue(NamedValue{globals_[name]->type, globals_[name]});
                throw SemanticError(Location{}, fmt::format("unknown variable '{}'", name));
            }
        }
        // constexpr
        // here we return a ConstexprValue wrapped as Value
        auto c = parseConstexpr(ctx->constexpr_());
        return Value(std::move(c));
    }

    ConstexprValue parseConstexpr(IRParser::ConstexprContext* ctx) {
        auto txt = ctx->getText();
        if (txt == "true") return ConstexprValue(true);
        if (txt == "false") return ConstexprValue(false);
        // array literal
        if (!ctx->constexpr_().empty()) {
            throw SemanticError(Location{}, "array constexpr not supported yet");
        }
        // numeric
        if (txt.find_first_of(".eE") != std::string::npos) {
            if (!txt.empty() && (txt.back() == 'f' || txt.back() == 'F')) {
                float v = std::strtof(txt.c_str(), nullptr);
                return ConstexprValue(v);
            } else {
                double v = std::strtod(txt.c_str(), nullptr);
                return ConstexprValue(v);
            }
        } else {
            int v = std::stoi(txt);
            return ConstexprValue(v);
        }
    }

    LeftValue parseLeftValue(IRParser::VarContext* vctx, const adt::TypeBox& declaredType) {
        if (vctx->temp()) {
            size_t id = static_cast<size_t>(std::stoul(vctx->temp()->INT_LITERAL()->getText()));
            if (!cur_func_) throw SemanticError(Location{}, "temp declared outside function");
            // if this is a new temp (append), allocate
            if (id == cur_func_->temps().size()) {
                return cur_func_->newTemp(declaredType);
            }
            const auto& temps = cur_func_->temps();
            if (id < temps.size()) return TempValue{.type = temps[id], .id = id};
            throw SemanticError(Location{}, fmt::format("invalid temp id ${}", id));
        } else {
            auto name = vctx->ID()->getText();
            // search params
            if (cur_func_) {
                for (const auto& p : cur_func_->params)
                    if (p->name == name) return NamedValue{p->type, p.get()};
                for (const auto& l : cur_func_->locals())
                    if (l->name == name) return NamedValue{l->type, l.get()};
            }
            if (globals_.count(name)) return NamedValue{globals_[name]->type, globals_[name]};
            throw SemanticError(Location{}, fmt::format("unknown name '{}'", name));
        }
    }

    InstOp parseBinop(const std::string& s) {
        if (s == "*") return InstOp::MUL;
        if (s == "/") return InstOp::DIV;
        if (s == "%") return InstOp::MOD;
        if (s == "+") return InstOp::ADD;
        if (s == "-") return InstOp::SUB;
        if (s == "<=") return InstOp::LEQ;
        if (s == ">=") return InstOp::GEQ;
        if (s == "<") return InstOp::LT;
        if (s == ">") return InstOp::GT;
        if (s == "==") return InstOp::EQ;
        if (s == "!=") return InstOp::NEQ;
        if (s == "&&") return InstOp::AND;
        if (s == "||") return InstOp::OR;
        throw SemanticError(Location{}, fmt::format("unknown binary operator '{}'", s));
    }
};

auto parse(std::istream& input) -> ir::Program {
    using namespace antlr4;
    ANTLRInputStream input_stream(input);
    IRLexer lexer(&input_stream);
    CommonTokenStream tokens(&lexer);
    IRParser parser(&tokens);
    auto tree = parser.program();
    IRConstructVisitor visitor;
    visitor.visit(tree);
    return std::move(visitor).takeProgram();
}

}  // namespace ir