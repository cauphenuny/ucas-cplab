#include "backend/ir/gen/irgen.h"
#include "backend/ir/parse/visit.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"

#include <filesystem>

int main() {
    auto path = "test/samples_codegen_functional";
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        if (file.path().extension() != ".cact") continue;
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
        }
    }
    return 0;
}