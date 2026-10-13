#include "list_logic.h"
#include "../ui/keys.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>

namespace pui {

int clamp_scroll(int scroll, int content_h, int view_h) {
    const int maxs = std::max(0, content_h - view_h);
    return std::max(0, std::min(scroll, maxs));
}

int scroll_into_view(int scroll, int top, int bottom, int view_h) {
    if (top < scroll) return top;
    if (bottom > scroll + view_h) return std::min(top, bottom - view_h);
    return scroll;
}

ScrollThumb scroll_thumb(int scroll, int content_h, int view_h, int min_h) {
    ScrollThumb t;
    if (view_h <= 0 || content_h <= view_h) return t;
    t.visible = true;
    t.h = std::min(view_h, std::max(min_h, (int)((long long)view_h * view_h / content_h)));
    const int range = content_h - view_h;
    t.y = (int)((long long)(view_h - t.h) * std::max(0, std::min(scroll, range)) / range);
    return t;
}

double ease_toward(double cur, double target, double rate, double snap, bool& settled) {
    const double d = target - cur;
    if (std::abs(d) < snap) return target;
    settled = false;
    return cur + d * rate;
}

int list_nav(int key, int cur, int count, int page) {
    if (count <= 0) return -1;
    int t;
    switch (key) {
    case ui::kKeyUp:       t = cur < 0 ? 0 : cur - 1; break;
    case ui::kKeyDown:     t = cur + 1; break;
    case ui::kKeyPageUp:   t = cur < 0 ? 0 : cur - page; break;
    case ui::kKeyPageDown: t = cur + page; break;
    case ui::kKeyHome:     t = 0; break;
    case ui::kKeyEnd:      t = count - 1; break;
    default: return -1;
    }
    return std::clamp(t, 0, count - 1);
}

int grid_nav(int key, int sel, int count, int cols, int page) {
    if (count <= 0) return -1;
    const int cur = sel < 0 ? 0 : sel;
    int t;
    switch (key) {
    case ui::kKeyLeft:     t = cur - 1; break;
    case ui::kKeyRight:    t = sel < 0 ? 0 : cur + 1; break;
    case ui::kKeyUp:       t = cur - cols; break;
    case ui::kKeyDown:     t = sel < 0 ? 0 : cur + cols; break;
    case ui::kKeyPageUp:   t = cur - page; break;
    case ui::kKeyPageDown: t = cur + page; break;
    case ui::kKeyHome:     t = 0; break;
    case ui::kKeyEnd:      t = count - 1; break;
    default: return -1;
    }
    return std::clamp(t, 0, count - 1);
}

bool typeahead_key(int key, bool have_text) {
    return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key == ' ' && have_text);
}

bool starts_with_upper(const std::string& s, const std::string& p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (toupper((unsigned char)s[i]) != (unsigned char)p[i]) return false;
    return true;
}

int typeahead_find(size_t n, size_t focus, size_t typed_len, const std::function<bool(size_t)>& match) {
    if (!n) return -1;
    if (focus >= n) focus = 0;
    const size_t first = typed_len == 1 ? focus + 1 : focus;
    for (size_t k = 0; k < n; ++k) {
        const size_t i = (first + k) % n;
        if (match(i)) return (int)i;
    }
    return -1;
}

void GroupedRows::set_groups(const std::vector<size_t>& sizes) {
    m_start.clear();
    m_count = 0;
    for (size_t s : sizes) {
        if (!s) continue;
        m_start.push_back(m_count);
        m_count += s;
    }
}

std::vector<size_t> GroupedRows::runs(const std::vector<std::string>& keys) {
    std::vector<size_t> out;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i == 0 || keys[i] != keys[i - 1]) out.push_back(0);
        ++out.back();
    }
    return out;
}

size_t GroupedRows::group_size(size_t g) const {
    return (g + 1 < m_start.size() ? m_start[g + 1] : m_count) - m_start[g];
}

size_t GroupedRows::group_of(int idx) const {
    auto it = std::upper_bound(m_start.begin(), m_start.end(), (size_t)idx);
    return (size_t)(it - m_start.begin()) - 1;
}

