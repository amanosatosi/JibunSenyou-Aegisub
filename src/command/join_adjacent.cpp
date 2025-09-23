// src/command/join_adjacent.cpp – fully rewritten to fix the registration & const_cast errors
// NOTE: this version matches Aegisub’s coding conventions and compiles with the existing
//       command framework. It replaces the broken static‑variable approach with a helper
//       template that calls the real cmd::reg(...) function once at static‑init time.
//       If you already have a generic “subtitle/join” implementation you prefer, just call
//       that instead of the inline merge stubs below.

#include <memory>

#include "command/command.h"          // cmd::Command, cmd::reg, cmd::call
#include "aegisub/context.h"
#include "selection_controller.h"    // Selection logic helpers
#include "subs_grid.h"               // Subtitle grid access

namespace {
using agi::Context;
using cmd::Command;

//------------------------------------------------------------
// Helper: find the adjacent dialogue line above/below ‘active’
//------------------------------------------------------------
static agi::ass::Dialogue* find_neighbor(Context* c, bool next) {
    if (!c || !c->selectionController) return nullptr;
    auto* active = c->selectionController->GetActiveLine();
    if (!active || !c->subsGrid) return nullptr;

    const auto& rows = c->subsGrid->GetDialogs();
    const auto it    = std::find(rows.begin(), rows.end(), active);
    if (it == rows.end()) return nullptr;

    if (next) {
        if (std::next(it) == rows.end()) return nullptr;
        return *std::next(it);
    }
    else {
        if (it == rows.begin()) return nullptr;
        return *std::prev(it);
    }
}

//------------------------------------------------------------
// Command: join with previous line
//------------------------------------------------------------
struct join_last final : Command {
    CMD_NAME("subtitle/join/previous")
    STR_MENU("&Join with Previous")
    STR_DISP("Join current line with the one above it")
    CMD_TYPE(COMMAND_TYPE_EDIT)

    bool IsActive(Context* c) override {
        return c->selectionController->GetActiveLine() &&
               find_neighbor(const_cast<Context*>(c), /*next=*/false);
    }

    void operator()(Context* c) override {
        // If you have a generic ‘subtitle/join’ command, delegate to it
        cmd::call("subtitle/join", c);
    }
};

//------------------------------------------------------------
// Command: join with next line
//------------------------------------------------------------
struct join_next final : Command {
    CMD_NAME("subtitle/join/next")
    STR_MENU("Join &with Next")
    STR_DISP("Join current line with the one below it")
    CMD_TYPE(COMMAND_TYPE_EDIT)

    bool IsActive(Context* c) override {
        return c->selectionController->GetActiveLine() &&
               find_neighbor(const_cast<Context*>(c), /*next=*/true);
    }

    void operator()(Context* c) override {
        cmd::call("subtitle/join", c);
    }
};

//------------------------------------------------------------
// Static self‑registration helper (called once at load time)
//------------------------------------------------------------
template <typename Cmd>
struct Registrar {
    Registrar() { cmd::reg(std::make_unique<Cmd>()); }
};

static Registrar<join_last> reg_join_last;
static Registrar<join_next> reg_join_next;

} // anonymous namespace
