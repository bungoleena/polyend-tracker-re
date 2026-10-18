// Master / Track Mixer / Global Mixer (spec 06 §4).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app/project_fields.h"
#include "ui/screens.h"

namespace ot {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Saturation mode labels 0..4 as listed by the stock Master page [S snap 022]
// (mode 0 = plain hard saturation, spec 04 §5.4; supersedes the [I] labels).
const char* kSatNames[5] = {"Hard Clip", "Soft Clip", "Saturate", "Overdrive", "Drive+"};
// Global mixer channels (spec 06 §4.5): Delay, Reverb, Dry Mix, Line In.
const uint32_t kGlobalLevel[4] = {pf::kMixDelay, pf::kMixReverb, pf::kMixDry, pf::kLineInLevel};
const uint32_t kGlobalMute[4] = {pf::kMixDelayMute, pf::kMixReverbMute, pf::kMixDryMute, pf::kLineInMute};
const char* kGlobalNames[4] = {"Delay", "Reverb", "Dry Mix", "Line In"};
const int kMainParams[4] = {0, 5, 6, 7};   // Volume, Saturation, Bass Boost, Space (F-index)
// Delay sync rate index -> note value, in DSYNC order (spec 04 §5.1,
// spec 16 §B.6).
const char* kDelayRates[19] = {"8",   "6",   "4",    "3",   "2",    "3/2",  "1",    "3/4",  "1/2", "3/8",
                               "1/3", "1/4", "3/16", "1/6", "1/8",  "1/12", "1/16", "1/24", "1/32"};
// Limiter sidechain 0..10 (spec 07 Q2, spec 16 §B.6): "Disable", the
// current names of tracks 1..8, "Line in L" / "Line in R".
void sidechainText(const Project& p, int n, char* out, int cap) {
    if (n <= 0) std::snprintf(out, size_t(cap), "Disable");
    else if (n <= 8) p.trackName(n - 1, out, uint32_t(cap));
    else std::snprintf(out, size_t(cap), "Line in %s", n == 9 ? "L" : "R");
}
// Master volume text (spec 16 §B.6): v/100 * 63.5 - 51.5 dB, v = 81 is
// exactly 0 dB.
void masterVolumeText(int v, char* out, int cap) {
    const double db = v == 81 ? 0.0 : double(v) / 100.0 * 63.5 - 51.5;
    std::snprintf(out, size_t(cap), "%0.2f dB", db);
}

// Fader level "-2.40 dB" [S snap 023].
void levelText(uint8_t v, char* out, int cap) {
    const float g = audio::trkGain(v);
    if (g <= 0) std::snprintf(out, size_t(cap), "-inf dB");
    else std::snprintf(out, size_t(cap), "%.2f dB", double(20.0f * std::log10(g)));
}

// --- stock Mixer / Master chrome (spec 20 §4.9, VISUAL_DIFF §8.11-8.13) ----
// Columns sit at x = 2 + 100 k (C8).
inline int colX(int k) { return 2 + 100 * k; }
// Value bar of the column box (x, y, w, h) without its own background; a
// value of 0 shows the 1.5-px line at the well bottom (§7.4).
void bar(Gfx& g, const Theme& th, int x, int y, int w, int h, float v, uint32_t fill = wg::kThemeColour,
         bool bipolar = false) {
    wg::ValueBarStyle st;
    st.background = false;
    st.fill = fill;
    st.bipolar = bipolar;
    wg::valueBar(g, th, x, y, w, h, v, st);
    if (bipolar || int(float(h - 25) * v + 0.5f) > 0) return;
    const uint32_t c = fill == wg::kThemeColour ? th.slot(slot::kValueBar) : fill;
    const int fx = x + 11, fw = w - 21, base = y + h - 15;
    wg::blendFill(g, fx, base - 1, fw, 1, c, wg::kEdgeA);
    wg::blendFill(g, fx, base + 1, fw, 1, c, wg::kEdgeA);
    g.fill(fx, base, fw, 1, c);
}
// Full-height column k (x 2 + 100 k, 29, 96 (+ 100 per extra column), 394).
void column(Gfx& g, const Theme& th, int k, int span = 1) { wg::panel(g, th, colX(k), wg::kPanelY, 96 + 100 * (span - 1)); }
void columnBar(Gfx& g, const Theme& th, int k, float v, bool bipolar = false) {
    column(g, th, k);
    bar(g, th, colX(k), wg::kPanelY, 97, wg::kPanelH, v, wg::kThemeColour, bipolar);
}
// Line-in send bar: disabled = the muted-fader colour (303030 line on
// Monochrome) [S round 3, snap 006 / 007].
void lineInBar(Gfx& g, const Theme& th, int k, uint8_t send, bool off) {
    column(g, th, k);
    bar(g, th, colX(k), wg::kPanelY, 97, wg::kPanelH, send / 100.0f,
        off ? th.slot(slot::kValueBarAlt) : wg::kThemeColour);
}
void columnFocus(Gfx& g, const Theme& th, int k) { wg::focusFrame(g, th, colX(k) - 1, 31, colX(k) + 96, 422); }
// Mixer column header (spec 20 §4.9): 080808 band y 32..58 with soft rows
// 31 / 59, bold font-4 name centred on 50 + 100 k; body panels from y 64.
// Band and name in the title-bar colours: 080808 / white on Original,
// b9b9ba / black on Monochrome [S round 3, snap 009 / 010].
void header(Gfx& g, const Theme& th, int k, const char* text) {
    const uint32_t c = th.slot(slot::kTitle + 1);
    wg::panel(g, th, colX(k), 32, 96, 26, c, false);   // rows 32..58
    wg::blendFill(g, colX(k), 31, 97, 1, c, wg::kEdgeA);
    wg::blendFill(g, colX(k), 59, 97, 1, c, wg::kEdgeA);
    g.text(50 + 100 * k, 45, kFont4, th.slot(slot::kTitle), text, IDisplay::kOptCenter);   // ink y 40..51 [S]
}
void body(Gfx& g, const Theme& th, int x, int w) {
    const uint32_t c = th.slot(slot::kEmptyPanel + 1);
    wg::panel(g, th, x, 64, w, 360, c);
    wg::blendFill(g, x, 63, w + 1, 1, c, wg::kEdgeA);
}
// Option list in column k (Saturation, Ping-pong, ...).
struct Names {
    const char* const* names;
};
void optionList(Gfx& g, const Theme& th, int k, const char* const* names, int count, int sel, bool enabled = true) {
    static Names ctx;
    ctx.names = names;
    wg::ListView v;
    v.x = colX(k);
    v.w = 97;
    v.count = count;
    v.sel = sel;
    v.selStyle = wg::ListSel::kWhite;
    v.top = wg::listFollow(sel, count, wg::listVisibleRows(v.h), 0);
    v.ctx = &ctx;
    v.item = [](void* c, int i, char* out, int cap) {
        std::snprintf(out, size_t(cap), "%s", static_cast<Names*>(c)->names[i]);
    };
    v.enabled = enabled;   // disabled style [S round 3, snap 007 Rate]
    wg::list(g, th, v);
}
// Line-in send text (Reverb / Delay "Line in"): the send byte through the
// fader table like the levels, "-inf dB" at 0 [S round 3, snap 006 / 007];
// the send gain is 0.5 * TRK[send] (spec 07 "Line-in sends").
// Disabled (greyed label, bar and value) while the Global Mixer Line In
// channel is muted [S snap 006 / 007 / 010 show both; the rule is I].
bool lineInOff(const Project& p) { return p.u8(pf::kLineInMute) != 0; }
}  // namespace