size_t GroupedRows::group_at(int y) const {
    size_t lo = 0, hi = m_start.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (group_top(mid) <= y) lo = mid + 1; else hi = mid;
    }
    return lo ? lo - 1 : 0;
}

int GroupedRows::item_at(int y) const {
    if (m_start.empty() || y < 0) return -1;
    const size_t g = group_at(y);
    const int off = y - group_top(g) - m_header;
    if (off < 0) return -1;
    const size_t i = (size_t)(off / m_row);
    return i < group_size(g) ? (int)(m_start[g] + i) : -1;
}

int GroupedRows::item_top(int idx) const {
    if (idx < 0 || (size_t)idx >= m_count) return -1;
    return (int)(group_of(idx) + 1) * m_header + idx * m_row;
}

bool GroupedRows::is_group_first(int idx) const {
    return idx >= 0 && (size_t)idx < m_count && m_start[group_of(idx)] == (size_t)idx;
}

int GroupedRows::drop_index_at(int y) const {
    if (m_start.empty() || y < 0) return 0;
    const size_t g = group_at(y);
    const int top = group_top(g);
    if (y < top + m_header) return (int)m_start[g];
    const size_t i = (size_t)((y - top - m_header + (m_row - m_row / 2)) / m_row);
    return (int)(m_start[g] + std::min(i, group_size(g)));
}

int GroupedRows::reveal(int scroll, int idx, int view_h) const {
    const int top = item_top(idx);
    if (top < 0) return scroll;
    const int want = is_group_first(idx) ? top - m_header : top;
    if (want < scroll) return want;
    if (top + m_row > scroll + view_h) return top + m_row - view_h;
    return scroll;
}

StarStrip star_strip(int view_w) { return StarStrip{ view_w - 106, 55 }; }

int star_at(const StarStrip& s, int x) {
    if (x < s.x || x >= s.x + s.w || s.w <= 0) return 0;
    return std::clamp((x - s.x) * 5 / s.w + 1, 1, 5);
}

int TreeRows::add(int parent, std::string label, int kind, int a, int b) {
    TreeItem it;
    it.label = std::move(label);
    it.kind = kind; it.a = a; it.b = b;
    it.depth = parent < 0 ? 0 : items[parent].depth + 1;
    items.push_back(std::move(it));
    const int idx = (int)items.size() - 1;
    if (parent < 0) roots.push_back(idx); else items[parent].children.push_back(idx);
    return idx;
}

void TreeRows::rebuild_rows() {
    rows.clear();
    std::function<void(int)> walk = [&](int i) {
        rows.push_back(i);
        if (items[i].expanded) for (int c : items[i].children) walk(c);
    };
    for (int r : roots) walk(r);
}

int TreeRows::row_of(int item) const {
    for (int r = 0; r < (int)rows.size(); ++r) if (rows[r] == item) return r;
    return -1;
}

bool TreeRows::toggle(int item) {
    if (item < 0 || item >= (int)items.size() || items[item].children.empty()) return false;
    items[item].expanded = !items[item].expanded;
    rebuild_rows();
    return true;
}

int TreeRows::parent_row(int row) const {
    if (row < 0 || row >= (int)rows.size()) return -1;
    const int depth = items[rows[row]].depth;
    for (int r = row - 1; r >= 0; --r) if (items[rows[r]].depth < depth) return r;
    return -1;
}

bool TreeRows::horizontal_key(int key, int row, int& select_row, int& toggle_item) const {
    select_row = toggle_item = -1;
    if ((key != ui::kKeyLeft && key != ui::kKeyRight) || row < 0 || row >= (int)rows.size()) return false;
    const int item = rows[row];
    const TreeItem& it = items[item];
    if (key == ui::kKeyRight) {
        if (it.children.empty()) return true;
        if (!it.expanded) toggle_item = item;
        else select_row = std::min(row + 1, (int)rows.size() - 1);
    } else {
        if (it.expanded) toggle_item = item;
        else select_row = parent_row(row);
    }
    return true;
}

