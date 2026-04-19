/// @brief Inline Pass, must run after SSA construction

/// TODO:
/// 0. collect call sites, for each call site:
/// 1. split block
/// 2. clone func
/// 3. add prologue block consisting mov inst for copying parameters
/// 4. collect exit blocks, add epilogue block consisting a phi inst for merging return values
/// 5. move params and locals to caller's locals
/// 6. rename temps, merge to caller's temps
/// 7. move func's block to caller
/// 8. connect blocks