void MixerScreen::start(int opt) {
    const int hi = (opt >> 8) & 0xFF;
    if (hi >= 1 && hi <= 3) page = hi - 1;
    for (auto& h : held_) h = false;
    // The entering Master press is consumed by the switch (spec 06 §0.3).
    masterPending_ = otherKeyDuringMaster_ = masterHoldFired_ = false;
    refreshBar();
}

int MixerScreen::meterBar(float meanLevel) {
    // spec 06 §4.4 level meter, steps 1-2 (0x35454).
    constexpr float kLoDb = -3.6f, kHiDb = 0.6f;
    const float x = meanLevel / 0.71f;
    if (x <= 0.0f) return 0;
    const float db = 20.0f * std::log10(x);
    if (db <= kLoDb) return 0;
    if (db >= kHiDb) return 100;
    return int((db - kLoDb) * 100.0f / (kHiDb - kLoDb));
}

namespace {
// "Back" on F8 of the sub-pages sits on the title line of a two-line label
// with an empty value (ink y 436..446) [S round 3, snap 006..008 / 011].
void setBack(BottomBar& b) { b.set(7, "Back", " "); }
}  // namespace

void MixerScreen::refreshBar() {
    BottomBar& b = ui_.bar;
    App& a = app_;
    Project& p = a.project;
    b.clear();
    char v[24];
    if (page == 0) {
        if (sub == 0) {
            masterVolumeText(a.cfg.masterVolume, v, sizeof(v));
            b.set(0, "Volume", v);
            b.set(1, "Reverb");
            b.set(2, "Delay");
            b.set(3, "Limiter");
            b.set(4, "EQ");
            b.set(5, "Saturation", kSatNames[clampi(p.u8(pf::kSaturation), 0, 4)]);
            std::snprintf(v, sizeof(v), "%d", p.u8(pf::kBassBoost));
            b.set(6, "Bass Boost", v);
            std::snprintf(v, sizeof(v), "%d", p.u8(pf::kSpace));
            b.set(7, "Space", v);
            // The selected parameter is shown by the column focus frame; the
            // label is not highlighted [S snap 022]; the sub-pages follow
            // the same rule [I].
        } else if (sub == 1) {
            // Values as whole percent: Size 0.60 -> "60" [S round 3, snap 006].
            static const char* n[4] = {"Size", "Damping", "Predelay", "Diffusion"};
            for (int i = 0; i < 4; ++i) {
                std::snprintf(v, sizeof(v), "%d", int(p.f32(pf::kReverbSize + 4 * uint32_t(i)) * 100.0f + 0.5f));
                b.set(i, n[i], v);
            }
            levelText(p.u8(pf::kLineInReverb), v, sizeof(v));
            b.set(4, "Line in", v, !lineInOff(p));
            setBack(b);
        } else if (sub == 2) {
            const uint8_t fl = p.u8(pf::kDelayFlags);
            b.set(0, "PingPong", fl & 0x80 ? "On" : "Off");
            b.set(1, "Sync", fl & 0x40 ? "On" : "Off");
            b.set(2, "Rate", kDelayRates[clampi(fl & 0x3F, 0, 18)], (fl & 0x40) != 0);
            std::snprintf(v, sizeof(v), "%d ms", p.u16(pf::kDelayTime));
            b.set(3, "Time", v, !(fl & 0x40));
            std::snprintf(v, sizeof(v), "%d", p.u8(pf::kDelayFeedback));
            b.set(4, "Feedback", v);
            std::snprintf(v, sizeof(v), "%d Hz", p.u16(pf::kDelayFilter));
            b.set(5, "Filter", v);
            levelText(p.u8(pf::kLineInDelay), v, sizeof(v));
            b.set(6, "Line in", v, !lineInOff(p));
            setBack(b);
        } else if (sub == 3) {
            std::snprintf(v, sizeof(v), "%.3fs", p.u16(pf::kLimAttack) / 1000.0);
            b.set(0, "LimiterAtt", v);   // stock limiter labels (STRINGS_DIFF §2)
            // Attack / release are stored in ms and shown as value/1000 s (spec 07 Q2).
            std::snprintf(v, sizeof(v), "%.3fs", double(p.f32(pf::kLimRelease)) / 1000.0);
            b.set(1, "LimiterRls", v);
            // Displayed threshold = 20 log10(LIMTHR[v*100/32767] / 32767) (spec 07 Q2).
            const float thr = audio::limiterThreshold(p.u16(pf::kLimThreshold) > 32767 ? 32767 : p.u16(pf::kLimThreshold));
            std::snprintf(v, sizeof(v), "%.2f dB", thr > 0.0f ? 20.0 * std::log10(double(thr)) : -96.0);
            b.set(2, "LimiterThr", v);   // "-6.00 dB" [S round 3, snap 008]
            // Read-only meter (spec 06 §4.3): one centred line, no value
            // [S round 3, snap 008].
            b.set(3, "Gain Reduce");
            sidechainText(p, p.u8(pf::kSidechain), v, sizeof(v));
            b.set(4, "Sidechain", v);
            setBack(b);
        } else {
            static const char* n[5] = {"115 Hz", "330 Hz", "990 Hz", "3000 Hz", "9900 Hz"};
            for (int i = 0; i < 5; ++i) {
                // "0.00 dB" [S round 3, snap 011]; +0.0 rounds to "0.00".
                const double db = double(12.0f * p.f32(pf::kEqGain + 4 * uint32_t(i)));
                std::snprintf(v, sizeof(v), "%.2f dB", std::fabs(db) < 0.005 ? 0.0 : db);
                b.set(i, n[i], v);
            }
            setBack(b);
        }
        return;
    }
    if (page == 1) {
        // Label: fader level (grey) / action (white); the track name is
        // the column header [S snap 023]. A muted channel's label uses the
        // disabled colours [S snap 024 Line In].
        for (int t = 0; t < 8; ++t) {
            levelText(p.u8(pf::kTrackVolume + uint32_t(t)), v, sizeof(v));
            if (rename) b.set(t, v, "Edit");
            else if (ui_.shift()) b.set(t, v, solo_ == t ? "Unsolo" : "Solo");
            else b.set(t, v, a.trackMuted(t) ? "Unmute" : "Mute", !a.trackMuted(t));
            b.highlight(t, held_[t]);
        }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        levelText(p.u8(kGlobalLevel[i]), v, sizeof(v));
        b.set(i, v, p.u8(kGlobalMute[i]) ? "Unmute" : "Mute", !p.u8(kGlobalMute[i]));
        b.highlight(i, held_[i]);
    }
}

