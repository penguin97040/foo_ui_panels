#pragma once
#include "../ui/view.h"
#include "../core/list_logic.h"
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace pui {

class SkinEngine;

class LibraryTree : public ui::View {
public:
    explicit LibraryTree(SkinEngine* engine) : m_engine(engine) {}

    void on_attached() override;
    void paint(gfx::Canvas& cv) override;
    void on_resize(int, int) override { clamp_scroll(); invalidate(); }
    void on_wheel(int x, int y, float notches) override;
    void on_mouse_down(const ui::MouseEvent& e) override;
    void on_mouse_move(int x, int y, unsigned mods, bool left_down) override;
    void on_mouse_up(const ui::MouseEvent& e) override;
    void on_mouse_leave() override;
    bool on_key_down(int key, unsigned mods) override;
    void on_timer(int id) override;
    void on_visibility(bool shown) override;
    void on_destroy() override;

private:
    struct Album {
        std::string name, id;
        bool remote = false;
        std::vector<metadb_handle_ptr> items;
    };
    struct Artist { std::string name, remote_id; std::vector<Album> albums; };
    enum Kind { kLoading, kPlaylists, kPlaylist, kArtist, kAlbum };
    struct RemoteAlbum { std::string artist, album, id, artist_id; };

    gfx::FontSpec font() const;
    gfx::FontSpec header_font() const;
    int text_row_h() const;
    int row_h() const;
    int art_size() const;
    bool split() const;
    int bar_h() const;
    SplitSections sections() const;
    void clamp_pl_scroll();
    void save_split();
    void playlists_changed();
    void draw_art(gfx::Canvas& cv, const TreeItem& it, int x, int y, int side, gfx::Color frame);
    void playlist_menu(t_size index, int x, int y);
    void load_local();
    void start_remote_load();
    void merge_remote(std::vector<RemoteAlbum>&& add);
    void finish_data();
    void refresh_tree();
    int  row_at(int y) const;
    void select_row(int row);
    void clamp_scroll();
    void toggle(int item);
    void activate(int item);
    void context_menu(int item, int x, int y);
    void play_album(const Album& al, bool replace);
    void play_artist(const Artist& ar, bool replace);
    Artist& artist_for(const std::string& name);
    std::string item_key(int item) const;

    // library_tree.layout = split: artists on top, the playlists below, and a bar between them that
    // drags to share out the height (m_split, per mille of it, kept in the skin's view state).
    struct LibEvents : library_callback_dynamic_impl_base {
        explicit LibEvents(LibraryTree* v) : m_v(v) {}
        LibraryTree* m_v;
        void on_items_added(metadb_handle_list_cref) override { m_v->host()->set_timer(kTimerReload, 1000); }
        void on_items_removed(metadb_handle_list_cref) override { m_v->host()->set_timer(kTimerReload, 1000); }
    };
    struct PlEvents : playlist_callback_impl_base {
        explicit PlEvents(LibraryTree* v)
            : playlist_callback_impl_base(flag_on_items_added | flag_on_items_removed | flag_on_playlist_activate |
                                          flag_on_playlist_created | flag_on_playlists_reorder |
                                          flag_on_playlists_removed | flag_on_playlist_renamed), m_v(v) {}
        LibraryTree* m_v;
        void on_items_added(t_size, t_size, metadb_handle_list_cref, const bit_array&) override { m_v->playlists_changed(); }
        void on_items_removed(t_size, const bit_array&, t_size, t_size) override { m_v->playlists_changed(); }
        void on_playlist_activate(t_size, t_size) override { m_v->playlists_changed(); }
        void on_playlist_created(t_size, const char*, t_size) override { m_v->playlists_changed(); }
        void on_playlists_reorder(const t_size*, t_size) override { m_v->playlists_changed(); }
        void on_playlists_removed(const bit_array&, t_size, t_size) override { m_v->playlists_changed(); }
        void on_playlist_renamed(t_size, const char*, t_size) override { m_v->playlists_changed(); }
    };
    enum { kTimerReload = 1 };
    static constexpr int kDefaultSplit = 600;

    SkinEngine* m_engine = nullptr;
    std::unique_ptr<LibEvents> m_libEvents;
    std::unique_ptr<PlEvents> m_plEvents;
    int m_split = kDefaultSplit;
    int m_plScroll = 0;
    bool m_dragging = false, m_overBar = false;
    int m_dragOff = 0;
    std::vector<Artist> m_artists;
    TreeRows m_tree;
    int m_sel = -1;
    int m_scroll = 0;
    std::set<std::string> m_expandedKeys;
    bool m_loaded = false, m_remote_loading = false;
    std::string m_remote_err;
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<shared_abort>();
    unsigned m_gen = 0;
};

}
