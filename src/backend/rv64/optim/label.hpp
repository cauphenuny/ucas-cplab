#pragma once
#include "backend/rv64/inst.hpp"
#include "framework.hpp"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rv64::optim {

struct DeadLabelElimination : Pass {
    bool apply(Module& mod) override {
        bool changed = false;
        std::unordered_set<std::string> labels;
        for (auto& func : mod.funcs) {
            for (auto& l : labels_in(func)) {
                labels.insert(l);
            }
            labels.insert(func.blocks[0].label);  // entry block label is always needed
        }
        for (auto& func : mod.funcs) {
            std::vector<Block> squashed_blocks;
            for (auto& block : func.blocks) {
                if (labels.count(block.label) > 0) {
                    squashed_blocks.push_back(std::move(block));
                } else {
                    changed = true;
                    Block& last = squashed_blocks.back();
                    last.insts.insert(last.insts.end(), block.insts.begin(), block.insts.end());
                }
            }
            func.blocks = std::move(squashed_blocks);
        }
        return changed;
    }

private:
    std::unordered_set<std::string> labels_in(Block& block) {
        std::unordered_set<std::string> labels;
        for (auto& inst : block.insts) {
            Match{inst}([&](const InstB& b) { labels.insert(b.target); },
                        [&](const InstJ& j) { labels.insert(j.target); },
                        [&](const PseudoB& br) { labels.insert(br.target); },
                        [&](const PseudoJ& j) { labels.insert(j.target); }, [&](const auto&) {});
        }
        return labels;
    }
    std::unordered_set<std::string> labels_in(Func& func) {
        std::unordered_set<std::string> labels;
        for (auto& block : func.blocks) {
            for (auto& l : labels_in(block)) {
                labels.insert(l);
            }
        }
        return labels;
    }
};

}  // namespace rv64::optim