int fixed_row_at(int y, int scroll, int row_h, int rows) {
    if (y < 0 || row_h <= 0) return -1;
    const int r = (y + scroll) / row_h;
    return r >= 0 && r < rows ? r : -1;
}

SplitSections split_sections(int h, int permille, int bar_h, int min_h) {
    SplitSections s;
    s.bar_h = std::max(0, std::min(bar_h, h));
    const int avail = std::max(0, h - s.bar_h);
    int top = (int)((long long)avail * std::clamp(permille, 0, 1000) / 1000);
    if (avail >= 2 * min_h) top = std::clamp(top, min_h, avail - min_h);
    else top = avail / 2;
    s.top_h = top;
    s.bar_y = top;
    s.bottom_y = top + s.bar_h;
    s.bottom_h = avail - top;
    return s;
}

int split_permille(int h, int bar_y, int bar_h) {
    const int avail = h - bar_h;
    if (avail <= 0) return 500;
    return (int)std::clamp((long long)bar_y * 1000 / avail, 0LL, 1000LL);
}

GridLayout album_grid(int client_w, int case_w, int case_h) {
    GridLayout g;
    if (case_w <= 0 || case_h <= 0) case_w = case_h = 1;
    g.cols = std::max(1, std::min(8, (client_w - g.margin * 2 + g.gutter) / (100 + g.gutter)));
    g.cellW = (client_w - g.margin * 2 - (g.cols - 1) * g.gutter) / g.cols;
    g.cellH = (g.cellW - 10) * case_h / case_w + 10 + 32;
    return g;
}

int GridLayout::content_height(int count) const {
    const int rows = (count + cols - 1) / cols;
    return margin * 2 + rows * (cellH + gutter);
}

int GridLayout::page(int view_h) const { return cols * std::max(1, view_h / (cellH + gutter)); }

int GridLayout::item_at(int x, int y, int count) const {
    const int gx = x - margin, gy = y - margin;
    if (gx < 0 || gy < 0) return -1;
    const int col = gx / (cellW + gutter), row = gy / (cellH + gutter);
    if (col >= cols) return -1;
    if (gx % (cellW + gutter) > cellW || gy % (cellH + gutter) > cellH) return -1;
    const int idx = row * cols + col;
    return idx < count ? idx : -1;
}

int GridLayout::reveal(int scroll, int idx, int view_h) const {
    if (idx < 0) return scroll;
    const int top = cell_y(idx), bottom = top + cellH;
    if (top < scroll) return std::max(0, top - margin);
    if (bottom > scroll + view_h) return bottom - view_h + margin;
    return scroll;
}

CoverFlow CoverFlow::fit(int w, int h) {
    CoverFlow f;
    f.size = std::min(h * 0.48f, w * 0.32f);
    f.cx = w * 0.5f;
    f.yMid = h * 0.38f;
    f.sideOff = f.size * 0.68f;
    f.spacing = f.size * 0.20f;
    return f;
}

FlowSlot CoverFlow::slot(float d) const {
    const float ad = std::fabs(d), t = std::min(ad, 1.0f), sign = d < 0 ? -1.0f : 1.0f;
    const float xc = cx + sign * (sideOff * t + spacing * std::max(ad - 1.0f, 0.0f));
    const float w = size * (1.0f - 0.62f * t);
    const float hOuter = size * (1.0f - 0.08f * t), hInner = size * (1.0f - 0.30f * t);
    FlowSlot s;
    s.x0 = xc - w / 2; s.x1 = xc + w / 2;
    s.h0 = d < 0 ? hOuter : hInner;
    s.h1 = d < 0 ? hInner : hOuter;
    return s;
}

bool coverflow_step(float& pos, int target) {
    const float diff = (float)target - pos;
    if (std::fabs(diff) < 0.004f) { pos = (float)target; return true; }
    pos += diff * 0.22f;
    return false;
}

static std::string lower_ascii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string primary_artist(const std::string& artist) {
    static const char* const kMarks[] = { " feat. ", " feat ", " ft. ", " ft ", " featuring ",
                                          "(feat", "[feat", "(ft.", "[ft.", "(featuring", "[featuring" };
    const std::string low = lower_ascii(artist);
    size_t cut = std::string::npos;
    for (const char* m : kMarks) cut = std::min(cut, low.find(m));
    const std::string out = trim(cut == std::string::npos ? artist : artist.substr(0, cut));
    return out.empty() ? trim(artist) : out;
}

