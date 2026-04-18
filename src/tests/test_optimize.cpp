#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.hpp"
#include "backend/ir/optim/const_propagation.hpp"
#include "backend/ir/optim/copy_propagation.hpp"
#include "backend/ir/optim/dead_alloc.hpp"
#include "backend/ir/optim/dead_block.hpp"
#include "backend/ir/optim/dead_def.hpp"
#include "backend/ir/optim/framework.hpp"
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

    std::vector<std::string> pass_names;
    if (!passes_arg.empty()) {
        std::istringstream ss(passes_arg);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) pass_names.push_back(token);
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
        {"ssa", []() { return std::make_unique<ir::optim::ToSSA>(); }},
        {"ssa2temp", []() { return std::make_unique<ir::optim::SSAValue2TempValue>(); }},
        {"block", []() { return std::make_unique<ir::optim::TrivialBlockElimination>(); }}};

    fmt::println(stderr, "Using directory: {}\n", path.string());

    for (const auto& file : std::filesystem::directory_iterator(path)) {
        if (file.path().extension() != ".cact") continue;

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
                in_path.replace_extension("in");
                std::string in_text;
                if (std::filesystem::exists(in_path)) {
                    std::ifstream in_file(in_path);
                    in_text = std::string{std::istreambuf_iterator<char>(in_file),
                                          std::istreambuf_iterator<char>()};
                }
                std::istringstream in_stream(in_text);
                std::ostringstream out_stream;
                ir::vm::VirtualMachine vm(in_stream, out_stream);
                vm.execute(prog);
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

            // apply passes in order
            for (auto& pass : passes) {
                pass->apply(program);
            }

            size_t after = run_program(program);

            double improvement = 0.0;
            if (before > 0) {
                improvement = ((ssize_t)before - (ssize_t)after) * 100.0 / before;
            }

            fmt::println(stderr, "{} -> {}, improvement: {:.2f}%", before, after, improvement);

        } catch (const std::exception& e) {
            fmt::println(stderr, "Exception while processing {}: {}\n", file.path().string(),
                         e.what());
            continue;
        }
    }

    return 0;
}
