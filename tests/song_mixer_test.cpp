// Song screen (spec 06 §2) and Master / Mixer (spec 06 §4): the F1
// "Loop Pattern" / "Continue Song" state machine, the Master key combination,
// the Track Mixer meter law and the smaller fixes from the line-by-line pass.
#include <cmath>
#include <cstring>

#include "test.h"
#include "ui_fakes.h"

#include "ui/screens.h"
#include "ui/widgets.h"

using namespace ot;

namespace {
SongScreen& song(Harness& h) { return *static_cast<SongScreen*>(h.app->ui.screen(kModSong)); }
MixerScreen& mixer(Harness& h) { return *static_cast<MixerScreen*>(h.app->ui.screen(kModMaster)); }

// Pump audio until the master step moves on (the next sync point).
void nextStep(Harness& h) {
    const int s0 = h.app->seq.masterStep();
    for (int i = 0; i < 400 && h.app->seq.masterStep() == s0; ++i) h.audio.pump(1);
}
// Pump audio until the song position differs from `pos` or `blocks` run out.
bool posChanges(Harness& h, int pos, int blocks) {
    for (int i = 0; i < blocks; ++i) {
        h.audio.pump(1);
        if (h.app->seq.songPosition() != pos) return true;
    }
    return false;
}
void threeSlotSong(Harness& h) {
    App& a = *h.app;
    a.project.playlist[0] = 1;
    a.project.playlist[1] = 2;
    a.project.playlist[2] = 3;
    a.project.playlist[3] = 0;
}
}  // namespace

// P10: F1 while a song plays is deferred to the next sync point and toggles
// "Loop Pattern" (state 3) / "Continue Song" (state 2) without restarting.
TEST(song_f1_loop_and_continue_at_sync_point) {
    Harness h;
    App& a = *h.app;
    threeSlotSong(h);
    h.tap(btn::Song);
    SongScreen& s = song(h);
    CHECK_STR(a.ui.bar.label[0].top, "Play");
    h.tap(btn::F2);   // play song from slot 0
    CHECK(a.seq.isPlaying() && a.seq.isSongMode());
    CHECK_EQ(s.transportState(), 2);
    CHECK_STR(a.ui.bar.label[0].top, "Loop");
    CHECK_STR(a.ui.bar.label[1].top, "Stop");

    h.audio.pump(10);
    h.tap(btn::F1);   // Loop Pattern: deferred
    CHECK(s.loopTogglePending());
    CHECK(a.seq.isSongMode());   // nothing happens before the sync point
    const int stepBefore = a.seq.step();
    nextStep(h);
    h.run(10);
    CHECK(!s.loopTogglePending());
    CHECK(!a.seq.isSongMode());
    CHECK(a.seq.isPlaying());
    CHECK(a.seq.step() >= stepBefore);   // no restart
    CHECK_EQ(s.transportState(), 3);
    CHECK_STR(a.ui.bar.label[0].top, "Continue");
    CHECK_STR(a.ui.bar.label[1].top, "Stop");

    // The slot loops: past one full pattern the song position stays at 0.
    CHECK(!posChanges(h, 0, 3000));
    CHECK_EQ(a.seq.currentPatternNumber(), 1);

    // Continue Song: resumes at the next sync point, then advances to slot 1
    // at this pattern's end.
    h.run(10);
    h.tap(btn::F1);
    CHECK(!a.seq.isSongMode());
    const int stepAt = a.seq.step();
    nextStep(h);
    h.run(10);
    CHECK(a.seq.isSongMode());
    CHECK(a.seq.step() >= stepAt);   // continues, no restart
    CHECK_EQ(s.transportState(), 2);
    CHECK(posChanges(h, 0, 3000));
    CHECK_EQ(a.seq.songPosition(), 1);
    CHECK_EQ(a.seq.currentPatternNumber(), 2);

    // A second F1 before the sync point schedules the action only once.
    h.run(10);
    h.tap(btn::F1);
    h.tap(btn::F1);
    nextStep(h);
    h.run(10);
    CHECK(!a.seq.isSongMode());
    CHECK_EQ(s.transportState(), 3);

    // Stop clears the loop state.
    h.tap(btn::F2);
    CHECK(!a.seq.isPlaying());
    CHECK(!s.loopingInSong());
    CHECK_EQ(s.transportState(), 0);
}