// Master parameters shown as option lists: Saturation; Delay Ping-pong,
// Sync, Rate; Limiter Sidechain. The encoder (+ = clockwise) moves their
// highlight down the list; on value bars it raises the value [S owner test
// on 1.9.2: "Saturation Right -> Down, Left -> up | Equalizer right -> up"].
bool MixerScreen::isListParam(int sub, int param) {
    switch (sub) {
        case 0: return param == 1;
        case 2: return param >= 0 && param <= 2;
        case 3: return param == 3;
        default: return false;
    }
}

void MixerScreen::masterChange(int d) {
    App& a = app_;
    Project& p = a.project;
    if (sub == 0) {
        switch (clampi(param, 0, 3)) {
            case 0:
                a.cfg.masterVolume = uint8_t(clampi(a.cfg.masterVolume + d, 0, 100));
                a.markConfigDirty();
                return;
            case 1: p.setU8(pf::kSaturation, uint8_t(clampi(p.u8(pf::kSaturation) + d, 0, 4))); break;
            case 2: p.setU8(pf::kBassBoost, uint8_t(clampi(p.u8(pf::kBassBoost) + d, 0, 100))); break;
            case 3: p.setU8(pf::kSpace, uint8_t(clampi(p.u8(pf::kSpace) + d, 0, 8))); break;
        }
    } else if (sub == 1) {
        if (param < 4) {
            const uint32_t off = pf::kReverbSize + 4 * uint32_t(param);
            const float mx = param == 3 ? 0.85f : 1.0f;
            p.setF32(off, clampf(p.f32(off) + d * (param == 3 ? 0.0085f : 0.01f), 0, mx));
        } else {
            p.setU8(pf::kLineInReverb, uint8_t(clampi(p.u8(pf::kLineInReverb) + d, 0, 100)));
        }
    } else if (sub == 2) {
        uint8_t fl = p.u8(pf::kDelayFlags);
        switch (param) {
            // Ping-pong / Sync lists "On" above "Off" [S round 3, snap 007]:
            // + = the next row down (Off), - = the row up (On).
            case 0: fl = uint8_t(d > 0 ? fl & ~0x80 : fl | 0x80); break;
            case 1: fl = uint8_t(d > 0 ? fl & ~0x40 : fl | 0x40); break;
            case 2: fl = uint8_t((fl & 0xC0) | clampi((fl & 0x3F) + d, 0, 18)); break;
            case 3: {
                int t = p.u16(pf::kDelayTime);
                const int stepv = t < 100 ? 1 : (t < 500 ? 5 : 10);
                p.setU16(pf::kDelayTime, uint16_t(clampi(t + d * stepv, 1, 3500)));
                break;
            }
            case 4: p.setU8(pf::kDelayFeedback, uint8_t(clampi(p.u8(pf::kDelayFeedback) + d, 0, 100))); break;
            case 5: p.setU16(pf::kDelayFilter, uint16_t(clampi(p.u16(pf::kDelayFilter) + d * 100, 100, 20000))); break;
            case 6: p.setU8(pf::kLineInDelay, uint8_t(clampi(p.u8(pf::kLineInDelay) + d, 0, 100))); break;
        }
        p.setU8(pf::kDelayFlags, fl);
    } else if (sub == 3) {
        switch (param) {
            case 0: p.setU16(pf::kLimAttack, uint16_t(clampi(p.u16(pf::kLimAttack) + d, 1, 1000))); break;
            // Release 1..1000 ms, step 1; the first edit clamps the New
            // Project 0.512 ms to >= 1 (spec 07 Q2).
            case 1: p.setF32(pf::kLimRelease, clampf(p.f32(pf::kLimRelease) + float(d), 1.0f, 1000.0f)); break;
            // Threshold: one detent = +-32767/100 (integer 327), clamped 0..32767.
            case 2: p.setU16(pf::kLimThreshold, uint16_t(clampi(int16_t(p.u16(pf::kLimThreshold)) + d * (32767 / 100), 0, 32767))); break;
            case 3: p.setU8(pf::kSidechain, uint8_t(clampi(p.u8(pf::kSidechain) + d, 0, 10))); break;
        }
    } else {
        const uint32_t off = pf::kEqGain + 4 * uint32_t(clampi(param, 0, 4));
        p.setF32(off, clampf(p.f32(off) + d * 0.02f, -1, 1));
    }
    a.markProjectDirty();
}