static bool all_same(const std::vector<std::string>& v, std::string& first) {
    first.clear();
    for (auto& s : v) {
        if (s.empty()) continue;
        if (first.empty()) first = s;
        else if (lower_ascii(s) != lower_ascii(first)) return false;
    }
    return true;
}

std::string group_artist(const std::vector<std::string>& album_artists, const std::vector<std::string>& artists) {
    std::string a;
    if (all_same(album_artists, a) && !a.empty()) return a;
    std::vector<std::string> prim;
    prim.reserve(artists.size());
    for (auto& s : artists) prim.push_back(primary_artist(s));
    if (all_same(prim, a)) return a;
    return "Various artists";
}

std::vector<size_t> album_sort_order(const std::vector<AlbumSortTrack>& t) {
    std::vector<std::string> keys;
    std::vector<std::vector<size_t>> groups;
    for (size_t i = 0; i < t.size(); ++i) {
        size_t g = 0;
        while (g < keys.size() && keys[g] != t[i].group) ++g;
        if (g == keys.size()) { keys.push_back(t[i].group); groups.emplace_back(); }
        groups[g].push_back(i);
    }
    struct G { std::string sort; std::vector<size_t> idx; };
    std::vector<G> gs;
    gs.reserve(groups.size());
    for (auto& idx : groups) {
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return lower_ascii(t[a].in_group) < lower_ascii(t[b].in_group);
        });
        std::vector<std::string> aa, ar;
        for (size_t i : idx) { aa.push_back(t[i].album_artist); ar.push_back(t[i].artist); }
        gs.push_back({ lower_ascii(group_artist(aa, ar)) + '\x01' + lower_ascii(t[idx[0]].group_rest), idx });
    }
    std::stable_sort(gs.begin(), gs.end(), [](const G& a, const G& b) { return a.sort < b.sort; });
    std::vector<size_t> order;
    order.reserve(t.size());
    for (auto& g : gs) order.insert(order.end(), g.idx.begin(), g.idx.end());
    for (size_t i = 0; i < order.size(); ++i) if (order[i] != i) return order;
    return {};
}

bool output_device_exclusive(const std::string& name) {
    const std::string low = lower_ascii(name);
    return low.find("[exclusive]") != std::string::npos || low.find("(exclusive)") != std::string::npos;
}

std::string output_device_base(const std::string& name) {
    std::string low = lower_ascii(name);
    for (const char* m : { "[exclusive]", "(exclusive)" }) {
        const size_t p = low.find(m);
        if (p != std::string::npos) low.erase(p, strlen(m));
    }
    const size_t colon = low.find(':');
    if (low.compare(0, 7, "default") == 0 && colon != std::string::npos) low.erase(0, colon + 1);
    return trim(low);
}

static bool nameless_default(const std::string& base) {
    return base == "default" || base == "primary sound driver" || base.empty();
}

int output_device_twin(const std::vector<std::string>& names, int current) {
    if (current < 0 || (size_t)current >= names.size()) return -1;
    const bool excl = output_device_exclusive(names[(size_t)current]);
    const std::string base = output_device_base(names[(size_t)current]);
    if (!excl && nameless_default(base)) {
        int only = -1;
        for (size_t i = 0; i < names.size(); ++i)
            if (output_device_exclusive(names[i])) { if (only >= 0) return -1; only = (int)i; }
        return only;
    }
    int fallback = -1;
    for (size_t i = 0; i < names.size(); ++i) {
        if ((int)i == current || output_device_exclusive(names[i]) == excl) continue;
        if (output_device_base(names[i]) != base) continue;
        // Back from exclusive, prefer the plain entry over a "Default : <same device>" one.
        if (lower_ascii(names[i]).compare(0, 7, "default") != 0) return (int)i;
        if (fallback < 0) fallback = (int)i;
    }
    return fallback;
}

}
