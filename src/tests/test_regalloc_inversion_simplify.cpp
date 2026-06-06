/// @brief Find minimal ABI + function for inverted register allocation.
///
/// Inversion: callee-saved backup temps get caller-saved colors while program
/// variables get callee-saved colors. Occurs when simultaneously live values
/// exceed caller-saved register budget.

#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/transform/optim/dead_alloc.hpp"
#include "backend/ir/transform/ssa/construct.hpp"
#include "backend/ir/transform/ssa/destruct.hpp"
#include "backend/rv64/abi.hpp"

#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ir::lowering;
using namespace ir::transform;

// ---------------------------------------------------------------------------
// ABI builder
// ---------------------------------------------------------------------------
struct ABIDesc {
    const char* name;
    int total, n_caller, n_callee, n_params;
};

static TargetABI make_abi(const ABIDesc& d) {
    std::set<size_t> caller, callee;
    for (int i = 0; i < d.n_caller; i++) caller.insert(i);
    for (int i = d.n_caller; i < d.total; i++) callee.insert(i);
    std::vector<size_t> params;
    for (int i = 0; i < d.n_params; i++) params.push_back(i);
    RegisterABI regs = {
        .size = (size_t)d.total,
        .caller_saved = caller,
        .callee_saved = callee,
        .reserved = {},
        .parameters = params,
        .return_value = 0,
        .name = [d](size_t idx) -> std::string {
            if (idx < (size_t)d.n_caller) return fmt::format("t{}", idx);
            return fmt::format("s{}", idx - d.n_caller);
        },
    };
    RegisterABI floats = regs;
    floats.name = [](size_t) { return std::string("f"); };
    return {
        .reg = {.generals = regs, .floats = floats, .return_addr = (size_t)d.total - 1},
        .mem = rv64::ABI.mem,
    };
}

// ---------------------------------------------------------------------------
// IR generation
// ---------------------------------------------------------------------------
static std::string gen_ir(int n_vars, int n_params) {
    std::string ir = "fn f(";
    for (int i = 0; i < n_params; i++) ir += (i ? ", " : "") + std::string(1, 'a' + i) + ": i32";
    ir += ") -> i32 {\n";
    for (int i = 1; i <= n_vars; i++) ir += "    let mut v" + std::to_string(i) + ": i32;\n";
    ir += "    'entry: {\n";
    for (int i = 1; i <= n_params && i <= n_vars; i++)
        ir +=
            "        @v" + std::to_string(i) + ": i32 = @" + std::string(1, 'a' + (i - 1)) + ";\n";
    for (int i = n_params + 1; i <= n_vars; i++)
        ir += "        @v" + std::to_string(i) + ": i32 = 0;\n";
    ir += "        => 'cond;\n"
          "    }\n"
          "    'cond: {\n"
          "        %0: bool = @v1 > 0;\n"
          "        => if %0 { 'loop } else { 'exit };\n"
          "    }\n"
          "    'loop: {\n";
    for (int i = 1; i <= n_vars; i++)
        ir += "        @v" + std::to_string(i) + ": i32 = @v" + std::to_string(i) + " + 1;\n";
    ir += "        => 'cond;\n"
          "    }\n"
          "    'exit: { return @v1; }\n}\n";
    return ir;
}