void MixerScreen::change(int d) {
    App& a = app_;
    if (page == 0) {
        masterChange(d);
        return;
    }
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        if (!held_[i]) continue;
        any = true;
        changed_[i] = true;
        if (page == 1) a.project.setU8(pf::kTrackVolume + uint32_t(i), uint8_t(clampi(a.project.u8(pf::kTrackVolume + uint32_t(i)) + d, 0, 100)));
        else if (i < 4) a.project.setU8(kGlobalLevel[i], uint8_t(clampi(a.project.u8(kGlobalLevel[i]) + d, 0, 100)));
    }
    if (any) a.markProjectDirty();
}

void MixerScreen::mixerKey(int i, uint8_t state) {
    App& a = app_;
    if (page == 2 && i >= 4) return;
    if (state == kPress) {
        if (rename && page == 1) {
            char nm[24];
            a.project.trackName(i, nm, sizeof(nm));
            static int renameTrack;
            renameTrack = i;
            ui_.kb.open("Track name", nm, 7, "Enter",
                        [](void* ctx, bool ok, const char* text) {
                            if (!ok) return;
                            App& ap = *static_cast<App*>(ctx);
                            ap.project.setTrackName(renameTrack, text);
                            ap.markProjectDirty();
                        },
                        &a);
            return;
        }
        held_[i] = true;
        changed_[i] = false;
        return;
    }
    if (state != kRelease || !held_[i]) return;
    held_[i] = false;
    if (changed_[i]) return;
    // spec 06 §4.4 release without change: mute / solo.
    if (page == 1) {
        if (ui_.shift()) {
            if (solo_ == i) {
                solo_ = -1;
                for (int t = 0; t < 8; ++t) a.setTrackMute(t, false);
            } else {
                solo_ = i;
                for (int t = 0; t < 8; ++t) a.setTrackMute(t, t != i);
            }
        } else {
            a.setTrackMute(i, !a.trackMuted(i));
        }
    } else {
        Project& p = a.project;
        if (ui_.shift()) {
            // Same solo rule as §4.4: the same key again unsolos all.
            const bool unsolo = globalSolo_ == i;
            globalSolo_ = unsolo ? -1 : i;
            for (int k = 0; k < 4; ++k) p.setU8(kGlobalMute[k], !unsolo && k != i);
        } else {
            p.setU8(kGlobalMute[i], !p.u8(kGlobalMute[i]));
        }
        a.markProjectDirty();
    }
}

