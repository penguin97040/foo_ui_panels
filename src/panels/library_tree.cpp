#include "library_tree.h"
#include "../core/skin_engine.h"
#include "../core/log.h"
#include "../core/navidrome_library_api.h"
#include "../core/image_cache.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace pui {

static const int kIndent = 18, kPad = 4;

// A skin sets the tree's face and pixel size with font.library_tree[.size]; rows grow with the size.
gfx::FontSpec LibraryTree::font() const {
    const gfx::FontSpec def{ "Segoe UI", 13, false };
    return m_engine ? m_engine->panel_font("library_tree", def) : def;
}

int LibraryTree::text_row_h() const { return std::max(20, (int)std::lround(20 * font().size / 13.0)); }

// library_tree.art_size (px) puts artist pictures and album covers beside the names; rows grow to fit.
int LibraryTree::art_size() const {
    return m_engine ? std::clamp(m_engine->config().num("library_tree.art_size", 0), 0, 96) : 0;
}

int LibraryTree::row_h() const {
    const int art = art_size();
    return art > 0 ? std::max(text_row_h(), art + 6) : text_row_h();
}

bool LibraryTree::split() const {
    return m_engine && m_engine->config().str("library_tree.layout") == "split";
}

gfx::FontSpec LibraryTree::header_font() const {
    return m_engine ? m_engine->panel_font("library_tree.header", font()) : font();
}

int LibraryTree::bar_h() const { return std::max(text_row_h(), (int)std::lround(header_font().size * 2.2)); }

// The artists area (top) and, when split, the bar and the playlists area below it.
SplitSections LibraryTree::sections() const {
    const int H = host() ? host()->bounds().h : 0;
    if (!split()) {
        SplitSections s;
        s.top_h = s.bar_y = s.bottom_y = H;
        return s;
    }
    return split_sections(H, m_split, bar_h(), 2 * row_h());
}

void LibraryTree::save_split() {
    if (m_engine) m_engine->set_view_state("library_tree.split", std::to_string(m_split));
}