// ---------------------------------------------------------------------------
// Run pipeline + detect inversion
// ---------------------------------------------------------------------------
static bool find_inversion(const std::string& ir_str, const TargetABI& abi) {
    auto stream = std::istringstream(ir_str);
    auto prog = ir::parse(stream);
    ConstructSSA().apply(*prog);
    NonSSAPassContext ctx(*prog);
    DestructSSA().apply(*prog, ctx);
    RegisterAllocation regalloc(abi, false);
    regalloc.apply(*prog, ctx);
    RegisterReplacement<NonSSAPassContext>(regalloc.colored, regalloc.precolored).apply(*prog, ctx);
    RedundantMoveElimination<NonSSAPassContext>().apply(*prog, ctx);

    std::string out = fmt::format("{}", *prog);
    bool has_t_save = false, has_s_use = false;
    std::istringstream sin(out);
    std::string ln;
    while (std::getline(sin, ln)) {
        // t_save: @__reg_tN: type = @__reg_sM  (backup in caller-saved)
        if (ln.find("@__reg_t") != std::string::npos &&
            ln.find("= @__reg_s") != std::string::npos && ln.find('+') == std::string::npos &&
            ln.find('*') == std::string::npos)
            has_t_save = true;
        // s_use: @__reg_sN: type = ... where RHS is not a __reg_t restore
        // and not a spill/mem access. This catches @__reg_sN = @__reg_tM (param copy)
        // and @__reg_sN = 0 (constant) etc.
        size_t sp = ln.find("@__reg_s");
        if (sp != std::string::npos) {
            auto eq = ln.find('=');
            if (eq != std::string::npos && eq > sp) {
                auto lhs = ln.substr(0, eq);
                auto rhs = ln.substr(eq + 1);
                bool lhs_is_s = lhs.find("@__reg_s") != std::string::npos;
                if (lhs_is_s && rhs.find("@__reg_t") == std::string::npos &&
                    rhs.find("spill") == std::string::npos && rhs.find("*") == std::string::npos &&
                    rhs.find("<-") == std::string::npos)
                    has_s_use = true;
            }
        }
    }
    return has_t_save && has_s_use;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::vector<ABIDesc> configs = {
        // name                        tot  caller callee params
        {"4-reg(t1,t0,s0,s1) 1p", 4, 2, 2, 1},
        {"5-reg(t1,t0,s0,s1,s2) 1p", 5, 2, 3, 1},
        {"5-reg(t1,t0,s0,s1,s2) 2p", 5, 2, 3, 2},
        {"6-reg(t2,t1,t0,s0,s1) 2p", 6, 3, 3, 2},
        {"7-reg(t2,t1,t0,s0,s1,s2) 2p", 7, 3, 4, 2},
        {"8-reg(4c4l) 2p", 8, 4, 4, 2},
        {"12-reg(6c6l) 2p", 12, 6, 6, 2},
        {"16-reg(8c8l) 2p", 16, 8, 8, 2},
        {"20-reg(10c10l) 2p", 20, 10, 10, 2},
        {"24-reg(12c12l) 2p", 24, 12, 12, 2},
        {"rv64(15c13l) 7p", 32, 15, 13, 7},
    };

    printf("=== Inversion threshold by ABI size ===\n\n");

    for (auto& desc : configs) {
        auto abi = make_abi(desc);
        int threshold = -1;
        int limit = desc.total <= 5 ? 6 : 10;
        for (int n = 1; n <= limit; n++) {
            auto ir = gen_ir(n, desc.n_params);
            bool inv = find_inversion(ir, abi);
            if (inv) {
                threshold = n;
                break;
            }
        }
        printf("[%s] threshold: n_vars=%d", desc.name, threshold);
        if (threshold > 0)
            printf("  (%d values: %d params + %d vars, %d caller regs)", threshold + desc.n_params,
                   desc.n_params, threshold, desc.n_caller);
        printf("\n");
    }

    // Probe: total=8, caller=6, callee=2, params=6 with 0-sized float regs
    // (eliminates float backup temp interference, reducing degree by 2)
    fmt::println("\n=== Probe: total=8, 0 float regs ===");
    {
        bool probe_found = false;
        for (int nv = 1; nv <= 5; nv++) {
            std::set<size_t> c0 = {0, 1, 2, 3, 4, 5}, c1 = {6, 7};
            RegisterABI r0 = {8, c0, c1, {}, {0, 1, 2, 3, 4, 5}, 0, [](size_t i) {
                                  return i < 6 ? fmt::format("t{}", i) : fmt::format("s{}", i - 6);
                              }};
            RegisterABI r1 = {0, {}, {}, {}, {}, 0, [](size_t) { return "f"; }};
            TargetABI abi = {{r0, r1, 7}, rv64::ABI.mem};
            auto ir = gen_ir(nv, 6);
            auto ss = std::istringstream(ir);
            auto pp = ir::parse(ss);
            ConstructSSA().apply(*pp);
            NonSSAPassContext cx(*pp);
            DestructSSA().apply(*pp, cx);
            DeadAllocElimination<NonSSAPassContext>().apply(*pp, cx);
            DeadTempElimination<NonSSAPassContext>().apply(*pp, cx);
            RegisterAllocation ra(abi, false);
            ra.apply(*pp, cx);
            RegisterReplacement<NonSSAPassContext>(ra.colored, ra.precolored).apply(*pp, cx);
            RedundantMoveElimination<NonSSAPassContext>().apply(*pp, cx);
            auto out = fmt::format("{}", *pp);
            bool has_t = false, has_s = false, has_sp = false;
            std::istringstream in(out);
            std::string ln;
            while (std::getline(in, ln)) {
                if (ln.find("@__reg_t") != std::string::npos &&
                    ln.find("=@__reg_s") != std::string::npos && ln.find('+') == std::string::npos)
                    has_t = true;
                auto p2 = ln.find("@__reg_s");
                if (p2 != std::string::npos) {
                    auto eq = ln.find('=');
                    if (eq != std::string::npos && eq > p2) {
                        auto rhs = ln.substr(eq + 1);
                        if (ln.substr(0, eq).find("@__reg_s") != std::string::npos &&
                            rhs.find("@__reg_t") == std::string::npos &&
                            rhs.find("spill") == std::string::npos)
                            has_s = true;
                    }
                }
                if (ln.find("__spill") != std::string::npos) has_sp = true;
            }
            if (has_t && has_s && !has_sp) {
                fmt::println("Found: total=8 caller=6 callee=2 params=6 n_vars={} (0 float)", nv);
                fmt::println("Before:\n{}", ir);
                fmt::println("After:\n{}", out);
                probe_found = true;
                goto done;
            }
        }
        if (!probe_found) fmt::println("Nothing for total=8 even with 0 float regs");
    }

    // Search for custom ABI with pseudo-spill (no __spill_N).
    fmt::println("\n=== Pseudo-spill search (<=8 regs) ===");
    {
        bool found = false;
        for (int total = 6; total <= 8; total++) {
            for (int callee = 1; callee <= total - 2; callee++) {
                int caller = total - callee;
                for (int n_params = 2; n_params <= caller && n_params <= 7; n_params++) {
                    // n_vars must be < total - callee (to avoid degree >= max_color)
                    int max_vars = total - callee - 1;
                    // n_vars must be > caller - n_params (to force s-reg use)
                    int min_vars = caller - n_params + 1;
                    for (int n_vars = min_vars; n_vars <= max_vars && n_vars <= 14; n_vars++) {
                        if (n_vars < 1) continue;
                        auto abi = make_abi({"", total, caller, callee, n_params});
                        auto ir = gen_ir(n_vars, n_params);
                        auto s = std::istringstream(ir);
                        auto p = ir::parse(s);
                        ConstructSSA().apply(*p);
                        NonSSAPassContext c(*p);
                        DestructSSA().apply(*p, c);
                        DeadAllocElimination<NonSSAPassContext>().apply(*p, c);
                        DeadTempElimination<NonSSAPassContext>().apply(*p, c);
                        RegisterAllocation ra(abi, false);
                        ra.apply(*p, c);
                        RegisterReplacement<NonSSAPassContext>(ra.colored, ra.precolored)
                            .apply(*p, c);
                        RedundantMoveElimination<NonSSAPassContext>().apply(*p, c);
                        std::string out = fmt::format("{}", *p);
                        bool t_save = false, s_use = false, has_spill = false;
                        std::istringstream sin(out);
                        std::string ln;
                        while (std::getline(sin, ln)) {
                            if (ln.find("@__reg_t") != std::string::npos &&
                                ln.find("= @__reg_s") != std::string::npos &&
                                ln.find('+') == std::string::npos &&
                                ln.find('*') == std::string::npos)
                                t_save = true;
                            size_t sp = ln.find("@__reg_s");
                            if (sp != std::string::npos) {
                                auto eq = ln.find('=');
                                if (eq != std::string::npos && eq > sp) {
                                    auto lhs = ln.substr(0, eq), rhs = ln.substr(eq + 1);
                                    if (lhs.find("@__reg_s") != std::string::npos &&
                                        rhs.find("@__reg_t") == std::string::npos &&
                                        rhs.find("spill") == std::string::npos &&
                                        rhs.find("*") == std::string::npos &&
                                        rhs.find("<-") == std::string::npos)
                                        s_use = true;
                                }
                            }
                            if (ln.find("__spill") != std::string::npos ||
                                ln.find("spill") != std::string::npos)
                                has_spill = true;
                        }
                        if (t_save && s_use && !has_spill) {
                            fmt::println("Found: total={} caller={} callee={} params={} n_vars={}",
                                         total, caller, callee, n_params, n_vars);
                            fmt::println("Before regalloc:\n{}", ir);
                            fmt::println("After regalloc:\n{}", out);
                            found = true;
                            goto done;
                        }
                    }
                }
            }
        }
        if (!found) fmt::println("Nothing found for total<=8");
    }
    fmt::println("\n=== Pseudo-spill search (larger) ===");
    {
        bool found = false;
        for (int total = 10; total <= 32; total += 2) {
            for (int caller = total / 2; caller <= total - 2; caller++) {
                int callee = total - caller;
                int n_vars = caller - 1;
                int n_params = caller > 7 ? 7 : caller - 1;
                if (n_vars < 2 || n_vars > 14) continue;
                if (n_params < 2) continue;
                auto abi = make_abi({"", total, caller, callee, n_params});
                auto ir = gen_ir(n_vars, n_params);
                auto s = std::istringstream(ir);
                auto p = ir::parse(s);
                ConstructSSA().apply(*p);
                NonSSAPassContext c(*p);
                DestructSSA().apply(*p, c);
                DeadAllocElimination<NonSSAPassContext>().apply(*p, c);
                DeadTempElimination<NonSSAPassContext>().apply(*p, c);
                RegisterAllocation ra(abi, false);
                ra.apply(*p, c);
                RegisterReplacement<NonSSAPassContext>(ra.colored, ra.precolored).apply(*p, c);
                RedundantMoveElimination<NonSSAPassContext>().apply(*p, c);
                std::string out = fmt::format("{}", *p);
                bool t_save = false, s_use = false, has_spill = false;
                std::istringstream sin(out);
                std::string ln;
                while (std::getline(sin, ln)) {
                    if (ln.find("@__reg_t") != std::string::npos &&
                        ln.find("= @__reg_s") != std::string::npos &&
                        ln.find('+') == std::string::npos && ln.find('*') == std::string::npos)
                        t_save = true;
                    size_t sp = ln.find("@__reg_s");
                    if (sp != std::string::npos) {
                        auto eq = ln.find('=');
                        if (eq != std::string::npos && eq > sp) {
                            auto lhs = ln.substr(0, eq), rhs = ln.substr(eq + 1);
                            if (lhs.find("@__reg_s") != std::string::npos &&
                                rhs.find("@__reg_t") == std::string::npos &&
                                rhs.find("spill") == std::string::npos &&
                                rhs.find("*") == std::string::npos &&
                                rhs.find("<-") == std::string::npos)
                                s_use = true;
                        }
                    }
                    if (ln.find("__spill") != std::string::npos ||
                        ln.find("spill") != std::string::npos)
                        has_spill = true;
                }
                if (t_save && s_use && !has_spill) {
                    fmt::println("Found: total={} caller={} callee={} params={} n_vars={}", total,
                                 caller, callee, n_params, n_vars);
                    fmt::println("Before regalloc:\n{}", ir);
                    fmt::println("After regalloc:\n{}", out);
                    found = true;
                    goto done;
                }
            }
        }
        if (!found) fmt::println("No custom ABI found");
    }
    fmt::println("\n=== Fallback: rv64 ABI ===");
    {
        auto ir = gen_ir(13, 7);
        auto s = std::istringstream(ir);
        auto p = ir::parse(s);
        ConstructSSA().apply(*p);
        NonSSAPassContext c(*p);
        DestructSSA().apply(*p, c);
        DeadAllocElimination<NonSSAPassContext>().apply(*p, c);
        DeadTempElimination<NonSSAPassContext>().apply(*p, c);
        fmt::println("Before regalloc:\n{}", *p);
        RegisterAllocation ra(rv64::ABI, false);
        ra.apply(*p, c);
        RegisterReplacement<NonSSAPassContext>(ra.colored, ra.precolored).apply(*p, c);
        RedundantMoveElimination<NonSSAPassContext>().apply(*p, c);
        fmt::println("After regalloc:\n{}", *p);
    }
done:;
    return 0;
}
