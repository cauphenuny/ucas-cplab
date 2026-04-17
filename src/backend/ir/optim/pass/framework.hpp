#include "backend/ir/ir.hpp"

namespace ir::optim::pass {

struct Pass {
    virtual void apply(Program& prog) = 0;
};

template <typename... Passes> struct Compose {
    void apply(Program& prog) {
        (Passes().apply(prog), ...);
    }
};

}  // namespace ir::optim::pass
