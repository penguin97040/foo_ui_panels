#pragma once
#include <string>
#include <vector>

namespace pui {

struct Button {
    int x = 0, y = 0, w = 0, h = 0;
    std::string action;
    std::string tooltip;

    std::string pvarKey, pvarValue, litImage;
    int litW = 0, litH = 0;
    bool selected = false;
};

struct Placement {
    std::string name, type;
    int x = 0, y = 0, w = 0, h = 0;
};

inline bool button_hit(const Button& b, int x, int y) {
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// The button the pointer is over (the last one drawn wins), as a rectangle; w == 0 for none. The hover
// rest clock (HOVERDELAY) follows this rectangle, not the button's place in the list, which can shift
// from one paint to the next.
struct HoverTarget {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const HoverTarget& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const HoverTarget& o) const { return !(*this == o); }
};
inline HoverTarget hover_target_at(const std::vector<Button>& list, int x, int y) {
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (button_hit(*it, x, y)) return { it->x, it->y, it->w, it->h };
    return {};
}

inline std::string tooltip_at(const std::vector<Button>& list, int x, int y) {
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (!it->tooltip.empty() && button_hit(*it, x, y)) return it->tooltip;
    return {};
}

}
