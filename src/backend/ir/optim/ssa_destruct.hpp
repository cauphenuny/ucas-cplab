/// @brief Exit from SSA Form by eliminating phi instructions

#include "backend/ir/ir.h"
#include "framework.hpp"

namespace ir::optim {

namespace minipass {

struct SplitCriticalEdges : NonSSAPass {};

struct ScheduleCopy : NonSSAPass {};

}  // namespace minipass

using DestructSSA =
    Compose<NonSSAPassContext, minipass::SplitCriticalEdges, minipass::ScheduleCopy>;

}  // namespace ir::optim
