#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"
#include "backend/ir/vm/view.hpp"
#include "utils/match.hpp"
#include "vm.h"

#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/terminal.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ir::vm {

// Format a View's value as a human-readable string.
static std::string format_view(const View& v) {
    if (v.data == nullptr) return "(void)";
    using namespace ir::type;
    return match(
        v.type.var(),
        [&](const Primitive& p) -> std::string {
            return Match{p}([&](auto t) -> std::string {
                using T = typename decltype(t)::type;
                return fmt::format("{}", *(const T*)v.data);
            });
        },
        [&](const Reference& r) -> std::string {
            if (r.is_slice) {
                auto ptr = (std::byte*)*(void**)v.data;
                return fmt::format("{}", fmt::ptr(ptr));
            } else {
                auto ptr = (std::byte*)*(void**)v.data;
                return fmt::format("{} at {}", format_view(View{.data = ptr, .type = r.elem}),
                                   fmt::ptr(ptr));
            }
        },
        [&](const Array& arr) -> std::string { return fmt::format("[array size={}]", arr.size); },
        [&](const auto&) -> std::string { return "?"; });
}

static void parse_breakpoint(const std::string& arg, std::string& status_message,
                             std::unordered_set<const void*>& breakpoints,
                             std::vector<std::function<bool()>>& breakpoint_conditions,
                             size_t& perf_num_insts, const Program* program) {
    auto colon = arg.find(':');
    if (colon != std::string::npos) {
        std::string blockname = arg.substr(0, colon);
        std::string idx_str = arg.substr(colon + 1);
        try {
            size_t inst_idx = std::stoul(idx_str);
            const Block* target_block = nullptr;
            for (const auto& func : program->funcs()) {
                for (const auto& blk : func->blocks()) {
                    if (blk->label == blockname) {
                        target_block = blk.get();
                        break;
                    }
                }
                if (target_block) break;
            }
            if (target_block) {
                auto it = target_block->insts().begin();
                for (size_t k = 0; k < inst_idx && it != target_block->insts().end(); k++) ++it;
                if (it != target_block->insts().end()) {
                    breakpoints.insert(&(*it));
                    status_message = fmt::format("breakpoint at '{}:{}'", blockname, inst_idx);
                } else {
                    status_message = fmt::format("error: index {} out of range", inst_idx);
                }
            } else {
                status_message = fmt::format("error: block '{}' not found", blockname);
            }
        } catch (...) {
            status_message = "error: invalid breakpoint argument";
        }
    } else if (!arg.empty()) {
        try {
            size_t N = std::stoul(arg);
            breakpoint_conditions.push_back([&pc = perf_num_insts, N] { return pc == N; });
            status_message = fmt::format("breakpoint at num_insts={}", N);
        } catch (...) {
            status_message = "error: invalid count";
        }
    }
}

