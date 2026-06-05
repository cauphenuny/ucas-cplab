#include "backend/ir/ir.h"
#include "vm.h"

namespace ir::vm {

void VirtualMachine::debug_tui() {
    fmt::print(stderr, "(riir-db) ");
}

}  // namespace ir::vm