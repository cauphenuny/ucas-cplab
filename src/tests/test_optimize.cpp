#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.h"
#include "backend/ir/optim/common_expr.hpp"
#include "backend/ir/optim/const_propagation.hpp"
#include "backend/ir/optim/copy_propagation.hpp"
#include "backend/ir/optim/dead_alloc.hpp"
#include "backend/ir/optim/dead_block.hpp"
#include "backend/ir/optim/dead_def.hpp"
#include "backend/ir/optim/framework.hpp"
#include "backend/ir/optim/inline.hpp"
#include "backend/ir/optim/ssa.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

int main(int argc, const char* argv[]) {
    // --enum [passes] [timeout]: enumerate all subsets of passes (must be first arg)
    // first arg: comma-separated pass names, e.g. "cp,dde"
    // second arg: timeout in seconds (default 5)
    bool enum_mode = false;
    if (argc > 1 && std::string(argv[1]) == "--enum") {
        enum_mode = true;
        --argc;
        ++argv;
    }
    std::string passes_arg;
    if (argc > 1) passes_arg = argv[1];

    int timeout_sec = 5;
    if (argc > 2) {
        try {
            timeout_sec = std::stoi(argv[2]);
        } catch (...) {
            fmt::println(stderr, "Invalid timeout argument, using default {}s", timeout_sec);
        }
    }
    auto timeout_dur = std::chrono::seconds(timeout_sec);

    std::vector<std::string> pass_names, negative_passes;
    if (!passes_arg.empty()) {
        std::istringstream ss(passes_arg);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                if (token[0] == '-') {
                    negative_passes.push_back(token.substr(1));
                } else {
                    pass_names.push_back(token);
                }
            }
        }
    }

    std::string base_path = "test/samples_codegen_functional";
    std::filesystem::path path(base_path);
    const int max_up = 10;
    int up = 0;
    while (!std::filesystem::exists(path) && up < max_up) {
        path = std::filesystem::path("..") / path;
        ++up;
    }
    if (!std::filesystem::exists(path)) {
        fmt::println(stderr, "Directory not found after searching {} levels: {}\n", max_up,
                     base_path);
        return 1;
    }

    // factory mapping from short name -> Pass constructor
    using PassFactory = std::function<std::unique_ptr<ir::optim::SSAPass>()>;
    std::map<std::string, PassFactory> factories = {
        {"copy", []() { return std::make_unique<ir::optim::CopyPropagation>(); }},
        {"def", []() { return std::make_unique<ir::optim::DeadDefElimination>(); }},
        {"alloc", []() { return std::make_unique<ir::optim::DeadAllocElimination>(); }},
        {"const", []() { return std::make_unique<ir::optim::ConstPropagation>(); }},
        {"inline", []() { return std::make_unique<ir::optim::Inlining>(); }},
        {"exp",
         []() {
             return std::make_unique<
                 ir::optim::Compose<ir::optim::SSAPassContext, ir::optim::DeadBlockElimination,
                                    ir::optim::CommonSubexprElimination>>();
         }},
        {"block", []() {
             return std::make_unique<
                 ir::optim::Compose<ir::optim::SSAPassContext, ir::optim::SimplifyCFG,
                                    ir::optim::DeadBlockElimination>>();
         }}};

    if (pass_names.empty()) {
        for (const auto& [name, _] : factories) {
            if (std::find(negative_passes.begin(), negative_passes.end(), name) ==
                negative_passes.end()) {
                pass_names.push_back(name);
                fmt::println(stderr, "add: {}", name);
            } else {
                fmt::println(stderr, "skip: {}", name);
            }
        }
    }

    fmt::println(stderr, "Using directory: {}\n", path.string());

    std::vector<std::filesystem::directory_entry> files;
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        if (file.path().extension() != ".cact") continue;
        files.push_back(file);
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.path().string() < b.path().string(); });

    // build set of pass-name lists to test: powerset in enum mode, single list otherwise
    std::vector<std::vector<std::string>> combinations;
    if (enum_mode) {
        int n = (int)pass_names.size();
        for (int mask = 0; mask < (1 << n); ++mask) {
            std::vector<std::string> subset;
            for (int i = 0; i < n; ++i) {
                if (mask & (1 << i)) subset.push_back(pass_names[i]);
            }
            combinations.push_back(std::move(subset));
        }
    } else {
        combinations.push_back(pass_names);
    }

    for (const auto& file : files) {
        fmt::print(stderr, "{}: ", file.path().string());

        // read source file
        std::ifstream fstream(file.path());
        if (!fstream.is_open()) {
            fmt::println(stderr, "Failed to open file: {}\n", file.path().string());
            continue;
        }
        std::string text{std::istreambuf_iterator<char>(fstream), std::istreambuf_iterator<char>()};

        std::ostringstream log;

        try {
            // parse & semantic analysis -> generate IR
            auto istream = std::istringstream(text);
            auto ast = ast::analysis(ast::parse(istream));
            auto program_box = std::shared_ptr<ir::Program>{ir::gen::generate(ast).release()};

            // helper to run program and return executed instruction count
            // captures file_path by value so the lambda is safe to run in a detached thread
            auto file_path = file.path();
            auto run_program = [file_path](const ir::Program& prog) -> size_t {
                std::filesystem::path in_path = file_path;
                std::filesystem::path out_path = file_path;
                in_path.replace_extension("in");
                out_path.replace_extension("out");
                std::string in_text;
                if (std::filesystem::exists(in_path)) {
                    std::ifstream in_file(in_path);
                    in_text = std::string{std::istreambuf_iterator<char>(in_file),
                                          std::istreambuf_iterator<char>()};
                }
                std::istringstream in_stream(in_text);
                std::ostringstream out_stream;
                ir::vm::VirtualMachine vm(in_stream, out_stream);
                uint8_t ret = vm.execute(prog);
                std::ifstream ans_file(out_path);
                std::string first_line;
                std::getline(ans_file, first_line);
                auto ans_text = std::string{std::istreambuf_iterator<char>(ans_file),
                                            std::istreambuf_iterator<char>()};
                out_stream << (int)ret;
                auto out_text = out_stream.str();
                while (ans_text.back() == '\n') ans_text.pop_back();
                while (out_text.back() == '\n') out_text.pop_back();
                if (ans_text != out_text) {
                    fmt::println(stderr, "Output mismatch: expect {}, get {}", ans_text, out_text);
                    exit(1);
                }
                return vm.perf().num_insts;
            };

            // run prog_ptr in a detached thread; shared_ptr keeps the program alive
            auto run_with_timeout =
                [&, run_program](
                    const std::shared_ptr<ir::Program>& prog_ptr) -> std::optional<size_t> {
                std::packaged_task<size_t()> task(
                    [prog_ptr, run_program]() { return run_program(*prog_ptr); });
                auto fut = task.get_future();
                std::thread(std::move(task)).detach();
                if (fut.wait_for(timeout_dur) != std::future_status::ready) {
                    return std::nullopt;
                }
                return fut.get();
            };

            auto result_before = run_with_timeout(program_box);
            if (!result_before) {
                fmt::println(stderr, "Timeout (>{}s), skipping", timeout_sec);
                continue;
            }
            size_t before = *result_before;

            for (const auto& combo : combinations) {
                // build label
                std::string label;
                for (size_t i = 0; i < combo.size(); ++i) {
                    if (i) label += '+';
                    label += combo[i];
                }
                if (label.empty()) label = "none";

                // fresh program from ast for each combination
                auto combo_prog_box =
                    std::shared_ptr<ir::Program>(ir::gen::generate(ast).release());
                auto& combo_prog = *combo_prog_box;

                ir::optim::Compose<void, ir::optim::ConstructSSA, ir::optim::SSAValue2TempValue>
                    ssa;
                ssa.apply(combo_prog);
                ir::optim::SSAPassContext ctx(combo_prog);

                std::vector<std::unique_ptr<ir::optim::SSAPass>> passes;
                for (const auto& name : combo) {
                    auto it = factories.find(name);
                    if (it != factories.end()) {
                        passes.push_back(it->second());
                    } else {
                        fmt::println(stderr, "Unknown pass: {} (skipping)", name);
                    }
                }

                auto apply = [&](ir::optim::SSAPassContext& ctx2,
                                 const std::vector<std::unique_ptr<ir::optim::SSAPass>>& ps) {
                    bool changed = false;
                    for (auto& pass : ps) {
                        bool pass_changed = pass->apply(combo_prog, ctx2);
                        if (pass_changed) {
                            log << fmt::format("----------\n{}\n", combo_prog);
                            ctx2.ud.verify();
                        }
                        changed |= pass_changed;
                    }
                    return changed;
                };
                log.clear();
                log << fmt::format("{}\n", combo_prog);
                while (apply(ctx, passes));

                auto result_after = run_with_timeout(combo_prog_box);
                if (!result_after) {
                    fmt::println(stderr, "  [{}]: Timeout after optimization (>{}s), skipping",
                                 label, timeout_sec);
                    continue;
                }
                size_t after = *result_after;
                double improvement =
                    before > 0 ? ((ssize_t)before - (ssize_t)after) * 100.0 / before : 0.0;
                fmt::println(stderr, "  [{}]: {} -> {}, improvement: {:.2f}%", label, before, after,
                             improvement);
            }

        } catch (const std::exception& e) {
            fmt::println(stderr, "Exception while processing {}: {}\n", file.path().string(),
                         e.what());
            fmt::println(stderr, "{}", log.str());
            exit(1);
        }
    }

    return 0;
}
