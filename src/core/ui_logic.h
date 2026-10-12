#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pui {

constexpr int kZoomMin = 50, kZoomMax = 400;
int clamp_zoom(int pct);
int zoom_step(int pct, int dir);
double zoom_factor_for(int setting_pct, int system_dpi);
int to_device(int logical, double f);
int to_logical(int device, double f);

std::vector<std::string> split_actions(const std::string& s);
void parse_anchor(const std::string& s, std::string& halign, std::string& valign);
void name_hash128(const std::string& name, uint8_t out[16]);

struct MiniPlan {
    bool act = false;
    bool enter = false;
    int w = 0, h = 0;
};
MiniPlan mini_mode_plan(int curW, int curH, int miniW, int miniH, int savedW, int savedH);

// Shuffle (on/off) and repeat (0 off, 1 playlist, 2 track) as one foobar2000 playback order, and back.
// Shuffle and repeat-track can't both be active orders, so the remembered pvar fills in the one the
// active order doesn't say.
const char* playback_order_for(bool shuffle, int repeat);
struct OrderState { bool shuffle = false; int repeat = 0; };
OrderState order_state(const std::string& active, bool rememberedShuffle, int rememberedRepeat);

bool filter_matches(const std::string& filter, const std::string& text);

enum class AlbumSort { Artist, Album, Year, Added };
AlbumSort album_sort_from(const std::string& s);
const char* album_sort_name(AlbumSort s);

struct AlbumKeys { std::string artist, album, year, added; };
std::vector<size_t> album_view(const std::vector<AlbumKeys>& all, AlbumSort sort, const std::string& filter);

}