void VirtualMachine::debug_tui() {
    using namespace ftxui;

    // FTXUI writes to std::cout directly. If output has been redirected to the
    // debug capture buffer, temporarily restore the real stdout so the TUI is
    // visible, then re-install the capture buffer when we return.
    std::streambuf* captured_buf = nullptr;
    if (saved_output_buf) {
        captured_buf = output.rdbuf(saved_output_buf);
    }

    auto screen = ScreenInteractive::Fullscreen();

    enum class InputMode { None, Breakpoint, StdinInject };
    InputMode input_mode = InputMode::None;
    std::string bp_input;
    std::string bp_placeholder = "block:idx or count";
    std::string stdin_input;
    std::string stdin_placeholder = "value(s) to feed to program input";
    std::string status_message;

    auto bp_input_comp = Input(&bp_input, &bp_placeholder);
    auto stdin_input_comp = Input(&stdin_input, &stdin_placeholder);

    auto renderer = Renderer(Container::Vertical({bp_input_comp, stdin_input_comp}), [&] {
        // --- Code panel (left side, top) ---
        Elements code_lines;
        std::string block_label =
            debug_state.current_block ? debug_state.current_block->label : "(null)";

        if (debug_state.current_block) {
            for (const auto& inst : debug_state.current_block->insts()) {
                bool is_current = (&inst == debug_state.current_inst);
                std::string inst_str = fmt::format("{}", inst);
                if (is_current) {
                    code_lines.push_back(text("\u25b6 " + inst_str) | bold | color(Color::Yellow));
                } else {
                    code_lines.push_back(text("  " + inst_str));
                }
            }

            if (debug_state.current_block->hasExit()) {
                const Exit& exit_ref = debug_state.current_block->exit();
                bool is_current = (&exit_ref == debug_state.current_inst);
                std::string exit_str = fmt::format("{}", exit_ref);
                if (is_current) {
                    code_lines.push_back(text("\u25b6 " + exit_str) | bold | color(Color::Yellow));
                } else {
                    code_lines.push_back(text("  " + exit_str));
                }
            }
        }

        auto code_panel = window(text(" Code (" + block_label + ") "), vbox(std::move(code_lines)));

        // --- Functions panel ---
        Elements func_lines;
        if (!active_frames.empty()) {
            const Func* cur_func = active_frames.back().second;
            func_lines.push_back(text("  fn " + cur_func->name) | bold);
            for (const auto& blk : cur_func->blocks()) {
                std::string exit_str = "(no exit)";
                if (blk->hasExit()) {
                    exit_str = fmt::format("{}", blk->exit());
                }
                bool is_current = (blk.get() == debug_state.current_block);
                auto entry = text("    '" + blk->label + ": " + exit_str);
                if (is_current) {
                    func_lines.push_back(entry | bold | color(Color::Yellow));
                } else {
                    func_lines.push_back(entry);
                }
            }
            for (auto& func : program->funcs()) {
                if (func.get() == cur_func) continue;
                func_lines.push_back(text("  fn " + func->name) | bold);
            }
        } else {
            func_lines.push_back(text("  (no active function)"));
        }
        auto func_panel = window(text(" Overview "), vbox(std::move(func_lines)));

        // --- Globals panel ---
        std::vector<std::pair<std::string, const View*>> sorted_globals;
        sorted_globals.reserve(global_frame.vars.size());
        for (const auto& [def, view] : global_frame.vars) {
            std::string name =
                std::visit([](const auto* p) -> std::string { return p->name; }, def);
            sorted_globals.emplace_back(std::move(name), &view);
        }
        std::sort(sorted_globals.begin(), sorted_globals.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        Elements global_entries;
        for (const auto& [name, view] : sorted_globals) {
            global_entries.push_back(
                text("  " + name + ": " + view->type.toString() + " = " + format_view(*view)));
        }
        // Choose columns so each column has at most ~max_rows_per_col rows.
        static constexpr size_t max_rows_per_col = 18;
        Elements globals_panels;
        if (global_entries.empty()) {
            globals_panels.push_back(text("  (none)"));
        } else {
            size_t n = global_entries.size();
            size_t ncols = std::max<size_t>(1, (n + max_rows_per_col - 1) / max_rows_per_col);
            size_t rows = (n + ncols - 1) / ncols;
            for (size_t ci = 0; ci < ncols; ci++) {
                Elements col;
                size_t start = ci * rows;
                size_t end = std::min(start + rows, n);
                for (size_t ei = start; ei < end; ei++) {
                    col.push_back(global_entries[ei]);
                }
                if (!col.empty()) {
                    globals_panels.push_back(vbox(std::move(col)) | flex);
                }
            }
        }
        auto globals_panel = window(text(" Globals "), hbox(std::move(globals_panels)));

        // --- Variables panel ---
        Elements vars_lines;
        size_t idx = debug_state.selected_frame_idx;
        if (active_frames.empty()) {
            vars_lines.push_back(text("  (no frames)"));
        } else {
            if (idx >= active_frames.size()) {
                idx = active_frames.size() - 1;
                debug_state.selected_frame_idx = idx;
            }
            auto* sf = active_frames[idx].first;
            // collect named vars, sort by name
            std::vector<std::pair<std::string, const View*>> sorted_vars;
            for (const auto& [def, view] : sf->vars) {
                std::string name =
                    std::visit([](const auto* p) -> std::string { return p->name; }, def);
                sorted_vars.emplace_back(std::move(name), &view);
            }
            std::sort(sorted_vars.begin(), sorted_vars.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [name, view] : sorted_vars) {
                vars_lines.push_back(
                    text("  " + name + ": " + view->type.toString() + " = " + format_view(*view)));
            }
            for (size_t ti = 0; ti < sf->temps.size(); ti++) {
                const auto& view = sf->temps[ti];
                if (view.data != nullptr) {
                    vars_lines.push_back(text("  %" + std::to_string(ti) + ": " +
                                              view.type.toString() + " = " + format_view(view)));
                }
            }
            if (sf->vars.empty() && sf->temps.empty()) {
                vars_lines.push_back(text("  (none)"));
            }
        }
        std::string frame_name = active_frames.empty() ? "?" : active_frames[idx].second->name;
        // Multi-column layout for variables, same adaptive logic as globals.
        Elements vars_cols;
        if (vars_lines.empty()) {
            vars_cols.push_back(text("  (none)"));
        } else {
            size_t n = vars_lines.size();
            size_t ncols = std::max<size_t>(1, (n + max_rows_per_col - 1) / max_rows_per_col);
            size_t rows = (n + ncols - 1) / ncols;
            for (size_t ci = 0; ci < ncols; ci++) {
                Elements col;
                size_t start = ci * rows;
                size_t end = std::min(start + rows, n);
                for (size_t ei = start; ei < end; ei++) {
                    col.push_back(vars_lines[ei]);
                }
                if (!col.empty()) {
                    vars_cols.push_back(vbox(std::move(col)) | flex);
                }
            }
        }
        auto vars_panel =
            window(text(" Variables (frame: " + frame_name + ") "), hbox(std::move(vars_cols)));

        // --- Call Stack panel ---
        Elements stack_lines;
        for (size_t i = 0; i < active_frames.size(); i++) {
            auto entry = text("  [" + std::to_string(i) + "] " + active_frames[i].second->name);
            if (i == debug_state.selected_frame_idx) {
                stack_lines.push_back(entry | bold | inverted);
            } else {
                stack_lines.push_back(entry);
            }
        }
        if (active_frames.empty()) {
            stack_lines.push_back(text("  (empty)"));
        }
        auto stack_panel = window(text(" Call Stack "), vbox(std::move(stack_lines)));

        // --- Program output ---
        std::string prog_output = debug_output_buf.str();
        Elements output_lines;
        if (prog_output.empty()) {
            output_lines.push_back(text("  (none)"));
        } else {
            size_t pos = 0, next;
            while ((next = prog_output.find('\n', pos)) != std::string::npos) {
                output_lines.push_back(text("  " + prog_output.substr(pos, next - pos)));
                pos = next + 1;
            }
            if (pos < prog_output.size())
                output_lines.push_back(text("  " + prog_output.substr(pos)));
        }
        auto output_panel = window(text(" Output "), vbox(std::move(output_lines)));

        // --- Stdin buffer panel ---
        // Show unread portion of the stdin buffer.
        std::string pending_input;
        {
            auto pos = debug_input_buf.tellg();
            std::string all = debug_input_buf.str();
            pending_input = (pos >= 0 && static_cast<size_t>(pos) <= all.size())
                                ? all.substr(static_cast<size_t>(pos))
                                : all;
        }
        Elements stdin_lines;
        if (pending_input.empty()) {
            stdin_lines.push_back(text("  (empty) — press [i] to inject"));
        } else {
            size_t pos2 = 0, next;
            while ((next = pending_input.find('\n', pos2)) != std::string::npos) {
                stdin_lines.push_back(text("  " + pending_input.substr(pos2, next - pos2)));
                pos2 = next + 1;
            }
            if (pos2 < pending_input.size())
                stdin_lines.push_back(text("  " + pending_input.substr(pos2)));
        }
        auto stdin_panel = window(text(" Stdin Buffer "), vbox(std::move(stdin_lines)));

        // --- Command bar ---
        Element cmd_bar;
        if (input_mode == InputMode::Breakpoint) {
            cmd_bar = bp_input_comp->Render() | border;
        } else if (input_mode == InputMode::StdinInject) {
            cmd_bar = stdin_input_comp->Render() | border;
        } else {
            cmd_bar = text(" [n/Enter] step  [c] continue  [b] breakpoint  [i] inject input  "
                           "[Up/Down] frame  [q] quit ");
        }

        auto status_line = status_message.empty() ? text("") : text(" " + status_message) | bold;

        // --- Assemble layout ---
        auto term_w = Terminal::Size().dimx;
        int half = term_w / 2;
        auto layout = vbox({
            hbox({
                code_panel | size(WIDTH, EQUAL, half),
                func_panel | size(WIDTH, EQUAL, term_w - half),
            }),
            globals_panel,
            hbox({vars_panel | flex, stack_panel | size(WIDTH, EQUAL, 32)}),
            hbox({stdin_panel | flex, output_panel | flex}),
            separator(),
            cmd_bar | bold,
            status_line,
        });

        return layout;
    });

    renderer |= CatchEvent([&](const Event& event) {
        if (input_mode == InputMode::Breakpoint) {
            if (event == Event::Return) {
                input_mode = InputMode::None;
                parse_breakpoint(bp_input, status_message, debug_state.breakpoints,
                                 debug_state.breakpoint_conditions, perf_counter.num_insts,
                                 program);
                return true;
            }
            if (event == Event::Escape) {
                input_mode = InputMode::None;
                status_message.clear();
                return true;
            }
            return false;  // propagate to child Input component
        }
        if (input_mode == InputMode::StdinInject) {
            if (event == Event::Return) {
                input_mode = InputMode::None;
                // Append the entered text + newline to the unconsumed stdin buffer.
                // Preserve whatever hasn't been read yet, then append the new data.
                std::string unread;
                {
                    std::string remaining;
                    std::getline(debug_input_buf, remaining, '\0');  // read to EOF
                    unread = remaining;
                }
                debug_input_buf.str(unread + stdin_input + "\n");
                debug_input_buf.clear();
                debug_input_buf.seekg(0);
                status_message = fmt::format("injected: \"{}\"", stdin_input);
                stdin_input.clear();
                return true;
            }
            if (event == Event::Escape) {
                input_mode = InputMode::None;
                status_message.clear();
                stdin_input.clear();
                return true;
            }
            return false;  // propagate to child Input component
        }

        if (event == Event::Character('n') || event == Event::Return) {
            screen.Exit();
            return true;
        }
        if (event == Event::Character('c')) {
            debug_state.stepping = false;
            screen.Exit();
            return true;
        }
        if (event == Event::Character('q')) {
            screen.Exit();
            exit(0);
            return true;
        }
        if (event == Event::Character('b')) {
            input_mode = InputMode::Breakpoint;
            bp_input.clear();
            bp_input_comp->TakeFocus();
            status_message = "enter breakpoint, Enter to confirm, Esc to cancel";
            return true;
        }
        if (event == Event::Character('i')) {
            input_mode = InputMode::StdinInject;
            stdin_input.clear();
            stdin_input_comp->TakeFocus();
            status_message = "enter value(s) for program input, Enter to confirm, Esc to cancel";
            return true;
        }
        if (event == Event::ArrowUp) {
            if (debug_state.selected_frame_idx > 0) debug_state.selected_frame_idx--;
            return true;
        }
        if (event == Event::ArrowDown) {
            if (!active_frames.empty() && debug_state.selected_frame_idx < active_frames.size() - 1)
                debug_state.selected_frame_idx++;
            return true;
        }
        return true;  // consume all other events silently
    });

    screen.Loop(renderer);

    // Re-install the capture buffer so program output continues to be captured.
    if (captured_buf) {
        output.rdbuf(captured_buf);
    }
}

}  // namespace ir::vm
