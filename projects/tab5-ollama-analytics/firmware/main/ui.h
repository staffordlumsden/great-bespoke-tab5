#pragma once
#include "ollama_analytics.h"
namespace oa {
struct Window { bool known = false; double used = 0; int64_t resets_at = 0; };
struct Quota { Window five_hour, seven_day; };
struct View {
    Models models;
    ou::Balance balance;
    int64_t now = 0;
    bool has_models = false, has_balance = false, dark = true;
    bool configured = false, wifi = false, clock_ready = false, busy = false;
    bool models_stale = false, balance_stale = false;
    char status[64] = "Add your Ollama key on SD to connect";
};
struct Painter {
    void *context;
    void (*rect)(void *, int, int, int, int, uint32_t);
};
enum class Action { none, refresh, theme, console };
enum Region { full = 1, ollama = 2, footer = 4, quota = 8 };
void draw(const View &view, Painter painter, unsigned regions);
Action hit(int x, int y);
} // namespace oa
