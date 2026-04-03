#include "IRBaseVisitor.h"
#include "IRLexer.h"
#include "IRParser.h"

#include <istream>

namespace ir {

auto parse(std::istream& input) {
    using namespace antlr4;
    ANTLRInputStream input_stream(input);
    IRLexer lexer(&input_stream);
    CommonTokenStream tokens(&lexer);
    IRParser parser(&tokens);
    auto tree = parser.program();
    IRBaseVisitor visitor;
    visitor.visit(tree);
}

}  // namespace ir