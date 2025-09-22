// src/command/join_adjacent.cpp
#include "command/command.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "selection_controller.h"
#include "include/aegisub/context.h"
#include <algorithm>

namespace {
using cmd::Command;

static AssDialogue* find_neighbor(agi::Context* c, bool prev) {
    auto* cur = c->selectionController->GetActiveLine();
    if (!cur) return nullptr;
    auto& ev = c->ass->Events;
    auto it = std::find_if(ev.begin(), ev.end(),
        [&](AssDialogue const& d){ return &d == cur; });
    if (it == ev.end()) return nullptr;
    if (prev) {
        if (it == ev.begin()) return nullptr;
        return &*std::prev(it);
    } else {
        auto nx = std::next(it);
        if (nx == ev.end()) return nullptr;
        return &*nx;
    }
}

struct join_last final : Command {
    CMD_NAME("edit/line/join/last")
    STR_MENU("Join last")
    STR_DISP("Join last")
    STR_HELP("Join the active line with the previous line (concatenate).")
    bool Validate(const agi::Context* c) override {
        return c->selectionController->GetActiveLine()
            && find_neighbor(const_cast<agi::Context*>(c), true);
    }
    void operator()(agi::Context* c) override {
        auto* cur = c->selectionController->GetActiveLine();
        auto* prev = find_neighbor(c, true);
        if (!cur || !prev) return;
        Selection sel; sel.insert(prev); sel.insert(cur);
        c->selectionController->SetSelectionAndActive(sel, cur);
        cmd::call("edit/line/join/concatenate", c);
    }
};

struct join_next final : Command {
    CMD_NAME("edit/line/join/next")
    STR_MENU("Join next")
    STR_DISP("Join next")
    STR_HELP("Join the active line with the next line (concatenate).")
    bool Validate(const agi::Context* c) override {
        return c->selectionController->GetActiveLine()
            && find_neighbor(const_cast<agi::Context*>(c), false);
    }
    void operator()(agi::Context* c) override {
        auto* cur = c->selectionController->GetActiveLine();
        auto* next = find_neighbor(c, false);
        if (!cur || !next) return;
        Selection sel; sel.insert(cur); sel.insert(next);
        c->selectionController->SetSelectionAndActive(sel, cur);
        cmd::call("edit/line/join/concatenate", c);
    }
};

static cmd::reg<join_last> reg_join_last;
static cmd::reg<join_next> reg_join_next;

} // namespace