static std::string lower_str(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static navidrome::navidrome_library_api::ptr navidrome_api() {
    service_enum_t<navidrome::navidrome_library_api> e;
    navidrome::navidrome_library_api::ptr api;
    e.next(api);
    return api;
}

static pfc::string8 fmt(const metadb_handle_ptr& h, const titleformat_object::ptr& obj) {
    pfc::string8 o; if (h.is_valid()) h->format_title(nullptr, o, obj, nullptr);
    return o;
}

void LibraryTree::on_attached() {
    if (m_engine) {
        const std::string v = m_engine->view_state("library_tree.split");
        if (!v.empty()) m_split = std::clamp(atoi(v.c_str()), 0, 1000);
    }
    if (!m_libEvents) m_libEvents = std::make_unique<LibEvents>(this);
    if (!m_plEvents) m_plEvents = std::make_unique<PlEvents>(this);
    load_local();
    start_remote_load();
    finish_data();
    m_loaded = true;
}

void LibraryTree::on_destroy() {
    m_libEvents.reset();
    m_plEvents.reset();
    m_alive->store(false);
    m_abort->abort();
}

void LibraryTree::on_visibility(bool shown) {
    if (shown && m_loaded) refresh_tree();
}

LibraryTree::Artist& LibraryTree::artist_for(const std::string& name) {
    const std::string k = lower_str(name);
    for (auto& a : m_artists) if (lower_str(a.name) == k) return a;
    m_artists.push_back({ name, "", {} });
    return m_artists.back();
}

void LibraryTree::on_timer(int id) {
    if (id != kTimerReload) return;
    host()->kill_timer(kTimerReload);
    load_local();
    start_remote_load();
    finish_data();
}

// Playlists were added, removed, renamed or switched, or their track counts changed.
void LibraryTree::playlists_changed() {
    if (split()) { clamp_pl_scroll(); invalidate(); }
    else if (m_loaded) refresh_tree();
}

void LibraryTree::load_local() {
    m_artists.clear();
    metadb_handle_list all;
    library_manager::get()->get_all_items(all);
    titleformat_object::ptr tfAlbumArtist, tfArtist, tfAlbum;
    auto tc = titleformat_compiler::get();
    tc->compile_safe(tfAlbumArtist, "[%album artist%]");
    tc->compile_safe(tfArtist, "[%artist%]");
    tc->compile_safe(tfAlbum, "[%album%]");
    for (t_size i = 0; i < all.get_count(); ++i) {
        std::string artist = fmt(all[i], tfAlbumArtist).c_str();
        if (artist.empty()) artist = fmt(all[i], tfArtist).c_str();
        if (artist.empty()) artist = "Unknown artist";
        std::string album = fmt(all[i], tfAlbum).c_str();
        if (album.empty()) album = "(no album)";
        Artist& ar = artist_for(artist);
        Album* found = nullptr;
        for (auto& al : ar.albums) if (!al.remote && lower_str(al.name) == lower_str(album)) { found = &al; break; }
        if (!found) { ar.albums.push_back({ album, "", false, {} }); found = &ar.albums.back(); }
        found->items.push_back(all[i]);
    }
}

void LibraryTree::start_remote_load() {
    ++m_gen;
    m_remote_err.clear(); m_remote_loading = false;
    auto api = navidrome_api();
    if (api.is_empty()) return;
    if (!api->is_configured()) return;
    m_remote_loading = true;
    const unsigned gen = m_gen;
    auto alive = m_alive; auto abort = m_abort; LibraryTree* self = this;
    fb2k::splitTask([=]() {
        struct Sink : navidrome::library_album_sink {
            std::vector<RemoteAlbum> all;
            void on_album(const char* albumId, const char* albumName, const char* artistName,
                          const char* artistId, const char*, int, int) override {
                all.push_back({ artistName, albumName, albumId, artistId });
            }
        } sink;
        pfc::string8 err; bool ok = false;
        try { ok = api->list_albums(sink, *abort, err); } catch (...) { err = "Navidrome library request failed."; }
        auto shared = std::make_shared<std::vector<RemoteAlbum>>(std::move(sink.all));
        std::string errs = ok ? std::string() : std::string(err.c_str());
        if (!ok) log::warn("navidrome", "album list request failed: " + errs);
        else log::info("navidrome", "album list loaded");
        fb2k::inMainThread([=]() {
            if (!alive->load() || self->m_gen != gen) return;
            self->m_remote_loading = false; self->m_remote_err = errs;
            self->merge_remote(std::move(*shared));
        });
    });
}

void LibraryTree::merge_remote(std::vector<RemoteAlbum>&& add) {
    for (auto& r : add) {
        Artist& ar = artist_for(r.artist);
        if (ar.remote_id.empty()) ar.remote_id = r.artist_id;
        bool have = false;
        for (auto& al : ar.albums) if (lower_str(al.name) == lower_str(r.album)) { have = true; break; }
        if (!have) ar.albums.push_back({ r.album, r.id, true, {} });
    }
    finish_data();
}

void LibraryTree::finish_data() {
    auto byName = [](const std::string& a, const std::string& b) { return lower_str(a) < lower_str(b); };
    for (auto& ar : m_artists)
        std::stable_sort(ar.albums.begin(), ar.albums.end(), [&](const Album& a, const Album& b) { return byName(a.name, b.name); });
    std::stable_sort(m_artists.begin(), m_artists.end(), [&](const Artist& a, const Artist& b) { return byName(a.name, b.name); });
    refresh_tree();
}

std::string LibraryTree::item_key(int i) const {
    const TreeItem& it = m_tree.items[i];
    switch (it.kind) {
    case kPlaylists: return "P";
    case kPlaylist:  return "p:" + it.label;
    case kArtist:    return "a:" + m_artists[it.a].name;
    case kAlbum:     return "b:" + m_artists[it.a].name + "|" + m_artists[it.a].albums[it.b].name;
    default:         return "l:" + it.label;
    }
}

void LibraryTree::refresh_tree() {
    const std::string selKey = m_sel >= 0 && m_sel < (int)m_tree.items.size() ? item_key(m_sel) : std::string();
    m_tree.clear();
    m_tree.items.reserve(m_artists.size() * 8 + 64);
    auto add = [&](int parent, std::string text, Kind kind, int a = -1, int b = -1) {
        return m_tree.add(parent, std::move(text), kind, a, b);
    };

    if (!split()) {   // split: the playlists have their own section below the bar
        auto pm = playlist_manager::get();
        const t_size np = pm->get_playlist_count();
        int pls = add(-1, "Playlists", kPlaylists);
        for (t_size i = 0; i < np; ++i) {
            pfc::string8 nm; pm->playlist_get_name(i, nm);
            add(pls, std::string(nm.c_str()) + "  (" + std::to_string(pm->playlist_get_item_count(i)) + ")", kPlaylist, (int)i);
        }
    }
    if (m_remote_loading) add(-1, "Loading Navidrome library...", kLoading);
    else if (!m_remote_err.empty()) add(-1, m_remote_err, kLoading);
    if (m_artists.empty() && !m_remote_loading)
        add(-1, "No music found (Media Library empty, Navidrome not available)", kLoading);

    for (size_t a = 0; a < m_artists.size(); ++a) {
        const Artist& ar = m_artists[a];
        int ai = add(-1, ar.name + "  (" + std::to_string(ar.albums.size()) + ")", kArtist, (int)a);
        for (size_t b = 0; b < ar.albums.size(); ++b)
            add(ai, ar.albums[b].name, kAlbum, (int)a, (int)b);
    }
    m_sel = -1;
    for (int i = 0; i < (int)m_tree.items.size(); ++i) {
        const std::string k = item_key(i);
        if (m_expandedKeys.count(k)) m_tree.items[i].expanded = true;
        if (!selKey.empty() && k == selKey) m_sel = i;
    }
    m_tree.rebuild_rows();
    clamp_scroll();
    invalidate();
}

void LibraryTree::clamp_scroll() {
    m_scroll = pui::clamp_scroll(m_scroll, (int)m_tree.rows.size() * row_h(), sections().top_h);
    clamp_pl_scroll();
}

void LibraryTree::clamp_pl_scroll() {
    if (!split()) { m_plScroll = 0; return; }
    const int n = (int)playlist_manager::get()->get_playlist_count();
    m_plScroll = pui::clamp_scroll(m_plScroll, n * text_row_h(), sections().bottom_h);
}

int LibraryTree::row_at(int y) const {
    if (y >= sections().top_h) return -1;
    return fixed_row_at(y, m_scroll, row_h(), (int)m_tree.rows.size());
}

void LibraryTree::select_row(int row) {
    if (m_tree.rows.empty()) return;
    row = std::max(0, std::min((int)m_tree.rows.size() - 1, row));
    m_sel = m_tree.rows[row];
    m_scroll = scroll_into_view(m_scroll, row * row_h(), (row + 1) * row_h(), sections().top_h);
    clamp_scroll();
    invalidate();
}

void LibraryTree::toggle(int item) {
    if (!m_tree.toggle(item)) return;
    if (m_tree.items[item].expanded) m_expandedKeys.insert(item_key(item)); else m_expandedKeys.erase(item_key(item));
    clamp_scroll();
    invalidate();
}

// A picture beside an artist (their own, else an album cover) or an album (its cover); an empty frame
// while it loads or when there's none.
void LibraryTree::draw_art(gfx::Canvas& cv, const TreeItem& it, int x, int y, int side, gfx::Color frame) {
    gfx::ImagePtr img;
    if (it.kind == kArtist) {
        for (const Album& al : m_artists[it.a].albums)
            if (!al.items.empty()) { img = artist_image(al.items[0]); break; }
    } else if (it.kind == kAlbum) {
        const Album& al = m_artists[it.a].albums[it.b];
        if (!al.items.empty()) img = cover_image(std::string(), al.items[0]);
    }
    if (img) draw_square_image(cv, img, x, y, side);
    else cv.frame_rect(gfx::Rect{ x, y, side, side }, frame);
}

void LibraryTree::paint(gfx::Canvas& cv) {
    const int W = cv.width(), H = cv.height();
    auto col = [this](const char* role, gfx::Color def) { return m_engine ? m_engine->color("library_tree", role, def) : def; };
    const gfx::Color bg = col("background", gfx::Color(20, 21, 26)), text = col("text", gfx::Color(228, 231, 240)),
                     lines = col("separator", gfx::Color(80, 85, 100));
    gfx::Color accent(0, 120, 215);
    if (m_engine && !m_engine->configured_color("library_tree", "highlight", accent)) m_engine->theme_color(accent);
    gfx::Color mark = accent;
    if (m_engine) m_engine->theme_color(mark);
    const SplitSections L = sections();
    const int kRowH = row_h(), art = art_size();
    cv.fill_rect(gfx::Rect{ 0, 0, W, H }, bg);
    cv.set_font(font());
    const int first = m_scroll / kRowH;
    for (int r = first; r < (int)m_tree.rows.size(); ++r) {
        const int y = r * kRowH - m_scroll;
        if (y >= L.top_h) break;
        const TreeItem& it = m_tree.items[m_tree.rows[r]];
        const int x = kPad + it.depth * kIndent;
        if (m_tree.rows[r] == m_sel) cv.fill_rect_alpha(gfx::Rect{ 0, y, W, kRowH }, accent, 150);
        if (!it.children.empty()) {
            const int bx = x, by = y + (kRowH - 9) / 2;
            cv.frame_rect(gfx::Rect{ bx, by, 9, 9 }, lines);
            cv.line(bx + 2, by + 4, bx + 7, by + 4, text);
            if (!it.expanded) cv.line(bx + 4, by + 2, bx + 4, by + 7, text);
        }
        int tx = x + 14;
        if (art > 0 && (it.kind == kArtist || it.kind == kAlbum)) {
            draw_art(cv, it, tx, y + (kRowH - art) / 2, art, lines);
            tx += art + 8;
        }
        cv.draw_text(it.label, gfx::Rect{ tx, y, W - tx - kPad, kRowH },
                     gfx::kSingleLine | gfx::kVCenter | gfx::kEndEllipsis, text);
    }
    const ScrollThumb th = scroll_thumb(m_scroll, (int)m_tree.rows.size() * kRowH, L.top_h);
    if (th.visible) cv.fill_rect_alpha(gfx::Rect{ W - 5, th.y, 4, th.h }, lines, 200);
    if (!split()) return;

    // Playlists: the active one highlighted like a selected artist, the playing one ticked.
    auto pm = playlist_manager::get();
    const int n = (int)pm->get_playlist_count(), ph = text_row_h();
    const t_size active = pm->get_active_playlist(), playing = pm->get_playing_playlist();
    const gfx::Color dim = col("text_dim", gfx::Color(150, 160, 180));
    cv.fill_rect(gfx::Rect{ 0, L.bottom_y, W, L.bottom_h }, bg);
    for (int i = std::max(0, m_plScroll / ph); i < n; ++i) {
        const int y = L.bottom_y + i * ph - m_plScroll;
        if (y >= H) break;
        if ((t_size)i == active) cv.fill_rect_alpha(gfx::Rect{ 0, y, W, ph }, accent, 150);
        if ((t_size)i == playing) cv.fill_rect(gfx::Rect{ kPad, y + (ph - 10) / 2, 3, 10 }, mark);
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        const std::string cnt = std::to_string(pm->playlist_get_item_count(i));
        const int cw = cv.text_width(cnt);
        cv.draw_text(cnt, gfx::Rect{ W - kPad - 6 - cw, y, cw, ph }, gfx::kSingleLine | gfx::kVCenter | gfx::kAlignRight,
                     (t_size)i == active ? text : dim);
        cv.draw_text(nm.c_str(), gfx::Rect{ kPad + 14, y, W - kPad - 14 - cw - 16, ph },
                     gfx::kSingleLine | gfx::kVCenter | gfx::kEndEllipsis, text);
    }
    const ScrollThumb pt = scroll_thumb(m_plScroll, n * ph, L.bottom_h);
    if (pt.visible) cv.fill_rect_alpha(gfx::Rect{ W - 5, L.bottom_y + pt.y, 4, pt.h }, lines, 200);

    // The bar: framed by 1 px lines, a tick and the PLAYLISTS label, the count, and a grip that lights
    // while hovered or dragged.
    cv.fill_rect(gfx::Rect{ 0, L.bar_y, W, L.bar_h }, bg);
    cv.fill_rect(gfx::Rect{ 0, L.bar_y, W, 1 }, lines);
    cv.fill_rect(gfx::Rect{ 0, L.bottom_y - 1, W, 1 }, lines);
    const gfx::FontSpec hf = header_font();
    const int tickH = std::max(6, (int)std::lround(hf.size * 0.75));
    cv.fill_rect(gfx::Rect{ 12, L.bar_y + (L.bar_h - tickH) / 2, 3, tickH }, mark);
    cv.set_font(hf);
    cv.draw_text("PLAYLISTS", gfx::Rect{ 22, L.bar_y, W / 2, L.bar_h }, gfx::kSingleLine | gfx::kVCenter,
                 col("header", text));
    cv.draw_text(std::to_string(n), gfx::Rect{ W / 2, L.bar_y, W / 2 - 12, L.bar_h },
                 gfx::kSingleLine | gfx::kVCenter | gfx::kAlignRight, dim);
    const gfx::Color grip = (m_overBar || m_dragging) ? mark : lines;
    const int gx = W / 2 + 4, gy = L.bar_y + L.bar_h / 2;
    cv.fill_rect(gfx::Rect{ gx, gy - 2, 24, 1 }, grip);
    cv.fill_rect(gfx::Rect{ gx, gy + 1, 24, 1 }, grip);
}

void LibraryTree::on_wheel(int, int y, float notches) {
    if (split() && y >= sections().bottom_y) {
        m_plScroll -= (int)(notches * text_row_h() * 3);
        clamp_pl_scroll();
        invalidate();
        return;
    }
    m_scroll -= (int)(notches * row_h() * 3);
    clamp_scroll();
    invalidate();
}

void LibraryTree::on_mouse_down(const ui::MouseEvent& e) {
    host()->focus();
    if (split()) {
        const SplitSections L = sections();
        if (e.y >= L.bar_y && e.y < L.bottom_y) {           // the bar: drag it, or double-click to reset
            if (e.button != ui::MouseButton::Left) return;
            if (e.double_click) {
                m_split = kDefaultSplit;
                save_split();
                clamp_scroll();
                invalidate();
                return;
            }
            m_dragging = true;
            m_dragOff = e.y - L.bar_y;
            host()->capture_mouse(true);
            invalidate();
            return;
        }
        if (e.y >= L.bottom_y) {                            // a playlist: click to switch to it
            const int i = (e.y - L.bottom_y + m_plScroll) / text_row_h();
            auto pm = playlist_manager::get();
            if (i < 0 || (t_size)i >= pm->get_playlist_count()) return;
            if (e.button == ui::MouseButton::Left) {
                pm->set_active_playlist((t_size)i);
                if (m_engine) m_engine->repaint_all();
            } else if (e.button == ui::MouseButton::Right) playlist_menu((t_size)i, e.x, e.y);
            return;
        }
    }
    const int row = row_at(e.y);
    if (row < 0) return;
    const int item = m_tree.rows[row];
    const TreeItem& it = m_tree.items[item];
    const int x = kPad + it.depth * kIndent;
    m_sel = item;
    invalidate();
    if (e.button == ui::MouseButton::Right) { context_menu(item, e.x, e.y); return; }
    if (e.button != ui::MouseButton::Left) return;
    if (!it.children.empty() && e.x >= x - 2 && e.x < x + 12) { toggle(item); return; }
    // library_tree.album_click = show: one click puts the album in the "Library view" playlist.
    if (!e.double_click && it.kind == kAlbum && m_engine &&
        m_engine->config().str("library_tree.album_click") == "show") {
        show_album(m_artists[it.a].albums[it.b]);
        return;
    }
    if (e.double_click) {
        const int k = it.kind;
        if (k == kAlbum || k == kPlaylist) activate(item);
        else toggle(item);
    }
}

void LibraryTree::on_mouse_move(int, int y, unsigned, bool) {
    if (m_dragging) {
        const int H = host()->bounds().h, bh = bar_h();
        m_split = split_permille(H, y - m_dragOff, bh);
        // Keep the stored share to what the minimum section sizes allow.
        m_split = split_permille(H, split_sections(H, m_split, bh, 2 * row_h()).bar_y, bh);
        clamp_scroll();
        invalidate();
        return;
    }
    if (!split()) return;
    const SplitSections L = sections();
    const bool over = y >= L.bar_y && y < L.bottom_y;
    if (over != m_overBar) {
        m_overBar = over;
        host()->set_cursor(over ? ui::Cursor::SizeNS : ui::Cursor::Arrow);
        invalidate();
    }
}

void LibraryTree::on_mouse_up(const ui::MouseEvent& e) {
    if (!m_dragging || e.button != ui::MouseButton::Left) return;
    m_dragging = false;
    host()->capture_mouse(false);
    save_split();
    invalidate();
}

void LibraryTree::on_mouse_leave() {
    if (m_dragging || !m_overBar) return;
    m_overBar = false;
    host()->set_cursor(ui::Cursor::Arrow);
    invalidate();
}

bool LibraryTree::on_key_down(int key, unsigned) {
    if (key == ui::kKeyF5) { load_local(); start_remote_load(); finish_data(); return true; }
    const int row = m_tree.row_of(m_sel);
    const int page = std::max(1, sections().top_h / row_h() - 1);
    const int to = list_nav(key, row, (int)m_tree.rows.size(), page);
    if (to >= 0) { select_row(to); return true; }
    if (m_sel < 0) return false;
    int selRow, toggleItem;
    if (m_tree.horizontal_key(key, row, selRow, toggleItem)) {
        if (toggleItem >= 0) toggle(toggleItem);
        else if (selRow >= 0) select_row(selRow);
        return true;
    }
    const TreeItem& it = m_tree.items[m_sel];
    switch (key) {
    case ui::kKeyEnter: {
        const int k = it.kind;
        if (k == kAlbum || k == kPlaylist || k == kArtist) activate(m_sel);
        else toggle(m_sel);
        return true;
    }
    }
    return false;
}

// Finds the playlist called name, or makes it at the end of the list; pfc_infinite if that fails.
static t_size find_or_create_playlist(const char* name) {
    auto pm = playlist_manager::get();
    for (t_size i = 0, n = pm->get_playlist_count(); i < n; ++i) {
        pfc::string8 nm; pm->playlist_get_name(i, nm);
        if (nm == name) return i;
    }
    return pm->create_playlist(name, strlen(name), pfc_infinite);
}

// An album's tracks in disc and track order.
static metadb_handle_list album_tracks_sorted(const std::vector<metadb_handle_ptr>& items) {
    static service_ptr_t<titleformat_object> order;
    if (order.is_empty())
        titleformat_compiler::get()->compile_safe(order, "$num($if2(%discnumber%,1),3)|$num($if2(%tracknumber%,0),4)|%path%");
    metadb_handle_list list; for (auto& h : items) list.add_item(h);
    list.sort_by_format(order, nullptr);
    return list;
}

metadb_handle_list LibraryTree::album_tracks(const Album& al) { return album_tracks_sorted(al.items); }

// Shows a local album in "Library view" and makes that the active playlist, without playing it. When
// "Library view" is what's playing, its tracks move to "Album Browser" first so playback carries on.
void LibraryTree::show_album(const Album& al) {
    if (al.remote) return;
    static const char* kView = "Library view";
    auto pm = playlist_manager::get();
    t_size view = find_or_create_playlist(kView);
    if (view == pfc_infinite) return;
    if (pm->get_playing_playlist() == view) {
        const t_size keep = find_or_create_playlist("Album Browser");
        if (keep != pfc_infinite && keep != view) {
            metadb_handle_list playing; pm->playlist_get_all_items(view, playing);
            pm->playlist_remove_items(keep, bit_array_true());
            pm->playlist_insert_items(keep, 0, playing, bit_array_false());
            pm->set_playing_playlist(keep);
        }
    }
    pm->playlist_remove_items(view, bit_array_true());
    pm->playlist_insert_items(view, 0, album_tracks(al), bit_array_false());
    pm->set_active_playlist(view);
    if (m_engine) m_engine->repaint_all();
}

void LibraryTree::play_album(const Album& al, bool replace) {
    if (al.remote) {
        auto api = navidrome_api();
        if (api.is_valid()) api->play_album(al.id.c_str(), replace, replace);
        return;
    }
    const metadb_handle_list list = album_tracks(al);
    auto pm = playlist_manager::get();
    if (replace) {
        const t_size target = find_or_create_playlist("Album Browser");
        if (target == pfc_infinite) return;
        pm->playlist_remove_items(target, bit_array_true());
        pm->playlist_insert_items(target, 0, list, bit_array_true());
        pm->set_active_playlist(target);
        pm->playlist_execute_default_action(target, 0);
    } else {
        pm->activeplaylist_add_items(list, bit_array_false());
    }
}

void LibraryTree::play_artist(const Artist& ar, bool replace) {
    if (!ar.remote_id.empty()) {
        auto api = navidrome_api();
        if (api.is_valid()) api->play_artist(ar.remote_id.c_str(), replace, replace);
        return;
    }
    metadb_handle_list list;
    for (auto& al : ar.albums) for (auto& h : al.items) list.add_item(h);
    if (!list.get_count()) return;
    auto pm = playlist_manager::get();
    if (replace) {
        pm->activeplaylist_clear();
        pm->activeplaylist_add_items(list, bit_array_false());
        pm->playlist_execute_default_action(pm->get_active_playlist(), 0);
    } else pm->activeplaylist_add_items(list, bit_array_false());
}

void LibraryTree::activate(int item) {
    const TreeItem& n = m_tree.items[item];
    if (n.kind == kAlbum) play_album(m_artists[n.a].albums[n.b], true);
    else if (n.kind == kArtist) play_artist(m_artists[n.a], true);
    else if (n.kind == kPlaylist) {
        playlist_manager::get()->set_active_playlist((t_size)n.a);
        if (m_engine) m_engine->repaint_all();
    }
}

void LibraryTree::playlist_menu(t_size index, int x, int y) {
    ui::MenuItem m; m.label = "Switch to this playlist"; m.id = 1;
    ui::Menu menu{ m };
    if (ui::popup_menu(host(), x, y, menu) == 1) {
        playlist_manager::get()->set_active_playlist(index);
        if (m_engine) m_engine->repaint_all();
    }
}

void LibraryTree::context_menu(int item, int x, int y) {
    const TreeItem n = m_tree.items[item];
    if (n.kind != kAlbum && n.kind != kPlaylist && n.kind != kArtist) return;
    auto mi = [](const char* label, int id) { ui::MenuItem m; m.label = label; m.id = id; return m; };
    ui::Menu menu;
    if (n.kind == kArtist) {
        menu.push_back(mi("Play artist", 4));
        menu.push_back(mi("Add artist to current playlist", 5));
    } else if (n.kind == kAlbum) {
        menu.push_back(mi("Play album", 1));
        menu.push_back(mi("Add to current playlist", 2));
    } else menu.push_back(mi("Switch to this playlist", 3));
    const int cmd = ui::popup_menu(host(), x, y, menu);
    if (cmd == 1 || cmd == 2) play_album(m_artists[n.a].albums[n.b], cmd == 1);
    else if (cmd == 3) activate(item);
    else if (cmd == 4 || cmd == 5) play_artist(m_artists[n.a], cmd == 4);
}

}
