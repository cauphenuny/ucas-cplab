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

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, const char* argv[]) {
    // first arg: comma-separated pass names, e.g. "cp,dde"
    std::string passes_arg;
    if (argc > 1) passes_arg = argv[1];

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
    using PassFactory = std::function<std::unique_ptr<ir::optim::Pass>()>;
    std::map<std::string, PassFactory> factories = {
        {"copy", []() { return std::make_unique<ir::optim::CopyPropagation>(); }},
        {"def", []() { return std::make_unique<ir::optim::DeadDefElimination>(); }},
        {"alloc", []() { return std::make_unique<ir::optim::DeadAllocElimination>(); }},
        {"const", []() { return std::make_unique<ir::optim::ConstPropagation>(); }},
        {"ssa2temp", []() { return std::make_unique<ir::optim::SSAValue2TempValue>(); }},
        {"inline", []() { return std::make_unique<ir::optim::Inlining>(); }},
        {"exp",
         []() {
             return std::make_unique<ir::optim::Compose<ir::optim::DeadBlockElimination,
                                                        ir::optim::CommonSubexprElimination>>();
         }},
        {"block", []() {
             return std::make_unique<
                 ir::optim::Compose<ir::optim::SimplifyCFG, ir::optim::DeadBlockElimination>>();
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

    for (const auto& file : files) {
        fmt::print(stderr, "{}: ", file.path().string());

        // read source file
        std::ifstream fstream(file.path());
        if (!fstream.is_open()) {
            fmt::println(stderr, "Failed to open file: {}\n", file.path().string());
            continue;
        }
        std::string text{std::istreambuf_iterator<char>(fstream), std::istreambuf_iterator<char>()};

        try {
            // parse & semantic analysis -> generate IR
            auto istream = std::istringstream(text);
            auto ast = ast::analysis(ast::parse(istream));
            auto program = ir::gen::generate(ast);

            // helper to run program and return executed instruction count
            auto run_program = [&](const ir::Program& prog) -> size_t {
                // prepare input stream from .in if exists
                std::filesystem::path in_path = file.path();
                std::filesystem::path out_path = file.path();
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

            size_t before = run_program(program);

            // build passes list from requested names
            std::vector<std::unique_ptr<ir::optim::Pass>> passes;
            for (const auto& name : pass_names) {
                auto it = factories.find(name);
                if (it != factories.end()) {
                    passes.push_back(it->second());
                } else {
                    fmt::println(stderr, "Unknown pass: {} (skipping)", name);
                }
            }

            auto apply = [&](const std::vector<std::unique_ptr<ir::optim::Pass>>& passes) {
                bool changed = false;
                for (auto& pass : passes) {
                    changed |= pass->apply(program);
                }
                return changed;
            };
            // apply passes in order
            ir::optim::ConstructSSA ssa;
            ssa.apply(program);
            while (apply(passes));

            size_t after = run_program(program);

            double improvement = 0.0;
            if (before > 0) {
                improvement = ((ssize_t)before - (ssize_t)after) * 100.0 / before;
            }

            fmt::println(stderr, "{} -> {}, improvement: {:.2f}%", before, after, improvement);

        } catch (const std::exception& e) {
            fmt::println(stderr, "Exception while processing {}: {}\n", file.path().string(),
                         e.what());
            exit(1);
        }
    }

    return 0;
}
