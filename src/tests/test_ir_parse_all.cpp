#include "backend/ir/gen/irgen.h"
#include "backend/ir/parse/visit.hpp"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

int main() {
    std::string base_path = "test/samples_codegen_functional";
    std::filesystem::path path(base_path);
    const int max_up = 10;
    int up = 0;
    while (!std::filesystem::exists(path) && up < max_up) {
        path = std::filesystem::path("..") / path;
        ++up;
    }
    if (!std::filesystem::exists(path)) {
        fmt::println("Directory not found after searching {} levels: {}\n", max_up, base_path);
        return 1;
    }
    fmt::println("Using directory: {}\n", path.string());
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        if (file.path().extension() != ".cact") continue;
        fmt::println("Testing file: {}\n", file.path().string());
        auto fstream = std::fstream(file.path());
        auto text =
            std::string(std::istreambuf_iterator<char>(fstream), std::istreambuf_iterator<char>());
        fmt::println("Code: \n{}\n", text);
        auto istream = std::istringstream(text);
        auto ast = ast::analysis(ast::parse(istream));
        auto code = ir::gen::generate(ast);
        auto ir_text = fmt::format("{}\n", code);
        fmt::println("Generated IR: \n{}\n", ir_text);
        auto ir_stream = std::istringstream(ir_text);
        try {
            auto ir_code = ir::parse(ir_stream);
            fmt::println("Reconstructed IR: \n{}\n", ir_code);
        } catch (const std::exception& e) {
            fmt::println("Exception: {}\n", e.what());
            exit(1);
        }
    }
    return 0;
}