void MixerScreen::onButton(uint8_t id, uint8_t state) {
    if (id == btn::Master) {
        // spec 06 §4.1 (0x35c94).
        if (state == kPress) {
            snapPage_ = page;
            snapSub_ = sub;
            snapParam_ = param;
            masterPending_ = true;
            otherKeyDuringMaster_ = false;
            masterHoldFired_ = false;
        } else if (state == kHold) {
            if (!masterPending_) return;
            page = 0;
            sub = 0;
            param = 0;
            masterHoldFired_ = true;
        } else if (state == kRelease) {
            if (!masterPending_) return;
            masterPending_ = false;
            if (otherKeyDuringMaster_) {
                // Combination: restore the snapshot and raise event 5.
                page = snapPage_;
                sub = snapSub_;
                param = snapParam_;
                for (auto& h : held_) h = false;
                ui_.switchTo(ui_.previous());
                return;
            }
            if (!masterHoldFired_) page = (page + 1) % 3;   // the hold already jumped
        }
        refreshBar();
        return;
    }
    if (masterPending_ && state == kPress && ui_.isDown(btn::Master)) otherKeyDuringMaster_ = true;
    if (btn::isFKey(id)) {
        const int f = id;
        if (page == 0) {
            if (state != kPress) return;
            if (sub == 0) {
                if (f >= 1 && f <= 4) {
                    sub = f;
                    param = 0;
                } else if (f == 0) {
                    param = 0;
                } else if (f == 5) {
                    if (param == 1) masterChange(1 - (app_.project.u8(pf::kSaturation) >= 4 ? 5 : 0));
                    param = 1;
                } else if (f == 6) {
                    param = 2;
                } else {
                    param = 3;
                }
            } else if (f == 7) {
                sub = 0;
                param = 0;
            } else {
                const int maxF = sub == 2 ? 6 : 4;
                if (f <= maxF && !(sub == 3 && f == 3)) param = (sub == 3 && f == 4) ? 3 : f;
            }
        } else {
            mixerKey(f, state);
        }
        refreshBar();
        return;
    }
    if (state != kPress) {
        if (id == btn::Shift) refreshBar();
        return;
    }
    switch (id) {
        case btn::Up:
        case btn::Down: {
            // Value bars: Up = +1, Down = -1. Option lists (drawn top to
            // bottom): Up = the item above, Down = the item below, like the
            // encoder (+ = next item down) [S owner test on 1.9.2].
            int d = id == btn::Up ? 1 : -1;
            if (page == 0 && isListParam(sub, param)) d = -d;
            change(d);
            break;
        }
        case btn::Left:
        case btn::Right:
            if (page == 0 && sub == 0) param = clampi(param + (id == btn::Right ? 1 : -1), 0, 3);
            else if (page == 0) {
                const int mx = sub == 1 ? 4 : sub == 2 ? 6 : sub == 3 ? 3 : 4;
                param = clampi(param + (id == btn::Right ? 1 : -1), 0, mx);
                if (sub == 2) {
                    const bool sync = app_.project.u8(pf::kDelayFlags) & 0x40;
                    if (param == 2 && !sync) param += id == btn::Right ? 1 : -1;
                    if (param == 3 && sync) param += id == btn::Right ? 1 : -1;
                }
                // A disabled Line in send (Line In muted) is skipped like
                // Rate / Time [I]; it is the last parameter of the page.
                if ((sub == 1 || sub == 2) && param == (sub == 1 ? 4 : 6) && lineInOff(app_.project)) --param;
            }
            break;
        case btn::Delete:
            // Delete resets held faders to 86 = 0 dB (spec 06 §4.4, spec 07 Q3/Q6).
            if (page == 1)
                for (int t = 0; t < 8; ++t)
                    if (held_[t]) app_.project.setU8(pf::kTrackVolume + uint32_t(t), 86);
            if (page == 2)
                for (int i = 0; i < 4; ++i)
                    if (held_[i]) app_.project.setU8(kGlobalLevel[i], 86);
            app_.markProjectDirty();
            break;
        case btn::Rec:
            if (page == 1) rename = !rename;
            break;
        case btn::Shift:
            break;
        default:
            Screen::onButton(id, state);
            break;
    }
    refreshBar();
}

