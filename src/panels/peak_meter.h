#pragma once
#include "../ui/view.h"
#include "../core/meter_math.h"
#include <chrono>
#include <mutex>
#include <vector>

namespace pui {

class SkinEngine;

class PeakMeter : public ui::View {
public:
    explicit PeakMeter(SkinEngine* engine);

    void on_attached() override;
    void on_timer(int) override { refresh_cfg(); }
    void paint(gfx::Canvas& cv) override;

private:
    void refresh_cfg();

    SkinEngine* m_engine = nullptr;
    service_ptr_t<visualisation_stream> m_vis;
    struct Cfg {
        gfx::Color accent{ 0, 140, 220 };
        // Skin options: peak_meter.orientation = vertical (columns rising from the bottom),
        // peak_meter.segment / segment_gap / channel_gap (px), color.peak_meter.unlit / peak.
        bool vertical = false;
        int seg = 4, segGap = 1, chGap = 2;
        bool hasUnlit = false; gfx::Color unlit, peak{ 255, 255, 255 };
        gfx::ImagePtr backdrop; int bx = 0, by = 0;
    };
    Cfg m_cfg; std::mutex m_cfgMx;
    std::vector<MeterBallistics> m_ch;
    std::chrono::steady_clock::time_point m_last{};
};

}
