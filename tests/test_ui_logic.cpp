#include "test.h"
#include "core/ui_logic.h"
#include "core/button.h"
#include <vector>
#include <cstring>

using namespace pui;

TEST("zoom_step: presets up and down, clamped at the ends") {
    CHECK_EQ(zoom_step(100, +1), 110);
    CHECK_EQ(zoom_step(100, -1), 90);
    CHECK_EQ(zoom_step(130, +1), 150);
    CHECK_EQ(zoom_step(130, -1), 125);
    CHECK_EQ(zoom_step(300, +1), 300);
    CHECK_EQ(zoom_step(75, -1), 75);
    CHECK_EQ(zoom_step(60, -1), 60);
    CHECK_EQ(zoom_step(100, 0), 100);
}

TEST("zoom_factor_for: automatic follows the system dpi") {
    CHECK_EQ(zoom_factor_for(0, 96), 1.0);
    CHECK_EQ(zoom_factor_for(0, 144), 1.5);
    CHECK_EQ(zoom_factor_for(0, 0), 1.0);
    CHECK_EQ(zoom_factor_for(125, 144), 1.25);
    CHECK_EQ(zoom_factor_for(1000, 96), 4.0);
}

TEST("to_device / to_logical round-trip without overshooting the edge") {
    CHECK_EQ(to_device(100, 1.5), 150);
    CHECK_EQ(to_logical(150, 1.5), 100);
    CHECK_EQ(to_logical(149, 1.5), 99);
    CHECK_EQ(to_device(7, 1.25), 9);
    CHECK_EQ(to_logical(5, 1.0), 5);
}

TEST("split_actions: semicolons, whitespace, empties") {
    auto a = split_actions(" PVAR:SET:a:1 ; WINDOWSIZE:430:172:RIGHT:TOP;;  ");
    CHECK_EQ(a.size(), (size_t)2);
    CHECK_EQ(a[0], std::string("PVAR:SET:a:1"));
    CHECK_EQ(a[1], std::string("WINDOWSIZE:430:172:RIGHT:TOP"));
    CHECK(split_actions("").empty());
    auto one = split_actions("Next");
    CHECK(one.size() == 1 && one[0] == "Next");
}

TEST("parse_anchor: separators and case") {
    std::string h, v;
    parse_anchor("right top", h, v);
    CHECK_EQ(h, std::string("RIGHT")); CHECK_EQ(v, std::string("TOP"));
    parse_anchor("CENTER:BOTTOM", h, v);
    CHECK_EQ(h, std::string("CENTER")); CHECK_EQ(v, std::string("BOTTOM"));
    parse_anchor("", h, v);
    CHECK(h.empty() && v.empty());
}

TEST("name_hash128: deterministic, distinct names differ") {
    uint8_t a[16], b[16], c[16];
    name_hash128("Mini mode", a);
    name_hash128("Mini mode", b);
    name_hash128("Mini mode ", c);
    CHECK(memcmp(a, b, 16) == 0);
    CHECK(memcmp(a, c, 16) != 0);
}

TEST("mini_mode_plan: enter, leave, nothing to do") {
    MiniPlan p = mini_mode_plan(950, 750, 430, 172, 0, 0);
    CHECK(p.act && p.enter && p.w == 430 && p.h == 172);
    p = mini_mode_plan(430, 172, 430, 172, 950, 750);
    CHECK(p.act && !p.enter && p.w == 950 && p.h == 750);
    CHECK(!mini_mode_plan(430, 172, 430, 172, 0, 0).act);
    CHECK(!mini_mode_plan(430, 172, 430, 172, 430, 172).act);
    CHECK(!mini_mode_plan(950, 750, 0, 0, 0, 0).act);
}

TEST("filter_matches: every word, case-insensitive") {
    CHECK(filter_matches("", "anything"));
    CHECK(filter_matches("pink", "Pink Floyd|The Wall"));
    CHECK(filter_matches("wall PINK", "Pink Floyd|The Wall"));
    CHECK(!filter_matches("pink zeppelin", "Pink Floyd|The Wall"));
    CHECK(filter_matches("  floyd  ", "Pink Floyd"));
}

TEST("album_sort_from / album_sort_name round-trip") {
    for (AlbumSort s : { AlbumSort::Artist, AlbumSort::Album, AlbumSort::Year, AlbumSort::Added })
        CHECK(album_sort_from(album_sort_name(s)) == s);
    CHECK(album_sort_from("") == AlbumSort::Artist);
    CHECK(album_sort_from("YEAR") == AlbumSort::Year);
}

TEST("playback_order_for / order_state: shuffle and repeat survive a round trip") {
    for (bool sh : { false, true })
        for (int rp : { 0, 1, 2 }) {
            OrderState s = order_state(playback_order_for(sh, rp), sh, rp);
            CHECK(s.shuffle == sh && s.repeat == rp);
        }
    CHECK(std::string(playback_order_for(true, 2)) == "Repeat (track)");
    // Changed from foobar's own menu: the active order wins over what was remembered.
    OrderState s = order_state("Default", true, 2);
    CHECK(!s.shuffle && s.repeat == 0);
    s = order_state("Shuffle (albums)", false, 2);
    CHECK(s.shuffle && s.repeat == 0);
    CHECK(order_state("Random", false, 0).shuffle);
}

TEST("hover_delay_state: hidden until the delay has passed, then shown") {
    HoverDelay d = hover_delay_state(500, 2000);
    CHECK(!d.shown); CHECK_EQ(d.waitMs, 1500);
    CHECK(hover_delay_state(2000, 2000).shown);
    CHECK(hover_delay_state(9000, 2000).shown);
    CHECK(hover_delay_state(0, 0).shown);
    CHECK_EQ(hover_delay_state(-5, 2000).waitMs, 2000);
}

TEST("hover_target_at: the last button drawn wins, and the same rectangle compares equal") {
    std::vector<Button> list(2);
    list[0].x = 0;  list[0].y = 0; list[0].w = 100; list[0].h = 30;
    list[1].x = 10; list[1].y = 0; list[1].w = 20;  list[1].h = 30;
    const HoverTarget t = hover_target_at(list, 15, 5);
    CHECK_EQ(t.x, 10); CHECK_EQ(t.w, 20);
    CHECK(hover_target_at(list, 50, 5) == (HoverTarget{0, 0, 100, 30}));
    CHECK_EQ(hover_target_at(list, 500, 5).w, 0);
    std::vector<Button> shifted = { list[1], list[0] };   // same buttons, new order: still the same target
    CHECK(hover_target_at(shifted, 50, 5) == hover_target_at(list, 50, 5));
}