void MixerScreen::onEncoder(int d) {
    change(d);
    refreshBar();
}

float MixerScreen::reductionDb(float meanGain) {
    if (!(meanGain > 0.0f) || meanGain >= 1.0f) return meanGain > 0.0f ? 0.0f : kGrRangeDb;
    const float db = -20.0f * std::log10(meanGain);
    return db > kGrRangeDb ? kGrRangeDb : db;
}

void MixerScreen::update(uint32_t now) {
    if (page == 0 && sub == 3) {
        // Limiter gain-reduction meter (spec 06 §4.3), live.
        if (now - lastGr_ < 50) return;
        lastGr_ = now;
        const float gr = reductionDb(app_.engine.limiterGainReduction());
        if (std::fabs(gr - gainReduction) >= 0.05f) {
            gainReduction = gr;
            refreshBar();
            ui_.dirty();
        }
        return;
    }
    if (page != 1) return;
    // Hold decay: 1 unit per 500 ms (spec 06 §4.4 step 3).
    if (now - lastDecay_ >= 500) {
        lastDecay_ = now;
        for (int& h : meterHold)
            if (h > 0) --h;
    }
    // One level reading per 10 ms (an OpenTracker choice; the engine reports
    // the block peak, scaled by 1/sqrt(2) to the RMS of a sine at that peak
    // so a full-scale sine reads 0 dB after the /0.71 step).
    if (now - lastMeter_ < 10) return;
    lastMeter_ = now;
    histPos_ = (histPos_ + 1) % 10;
    bool changed = false;
    for (int t = 0; t < 8; ++t) {
        hist_[t][histPos_] = app_.engine.trackPeak(t) * 0.70710678f;
        float sum = 0;
        for (float v : hist_[t]) sum += v;
        const int bar = meterBar(sum / 10.0f);
        if (bar != meter[t]) changed = true;
        meter[t] = bar;
        if (bar > meterHold[t]) meterHold[t] = bar;
    }
    if (changed) ui_.dirty();
}

