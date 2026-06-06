/// @brief Reproduce the "inverted" register allocation pattern.
///
/// Inversion: callee-saved register backups get caller-saved (t) register
/// colors while program variables get callee-saved (s) register colors.
///
/// Root cause: when more values are simultaneously live than there are
/// caller-saved registers, the Briggs allocator must assign some program
/// variables to callee-saved (s) registers. Their backup temps cannot
/// coalesce (the function body uses those s-regs), so they take whatever
/// colors remain — the highest t-registers (t3-t6).
///
///  f entry (post-lowering):
///    @__reg_t6: int = @__reg_ra;    // ra backup -> t6
///    @__reg_t5: int = @__reg_s0;    // s0 backup -> t5
///    @__reg_t4: int = @__reg_s1;    // s1 backup -> t4
///    @__reg_t3: int = @__reg_s2;    // s2 backup -> t3
///    @__reg_s2: int = @__reg_a2;    // param c -> s2 (callee-saved!)

#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/transform/ssa/construct.hpp"
#include "backend/ir/transform/ssa/destruct.hpp"
#include "backend/rv64/abi.hpp"

#include <sstream>
#include <string>
#include <vector>

using namespace ir::lowering;
using namespace ir::transform;

// ---------------------------------------------------------------------------
// Generate IR with N live-across-loop variables
// ---------------------------------------------------------------------------
static std::string gen_ir(int n_vars) {
    // Generate parameter list: a0..aN (up to 8)
    int n_params = n_vars < 14 ? 7 : 0;  // fewer params -> more caller-saved free
    if (n_vars >= 14) n_params = 4;
    if (n_vars >= 18) n_params = 0;

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
        ir += "        @v" + std::to_string(i) + ": i32 = @v" + std::to_string(i) + " + " +
              std::to_string((i % 5) + 1) + ";\n";
    ir += "        => 'cond;\n"
          "    }\n"
          "    'exit: {\n"
          "        return @v1;\n"
          "    }\n"
          "}\n";
    return ir;
}

// ---------------------------------------------------------------------------
// Register allocation pipeline
// ---------------------------------------------------------------------------
static bool find_inversion(const std::string& ir_str, const TargetABI& abi) {
    auto stream = std::istringstream(ir_str);
    auto prog_box = ir::parse(stream);
    auto& prog = *prog_box;

    // SSA pipeline (same as the compiler)
    ConstructSSA().apply(prog);
    NonSSAPassContext ctx(prog);
    DestructSSA().apply(prog, ctx);

    // Register allocation
    RegisterAllocation regalloc(abi, false);
    regalloc.apply(prog, ctx);
    ctx.ud.verify();

    // Get color map for debugging
    std::string colors;
    for (auto& [val, id] : regalloc.colored)
        colors += "  " + fmt::format("{}", val) + " -> color " + std::to_string(id) + "\n";

    // Replace colored values with register proxies + elim redundant MOVs
    RegisterReplacement<NonSSAPassContext>(regalloc.colored, regalloc.precolored).apply(prog, ctx);
    RedundantMoveElimination<NonSSAPassContext>().apply(prog, ctx);
    ctx.ud.verify();

    std::string out = fmt::format("{}", prog);

    // Detect inversion: look for @__reg_tN = @__reg_sM (backup in t-reg)
    // and @__reg_sN = @__reg_aM (program var in s-reg)
    bool has_t_save = false, has_s_use = false;
    std::istringstream sin(out);
    std::string ln;
    while (std::getline(sin, ln)) {
        if (ln.find("@__reg_t") != std::string::npos &&
            ln.find("= @__reg_s") != std::string::npos && ln.find('+') == std::string::npos &&
            ln.find('*') == std::string::npos)
            has_t_save = true;
        if (ln.find("@__reg_s") != std::string::npos) {
            auto eq = ln.find('=');
            if (eq != std::string::npos) {
                auto rhs = ln.substr(eq + 1);
                if (rhs.find("@__reg_a") != std::string::npos &&
                    rhs.find("spill") == std::string::npos && rhs.find("*") == std::string::npos)
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
    // Use the real rv64 ABI — the inversion appears under sufficient pressure
    auto& abi = rv64::ABI;

    printf("=== Inversion search (raw IR) ===\n\n");

    // Scan n_vars from 12 upwards to find the threshold
    int found_at = -1;
    for (int n = 12; n <= 20; n++) {
        auto ir = gen_ir(n);
        bool inv = find_inversion(ir, abi);
        printf("[n_vars=%d] %s\n", n, inv ? "*** INVERTED ***" : "normal");
        if (inv && found_at < 0) found_at = n;
    }

    if (found_at >= 0) {
        printf("\nThreshold: n_vars >= %d triggers inversion\n", found_at);
        printf("With 7 params consuming a-regs, ~%d values are simultaneously\n", found_at + 7);
        printf("live, exceeding ~15 caller-saved rv64 registers.\n\n");

        // Re-run at threshold and print entry block
        auto ir = gen_ir(found_at);
        auto stream = std::istringstream(ir);
        auto prog = ir::parse(stream);
        ConstructSSA().apply(*prog);
        NonSSAPassContext ctx(*prog);
        DestructSSA().apply(*prog, ctx);
        RegisterAllocation regalloc(rv64::ABI, false);
        regalloc.apply(*prog, ctx);
        RegisterReplacement<NonSSAPassContext>(regalloc.colored, regalloc.precolored)
            .apply(*prog, ctx);
        RedundantMoveElimination<NonSSAPassContext>().apply(*prog, ctx);
        auto out = fmt::format("{}", *prog);
        size_t e = out.find("'entry:");
        if (e != std::string::npos) {
            size_t end = out.find("\n    '", e + 1);
            if (end == std::string::npos) end = out.find("\n}", e + 1);
            printf("Entry block:\n%s\n", out.substr(e, end - e).c_str());
        }
        return 0;
    }
    printf("\nNot found (up to 30 vars)\n");
    return 0;
}