// F1 on a pattern started here stops (state 5); Shift + Play acts as F1.
TEST(song_f1_pattern_play_and_stop) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Song);
    h.down(btn::Shift);
    h.tap(btn::Play);   // Shift + Play = F1: loop the cursor slot's pattern
    h.up(btn::Shift);
    CHECK(a.seq.isPlaying() && !a.seq.isSongMode());
    CHECK_EQ(song(h).transportState(), 5);
    CHECK_STR(a.ui.bar.label[0].top, "Stop");
    h.tap(btn::F1);
    CHECK(!a.seq.isPlaying());
}

// spec 06 §2.5: Song key held + Play, release -> back to the previous screen.
TEST(song_key_play_returns_to_previous) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Song);
    CHECK(a.ui.active() == kModSong);
    // A plain press/release of the own key stays.
    h.tap(btn::Song);
    CHECK(a.ui.active() == kModSong);
    CHECK_EQ(song(h).focus, 1);   // focus restored
    h.down(btn::Song);
    CHECK_EQ(song(h).focus, 2);
    h.tap(btn::Play);
    CHECK(a.seq.isPlaying());
    h.up(btn::Song);
    CHECK(a.ui.active() == kModPattern);
}

// spec 06 §2.4: the timeout clears only the "TAP n/4" display; the next tap
// abandons the series (count 0), so it does not start a new one.
TEST(song_tap_timeout_keeps_count) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Song);
    SongScreen& s = song(h);
    h.tap(btn::F8);   // focus tempo
    h.tap(btn::F8);   // tap 1
    h.run(300);
    h.tap(btn::F8);   // tap 2
    CHECK_EQ(s.tapCount, 2);
    CHECK_STR(a.ui.bar.label[7].top, "TAP 2/4");
    h.run(4000);      // > 3*1000 + 1000 ms since t0 (the abandon check uses k+1)
    CHECK(!s.tapShown);
    CHECK_EQ(s.tapCount, 2);
    CHECK_STR(a.ui.bar.label[7].top, "Tempo");
    h.tap(btn::F8);   // abandons
    CHECK_EQ(s.tapCount, 0);
    // Four taps 500 ms apart -> 180000 / 1500 = 120 BPM.
    for (int i = 0; i < 4; ++i) {
        if (i) h.run(500 - 40);
        h.tap(btn::F8);
    }
    CHECK_NEAR(a.project.tempo(), 120.0, 0.5);
    CHECK_EQ(s.tapCount, 1);
}

// spec 06 §2.3 F6/F7: changing the playing slot's pattern reloads it.
TEST(song_f7_reloads_playing_slot) {
    Harness h;
    App& a = *h.app;
    threeSlotSong(h);
    h.tap(btn::Song);
    h.tap(btn::Rec);
    h.tap(btn::F2);
    CHECK_EQ(a.seq.currentPatternNumber(), 1);
    h.tap(btn::F7);   // slot 0: pattern 1 -> 2
    CHECK_EQ(a.project.playlist[0], 2);
    CHECK_EQ(a.seq.currentPatternNumber(), 2);
    CHECK(a.seq.isPlaying() && a.seq.isSongMode());
    CHECK_EQ(a.seq.songPosition(), 0);
}

// Grid selection: Shift + Down extends, Shift + Up shrinks (like Shift + Right).
TEST(song_grid_shift_extends_down) {
    Harness h;
    threeSlotSong(h);
    h.tap(btn::Song);
    SongScreen& s = song(h);
    h.tap(btn::Rec);
    h.tap(btn::Left);   // list -> grid
    CHECK_EQ(s.focus, 0);
    h.tap(btn::Up);
    h.tap(btn::Up);
    CHECK_EQ(s.selRow0, 0);
    h.down(btn::Shift);
    h.tap(btn::Down);
    CHECK_EQ(s.selRows, 2);
    h.tap(btn::Down);
    CHECK_EQ(s.selRows, 3);
    h.tap(btn::Up);
    CHECK_EQ(s.selRows, 2);
    h.up(btn::Shift);
}