void MixerScreen::draw(Gfx& g) {
    App& a = app_;
    const Theme& th = ui_.th();
    Project& p = a.project;
    static const char* kSub[5] = {"Master 1/3", "Master Reverb", "Master Delay", "Master Limiter", "Master EQ"};
    if (page == 0) {
        ui_.drawTitle(g, kSub[sub]);
        if (sub == 0) {
            // spec 06 §4.2 / VISUAL_DIFF §8.11: Volume bar, one wide panel
            // over F2..F5, the Saturation list, Bass Boost and Space bars;
            // the selected parameter has the column focus frame [S snap 022].
            columnBar(g, th, 0, a.cfg.masterVolume / 100.0f);
            wg::panel(g, th, 102, 30, 397, wg::kPanelH - 1);
            optionList(g, th, 5, kSatNames, 5, clampi(p.u8(pf::kSaturation), 0, 4));
            columnBar(g, th, 6, p.u8(pf::kBassBoost) / 100.0f);
            columnBar(g, th, 7, p.u8(pf::kSpace) / 8.0f);
            columnFocus(g, th, kMainParams[clampi(param, 0, 3)]);
        } else if (sub == 1) {
            // One value bar per parameter (F1..F5), an empty panel over
            // F6..F8 [S round 3, snap 006]. Bars show the stored 0..1 value
            // (Diffusion 0.80 = 80 % of the well; its range ends at 0.85).
            for (int i = 0; i < 4; ++i) columnBar(g, th, i, p.f32(pf::kReverbSize + 4 * uint32_t(i)));
            lineInBar(g, th, 4, p.u8(pf::kLineInReverb), lineInOff(p));
            column(g, th, 5, 3);
            columnFocus(g, th, param);
        } else if (sub == 2) {
            // On/Off and rate lists, bars for the rest [S round 3, snap 007];
            // the Rate list is drawn disabled while Sync is off.
            static const char* const kOnOff[2] = {"On", "Off"};   // [S round 3, snap 007]
            const uint8_t fl = p.u8(pf::kDelayFlags);
            optionList(g, th, 0, kOnOff, 2, (fl & 0x80) ? 0 : 1);
            optionList(g, th, 1, kOnOff, 2, (fl & 0x40) ? 0 : 1);
            optionList(g, th, 2, kDelayRates, 19, clampi(fl & 0x3F, 0, 18), (fl & 0x40) != 0);
            columnBar(g, th, 3, (p.u16(pf::kDelayTime) - 1) / 3499.0f);
            columnBar(g, th, 4, p.u8(pf::kDelayFeedback) / 100.0f);
            columnBar(g, th, 5, (p.u16(pf::kDelayFilter) - 100) / 19900.0f);
            lineInBar(g, th, 6, p.u8(pf::kLineInDelay), lineInOff(p));
            column(g, th, 7);
            columnFocus(g, th, param);
        } else if (sub == 3) {
            // Attack / Release / Threshold bars, the gain-reduction meter
            // above F4 (spec 06 §4.3), the Sidechain list above F5 [S round
            // 3, snap 008 / 012]. The meter is a bar hanging from the top of
            // the well in the cyan slot: at no reduction the 1.5-px line on
            // row 39 (x 313..388, 00a4b3 on Monochrome); it grows downwards.
            columnBar(g, th, 0, (p.u16(pf::kLimAttack) - 1) / 999.0f);
            columnBar(g, th, 1, (p.f32(pf::kLimRelease) - 1.0f) / 999.0f);
            columnBar(g, th, 2, p.u16(pf::kLimThreshold) / 32767.0f);
            column(g, th, 3);
            {
                const uint32_t c = th.slot(slot::kCyan);
                const int fx = colX(3) + 11, fw = 76, top = 39;
                const int h = int(gainReduction / kGrRangeDb * 369.0f + 0.5f);
                wg::blendFill(g, fx, top - 1, fw, 1, c, wg::kEdgeA);
                wg::blendFill(g, fx, top + h + 1, fw, 1, c, wg::kEdgeA);
                g.fill(fx, top, fw, h + 1, c);
            }
            char names[11][24];
            const char* list[11];
            for (int i = 0; i < 11; ++i) {
                sidechainText(p, i, names[i], sizeof(names[i]));
                list[i] = names[i];
            }
            optionList(g, th, 4, list, 11, clampi(p.u8(pf::kSidechain), 0, 10));
            column(g, th, 5, 3);
            columnFocus(g, th, param == 3 ? 4 : param);
        } else {
            // Five band bars filled from the well bottom, -12 dB empty,
            // 0 dB half, +12 dB full [S round 3, snap 011: 0.00 dB = 185 of
            // 370 rows].
            for (int i = 0; i < 5; ++i) columnBar(g, th, i, (p.f32(pf::kEqGain + 4 * uint32_t(i)) + 1.0f) * 0.5f);
            column(g, th, 5, 3);
            columnFocus(g, th, clampi(param, 0, 4));
        }
        return;
    }
    // Muted fader: the value-bar alt slot, 222222 on Original [S snap 024],
    // 303030 on Monochrome [S round 3, snap 010].
    const uint32_t off = th.slot(slot::kValueBarAlt);
    const uint32_t green = th.slot(slot::kGreen);
    if (page == 1) {
        // spec 06 §4.4 / VISUAL_DIFF §8.12: per track a header, a 47-px
        // fader (x 2 + 100 k) and a 47-px level meter (x 52 + 100 k).
        ui_.drawTitle(g, "Track Mixer 2/3", rename ? "RENAME" : nullptr);
        for (int t = 0; t < 8; ++t) {
            const int x = colX(t);
            char nm[24];
            p.trackName(t, nm, sizeof(nm));
            header(g, th, t, nm);
            body(g, th, x, 47);
            body(g, th, x + 50, 47);
            bar(g, th, x, 64, 47, 359, p.u8(pf::kTrackVolume + uint32_t(t)) / 100.0f,
                a.trackMuted(t) ? off : wg::kThemeColour);
            bar(g, th, x + 50, 64, 47, 359, meter[t] / 100.0f, green);
            if (meterHold[t] > meter[t]) {   // peak hold (spec 06 §4.4): a 1-px line [I]
                const int y = 408 - int(334.0f * float(meterHold[t]) / 100.0f + 0.5f) + 1;
                g.fill(x + 61, y, 26, 1, green);
            }
        }
        return;
    }
    // spec 06 §4.5 / VISUAL_DIFF §8.13: four channels, each a 32-px fader and
    // two 32-px meters on one panel; the rest one empty panel. The engine
    // has no per-channel meters here: they rest at level 0 [I].
    ui_.drawTitle(g, "Global Mixer 3/3");
    for (int i = 0; i < 4; ++i) {
        const int x = colX(i);
        header(g, th, i, kGlobalNames[i]);
        body(g, th, x, 96);
        const bool muted = p.u8(kGlobalMute[i]) != 0;
        bar(g, th, x, 64, 32, 359, p.u8(kGlobalLevel[i]) / 100.0f, muted ? off : wg::kThemeColour);
        // A muted channel's meters rest in the muted colour too [S round 3,
        // snap 010 Line In].
        bar(g, th, x + 32, 64, 32, 359, 0.0f, muted ? off : green);
        bar(g, th, x + 64, 64, 32, 359, 0.0f, muted ? off : green);
    }
    wg::panel(g, th, 402, 30, 396, wg::kPanelH - 1);
}

}  // namespace ot