// P17: Master key combination.
// spec 20 §4.3 / VISUAL_DIFF §3.9: stock Song layout. Strip mask 0xbf, the
// bar model keeps every label's text (labels 0..5 are drawn by the screen in
// the both-grey set), a 12-row window with a scrollbar for longer songs, and
// the worst-case frame (16 slots, custom fonts, grid selection, playing)
// stays inside the 2048-word RAM_DL.
TEST(song_stock_layout_and_dl_budget) {
    Harness h(false);
    h.disp.customFonts = true;
    h.app->boot();
    App& a = *h.app;
    CHECK(customFontsBound());
    for (int i = 0; i < 16; ++i) a.project.playlist[i] = uint8_t(1 + i % 4);
    a.project.playlist[16] = 0;
    for (int n = 1; n <= 4; ++n) {
        for (int t = 0; t < kTrackCount; t += n) a.stored(n).tracks[t].steps[0].note = 60;
        a.markPatternDirty(n);
    }
    h.tap(btn::Song);
    SongScreen& s = song(h);
    CHECK_EQ(int(a.ui.bar.separators & 0xFE), 0xBE);
    CHECK_STR(a.ui.bar.label[0].bottom, "Pattern");
    CHECK_STR(a.ui.bar.label[1].bottom, "Song");
    CHECK_STR(a.ui.bar.label[3].bottom, "Slot");
    CHECK(a.ui.bar.label[5].wide);
    CHECK_STR(a.ui.bar.label[5].top, "Pattern");
    CHECK(a.ui.bar.label[2].hidden && !a.ui.bar.label[2].enabled);
    CHECK(!a.ui.bar.label[7].hidden);
    CHECK_EQ(int(a.ui.bar.label[4].underline), int(wg::kUnderlineRed));
    h.tap(btn::Rec);   // edit: the edit-only keys turn available (green)
    CHECK(s.edit);
    CHECK(a.ui.bar.label[2].enabled);
    CHECK_EQ(int(a.ui.bar.label[4].underline), int(wg::kUnderlineGreen));
    for (int i = 0; i < 13; ++i) h.tap(btn::Down);
    CHECK_EQ(s.cursor, 13);
    h.tap(btn::Left);   // grid focus with a selection
    CHECK_EQ(s.focus, 0);
    h.tap(btn::F2);     // play song
    a.ui.dirty();
    h.run(40);
    std::printf("song worst-case frame: ~%u DL words\n", unsigned(a.ui.lastDlWords()));
    CHECK(a.ui.lastDlWords() < 2048);
    // The header text is turned (bitmap transform inside a saved context).
    bool saved = false;
    for (auto& l : h.disp.last) saved |= l == "SAVE_CONTEXT";
    CHECK(saved);
    h.tap(btn::F2);
    applyFontSet(nullptr, 0);   // global font state back to the ROM fallback
}

TEST(master_key_combination) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Master);   // enter (press consumed by the switch)
    MixerScreen& m = mixer(h);
    CHECK(a.ui.active() == kModMaster);
    h.tap(btn::Master);
    CHECK_EQ(m.page, 1);
    // Hold alone: jump to the Master main page; the release does not cycle.
    h.down(btn::Master);
    h.run(400);
    CHECK_EQ(m.page, 0);
    h.up(btn::Master);
    CHECK_EQ(m.page, 0);
    CHECK(a.ui.active() == kModMaster);
    // Back to the Track Mixer, then hold + another key: snapshot restored and
    // event 5 (previous screen).
    h.tap(btn::Master);
    CHECK_EQ(m.page, 1);
    h.down(btn::Master);
    h.run(400);           // hold tick -> Master main page
    CHECK_EQ(m.page, 0);
    h.tap(btn::F2);       // Reverb sub-page
    CHECK_EQ(m.sub, 1);
    h.up(btn::Master);
    CHECK(a.ui.active() == kModPattern);
    CHECK_EQ(m.page, 1);
    CHECK_EQ(m.sub, 0);
    // Quick press + other key + release (no hold) also returns.
    h.tap(btn::Master);
    CHECK(a.ui.active() == kModMaster);
    h.down(btn::Master);
    h.tap(btn::F1);
    h.up(btn::Master);
    CHECK(a.ui.active() == kModPattern);
    CHECK_EQ(m.page, 1);
}

// spec 06 §4.4 meter law.
TEST(mixer_meter_law) {
    // log10 units: -3.6..+0.6 = -72..+12 dB.
    auto lvl = [](float db) { return 0.71f * std::pow(10.0f, db / 20.0f); };
    CHECK_EQ(MixerScreen::meterBar(0.0f), 0);
    CHECK_EQ(MixerScreen::meterBar(lvl(-73.0f)), 0);
    CHECK_EQ(MixerScreen::meterBar(0.71f), 85);   // 0 dB: 3.6 / 4.2
    CHECK_EQ(MixerScreen::meterBar(lvl(-30.0f)), 50);
    CHECK_EQ(MixerScreen::meterBar(lvl(13.0f)), 100);
    // Ordinary track levels move the meter (stock 1.9.2 photo, user report).
    CHECK(MixerScreen::meterBar(lvl(-20.0f)) > 55 && MixerScreen::meterBar(lvl(-20.0f)) < 65);
    CHECK(MixerScreen::meterBar(lvl(-35.0f)) > 0);

    Harness h;
    h.tap(btn::Master);
    h.tap(btn::Master);
    MixerScreen& m = mixer(h);
    CHECK_EQ(m.page, 1);
    m.meterHold[0] = 50;
    h.run(1600);   // silence: 3 decays of 1 unit per 500 ms
    CHECK(m.meterHold[0] >= 46 && m.meterHold[0] <= 47);
    CHECK_EQ(m.meter[0], 0);
}

// spec 06 §4.5: Global Mixer solo follows §4.4 (same key again unsolos).
TEST(global_mixer_solo_toggle) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Master);
    h.tap(btn::Master);
    h.tap(btn::Master);
    CHECK_EQ(mixer(h).page, 2);
    h.down(btn::Shift);
    h.tap(btn::F2);
    int muted = 0;
    for (int i = 0; i < 4; ++i) muted += a.ui.bar.label[i].bottom[0] == 'U';
    h.up(btn::Shift);
    CHECK_EQ(muted, 3);
    CHECK_STR(a.ui.bar.label[1].bottom, "Mute");
    h.down(btn::Shift);
    h.tap(btn::F2);
    h.up(btn::Shift);
    for (int i = 0; i < 4; ++i) CHECK_STR(a.ui.bar.label[i].bottom, "Mute");
}

#include "formats/wav.h"

// Global Mixer 3/3 meters show each channel's level (they rested at 0): a
// note with delay and reverb sends moves Dry, Delay and Reverb, Line In
// stays at 0 without input.
TEST(global_mixer_meters_follow_the_buses) {
    Harness h;
    App& a = *h.app;
    std::vector<int16_t> s(88200);
    for (size_t i = 0; i < s.size(); ++i) s[i] = int16_t(20000.0 * std::sin(double(i) * 0.06));
    std::vector<uint8_t> f(s.size() * 2 + 64);
    f.resize(wavEncode(s.data(), uint32_t(s.size()), 1, f.data(), f.size()));
    h.fs.files["tone.wav"] = f;
    CHECK(a.importSample("tone.wav", 0));
    a.instruments[0].delaySend = 100;
    a.instruments[0].reverbSend = 100;
    a.markInstrumentDirty(0, false);
    a.pattern().tracks[0].steps[0].note = 60;
    a.pattern().tracks[0].steps[0].instrument = 0;
    a.patternEdited();
    h.tap(btn::Master);
    h.tap(btn::Master);
    h.tap(btn::Master);
    MixerScreen& m = mixer(h);
    CHECK_EQ(m.page, 2);
    a.seq.play();
    int mx[4][2] = {};
    uint32_t acc = 0;
    for (uint32_t t = 0; t < 1500; t += 10) {
        acc += 10u * 44100u;
        int blocks = 0;
        while (acc >= 128000u) {
            acc -= 128000u;
            ++blocks;
        }
        h.audio.pump(blocks);
        h.run(10);
        for (int c = 0; c < 4; ++c)
            for (int sd = 0; sd < 2; ++sd) mx[c][sd] = std::max(mx[c][sd], m.globalMeter[c][sd]);
    }
    std::printf("  global meters: delay %d/%d reverb %d/%d dry %d/%d line %d/%d\n", mx[0][0], mx[0][1], mx[1][0], mx[1][1],
                mx[2][0], mx[2][1], mx[3][0], mx[3][1]);
    CHECK(mx[0][0] > 0 && mx[0][1] > 0);   // delay
    CHECK(mx[1][0] > 0 && mx[1][1] > 0);   // reverb
    CHECK(mx[2][0] > 0 && mx[2][1] > 0);   // dry
    CHECK_EQ(mx[3][0] + mx[3][1], 0);      // no line input
}
