// Sample Recorder (spec 05 §2.11, appendix C §2; spec 02 §8.3): input tap,
// sources, gain, meter, count-in, record/stop, auto-trim, crop + undo,
// naming / folders, save and save & load.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "test.h"
#include "ui_fakes.h"

#include "app/input_gain.h"
#include "formats/wav.h"
#include "ui/screens.h"

using namespace ot;

namespace {
RecorderScreen& rec(Harness& h) { return *static_cast<RecorderScreen*>(h.app->ui.screen(kModRecorder)); }

// Tone (440 Hz, amplitude 10000) in blocks [on, off), silence elsewhere.
void toneBlocks(Harness& h, uint64_t on, uint64_t off) {
    h.lineGen = [on, off](int16_t* b, uint64_t k) {
        for (int i = 0; i < 128; ++i) {
            const double t = double(k * 128 + uint64_t(i)) / 44100.0;
            const int16_t v = k >= on && k < off ? int16_t(10000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * t)) : 0;
            b[2 * i] = b[2 * i + 1] = v;
        }
    };
}

// Input flowing while the UI runs: ~3 audio blocks per 10 ms loop slice, as
// on the device (the meter reads the peak since its last frame).
void live(Harness& h, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        h.audio.pump(3);
        h.run(10);
    }
}

void recordSome(Harness& h, uint32_t blocks) {
    h.tap(btn::F8);   // Record
    CHECK(h.app->capture.recording());
    h.lineBlocks = 0;
    h.audio.pump(int(blocks));
    h.tap(btn::F8);   // Stop
}
}  // namespace

TEST(recorder_record_trim_crop_undo) {
    Harness h;
    App& a = *h.app;
    toneBlocks(h, 10, 40);
    h.tap(btn::SampleRecorder);
    CHECK(a.ui.active() == kModRecorder);
    RecorderScreen& r = rec(h);
    CHECK_EQ(r.state, 0);
    CHECK_STR(a.ui.bar.label[0].top, "Source");
    CHECK_STR(a.ui.bar.label[0].bottom, "Line in");
    CHECK_STR(a.ui.bar.label[7].top, "Record");
    CHECK(r.sourceOk);
    recordSome(h, 60);
    CHECK(!a.capture.active());
    CHECK_EQ(r.state, 1);
    CHECK_EQ(r.len, 60u * 128u);
    CHECK_STR(a.ui.bar.label[4].top, "Crop");
    // Auto-trim (appendix C §2.10): first frame above -70 dBFS minus 44 frames.
    const uint32_t s0 = uint32_t(uint64_t(r.startM) * r.len / 65535);
    const uint32_t s1 = uint32_t(uint64_t(r.endM) * r.len / 65535);
    CHECK(s0 >= 10 * 128 - 44 - 2 && s0 <= 10 * 128 - 44 + 3);
    CHECK(s1 > 40 * 128 + 44 && s1 < 40 * 128 + 1500);
    // Crop (b4) keeps the selection; Undo (b5) restores it.
    h.tap(btn::F5);
    CHECK_EQ(r.len, s1 - s0);
    CHECK_EQ(r.undoDepth, 1);
    CHECK_EQ(r.startM, 0);
    CHECK_EQ(r.endM, 65535);
    h.tap(btn::F6);
    CHECK_EQ(r.len, 60u * 128u);
    CHECK_EQ(r.undoDepth, 0);
    // Undo depth is capped at 5 levels.
    for (int i = 0; i < 7; ++i) {
        r.startM = 1000;
        h.tap(btn::F5);
    }
    CHECK_EQ(r.undoDepth, RecorderScreen::kUndoLevels);
    for (int i = 0; i < 7; ++i) h.tap(btn::F6);
    CHECK_EQ(r.undoDepth, 0);
    CHECK(r.len < 60u * 128u);   // the oldest levels were dropped
    // Cancel (b6) asks; Continue discards and returns to the setup state.
    h.tap(btn::F7);
    CHECK(a.ui.askOpen());
    h.tap(btn::F8);
    CHECK_EQ(r.state, 0);
}

TEST(recorder_autotrim_unit) {
    std::vector<int16_t> d(10000, 0);
    uint32_t s = 0, e = 0;
    autoTrim(d.data(), uint32_t(d.size()), &s, &e);
    CHECK_EQ(s, 0u);   // silence: full range kept
    CHECK_EQ(e, 10000u);
    for (int i = 3000; i < 4000; ++i) d[size_t(i)] = 8000;
    autoTrim(d.data(), uint32_t(d.size()), &s, &e);
    CHECK_EQ(s, 3000u - 44u);
    CHECK(e > 4000u + 44u && e < 4900u);
    CHECK_EQ(meterUnits(1.0f), 100);
    CHECK_EQ(meterUnits(0.0f), 0);
    CHECK(meterUnits(0.5f) > 70 && meterUnits(0.5f) < 85);
    CHECK(meterUnits(0.9f) > 85 && meterUnits(0.9f) < 100);
    // spec 22 §3: the gain is analog (codec level), stock integer scale.
    CHECK_EQ(input_gain::lineLevel(0), 0);
    CHECK_EQ(input_gain::lineLevel(50), 7);
    CHECK_EQ(input_gain::lineLevel(100), 15);
    CHECK_EQ(input_gain::lineLevel(7), 1);
    CHECK_EQ(input_gain::micDb(2, 50), 12);
    CHECK_EQ(input_gain::micDb(2, 100), 25);
    CHECK_EQ(input_gain::micDb(3, 50), 22);
    CHECK_EQ(input_gain::micDb(3, 100), 44);
}

TEST(recorder_save_and_load) {
    Harness h;
    App& a = *h.app;
    toneBlocks(h, 5, 30);
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    recordSome(h, 40);
    h.tap(btn::F5);   // crop to the trimmed range
    const uint32_t frames = r.len;
    h.tap(btn::F8);   // Save -> naming keyboard
    CHECK_EQ(r.state, 2);
    CHECK(a.ui.kb.isOpen());
    CHECK_STR(a.ui.kb.text(), "recording001");
    h.tap(btn::F7);   // Save & Load
    CHECK(!a.ui.kb.isOpen());
    CHECK(h.fs.dirs.count("Recordings"));
    CHECK(h.fs.files.count("Recordings/recording001.wav"));
    WavInfo info;
    const auto& w = h.fs.files["Recordings/recording001.wav"];
    CHECK(wavParseHeader(w.data(), w.size(), info));
    CHECK_EQ(info.channels, 1);
    CHECK_EQ(info.bitsPerSample, 16);
    CHECK_EQ(info.sampleRate, 44100u);
    CHECK_EQ(info.dataSize, frames * 2);
    // Loaded into the first free slot; the loader shows it.
    CHECK(a.instrPresent[0]);
    CHECK_EQ(a.bank.frames(0), frames);
    CHECK_STR(a.instruments[0].name, "recording001");
    CHECK_EQ(a.currentInstrument(), 0);
    CHECK(a.ui.active() == kModLoader);

    // Second recording: plain Save -> recording002; then a name clash.
    h.tap(btn::SampleRecorder);
    recordSome(h, 40);
    h.tap(btn::F8);
    CHECK_STR(a.ui.kb.text(), "recording002");
    h.tap(btn::F8);   // Save
    CHECK(h.fs.files.count("Recordings/recording002.wav"));
    CHECK_EQ(r.state, 1);
    CHECK(!a.instrPresent[1]);
    CHECK(!r.save("recording002", false, false));   // exists -> overwrite prompt
    CHECK(a.ui.askOpen());
    h.tap(btn::F7);   // Rename -> first free "<name><k>"
    CHECK(a.ui.kb.isOpen());
    CHECK_STR(a.ui.kb.text(), "recording0021");
    h.tap(btn::F8);
    CHECK(h.fs.files.count("Recordings/recording0021.wav"));
    CHECK(r.save("recording002", false, false) == false);
    h.tap(btn::F8);   // Overwrite
    CHECK(!a.ui.askOpen());
}

TEST(recorder_folders_and_names) {
    Harness h;
    App& a = *h.app;
    char folder[32], name[32];
    CHECK(a.recordingsFolder(folder, sizeof(folder)));
    CHECK_STR(folder, "Recordings");
    h.fs.files["Recordings/recording001.wav"] = {1};
    CHECK(a.recordingName(folder, name, sizeof(name)));
    CHECK_STR(name, "recording002");
    // Highest "Recordings N" is used; > 98 entries starts the next one.
    h.fs.dirs.insert("Recordings 2");
    CHECK(a.recordingsFolder(folder, sizeof(folder)));
    CHECK_STR(folder, "Recordings 2");
    for (int i = 0; i < 99; ++i) h.fs.files["Recordings 2/f" + std::to_string(i) + ".wav"] = {1};
    CHECK(a.recordingsFolder(folder, sizeof(folder)));
    CHECK_STR(folder, "Recordings 3");
    CHECK(h.fs.dirs.count("Recordings 3"));
}

// User report (2.0.1, also 1.9.4): "metering on the recording page seems a bit
// slow". The meter was read every 160 ms and fell 1 unit per 500 ms, so a
// peak kept the bar up for tens of seconds. Stock reads it every frame and
// lets it fall one unit per pass (> 500 us, appendix C §2.6 / appendix A
// timers): it must drop to the new level within ~0.1 s.
TEST(recorder_meter_follows_input_at_frame_rate) {
    CHECK_EQ(RecorderScreen::meterFall(90, 95, 16), 95);   // instant rise
    CHECK_EQ(RecorderScreen::meterFall(90, 10, 16), 74);   // 1 unit per ms
    CHECK_EQ(RecorderScreen::meterFall(90, 10, 500), 10);  // never below the reading
    CHECK(RecorderScreen::kMeterMs <= 20);                   // >= 50 readings / s
    Harness h;
    bool loud = true;
    h.lineGen = [&loud](int16_t* b, uint64_t) {
        for (int i = 0; i < 256; ++i) b[i] = loud ? int16_t((i / 2) % 2 ? 30000 : -30000) : 0;
    };
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    live(h, 100);
    CHECK(r.meter > 85);
    loud = false;
    live(h, 150);   // 150 ms of silence
    CHECK_EQ(r.meter, 0);
}

TEST(recorder_sources_gain_monitor_meter) {
    Harness h;
    App& a = *h.app;
    h.lineGen = [](int16_t* b, uint64_t) {
        for (int i = 0; i < 256; ++i) b[i] = int16_t((i / 2) % 2 ? 30000 : -30000);
    };
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    // Meter follows the input.
    live(h, 200);
    CHECK(r.meter > 85);
    // b0 again = next source: radio (no tuner -> not available), mics (no routing hook).
    h.tap(btn::F1);
    CHECK_EQ(a.cfg.recSource, 1);
    CHECK(!r.sourceOk);
    CHECK_STR(a.ui.bar.label[0].bottom, "n/a");
    CHECK_STR(a.ui.bar.label[1].top, "Radio Freq");
    h.tap(btn::F8);   // Record refused
    CHECK(!a.capture.active());
    h.tap(btn::F1);
    CHECK_EQ(a.cfg.recSource, 2);
    h.tap(btn::F1);
    h.tap(btn::F1);
    CHECK_EQ(a.cfg.recSource, 0);   // wraps
    CHECK(r.sourceOk);
    // Gain (b5) +10.
    h.tap(btn::F6);
    h.enc(10);
    CHECK_EQ(a.cfg.recGain[0], 60);
    // Monitor (b6): second press toggles; off keeps the input out of the mix.
    CHECK(!a.inputMonitorMuted);
    h.tap(btn::F7);
    h.tap(btn::F7);
    CHECK_EQ(a.cfg.recMonitor, 0);
    CHECK(a.inputMonitorMuted);
    h.tap(btn::Pattern);
    CHECK(!a.inputMonitorMuted);   // leaving restores the monitor path
}

TEST(recorder_count_in_and_memory_full) {
    // Small sample bank: 60000 samples of free memory.
    Harness h(true, 60000);
    App& a = *h.app;
    toneBlocks(h, 0, 100000);
    a.cfg.metronome = 1;
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    h.tap(btn::Rec);
    CHECK(a.capture.state() == Capture::kCountIn);
    // One bar of (tsNum+1)*(tsDen+1) steps at the project tempo.
    const double stepSecs = 15.0 / double(a.project.tempo());
    const uint32_t countFrames = uint32_t((a.cfg.tsNum + 1) * (a.cfg.tsDen + 1) * uint32_t(stepSecs * 44100.0 + 0.5));
    h.audio.pump(int(countFrames / 128) - 1);
    CHECK(a.capture.state() == Capture::kCountIn);
    h.audio.pump(2);
    CHECK(a.capture.recording());
    CHECK(a.capture.length() > 0 && a.capture.length() <= 256);
    // Memory full: the tap stops, the screen reports it.
    h.audio.pump(600);
    CHECK(!a.capture.active());
    CHECK(a.capture.full());
    h.run(50);
    CHECK_EQ(r.state, 1);
    CHECK_EQ(r.len, a.capture.capacity());
    CHECK(a.ui.askOpen());
    h.tap(btn::F8);
    CHECK(!a.ui.askOpen());
}

TEST(recorder_forces_midi_instrument) {
    // appendix C §2.2: entry forces the current instrument into 48..63 and
    // remembers the last MIDI instrument used there.
    Harness h;
    App& a = *h.app;
    a.setCurrentInstrument(3);
    h.tap(btn::SampleRecorder);
    CHECK_EQ(a.currentInstrument(), kFirstMidiInstrument);
    a.setCurrentInstrument(kFirstMidiInstrument + 5);
    h.tap(btn::Pattern);
    a.setCurrentInstrument(7);
    h.tap(btn::SampleRecorder);
    CHECK_EQ(a.currentInstrument(), kFirstMidiInstrument + 5);
    h.tap(btn::Pattern);
    a.setCurrentInstrument(kFirstMidiInstrument + 9);   // already MIDI: kept
    h.tap(btn::SampleRecorder);
    CHECK_EQ(a.currentInstrument(), kFirstMidiInstrument + 9);
}

TEST(recorder_metronome_opens_config_and_returns) {
    // appendix C §2.9: F4 Metronome opens Config remembering the recorder;
    // the return start argument comes back to the setup state.
    Harness h;
    App& a = *h.app;
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    CHECK_STR(a.ui.bar.label[3].top, "Metronome");
    h.tap(btn::F4);
    CHECK(a.ui.active() == kModConfig);
    CHECK(r.returnsFromConfig());
    a.ui.switchTo(kModRecorder, kModConfig);
    CHECK(a.ui.active() == kModRecorder);
    CHECK(!r.returnsFromConfig());
    CHECK_EQ(r.state, 0);
    CHECK_EQ(r.sel, 3);
}

TEST(recorder_pads_play_chromatically) {
    // appendix C §2.11: b0 Preview plays at note 60 (own pitch); pads play
    // the selection through the pad -> note map, release stops.
    Harness h;
    App& a = *h.app;
    toneBlocks(h, 0, 1000);
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    recordSome(h, 40);
    r.startM = 0;
    r.endM = 65535;
    const uint32_t n = r.len;
    h.down(btn::F1);
    CHECK(a.engine.previewBufferActive());
    CHECK_EQ(r.playNote, 60);
    CHECK_EQ(r.playFrames, n);
    h.up(btn::F1);
    CHECK(!a.engine.previewBufferActive());
    CHECK_EQ(r.playNote, -1);
    for (uint8_t p : {uint8_t(0), uint8_t(20), uint8_t(47)}) {
        const int note = a.padNote(p);
        h.push(InputEvent::kPadDown, p);
        CHECK(a.engine.previewBufferActive());
        CHECK_EQ(r.playNote, note);
        const double expect = double(n - 1) / std::pow(2.0, double(note - 60) / 12.0) + 1.0;
        CHECK(std::fabs(double(r.playFrames) - expect) <= 2.0);
        h.push(InputEvent::kPadUp, p);
        CHECK(!a.engine.previewBufferActive());
    }
    // Two pads held: the last one sounds; releasing the first keeps it.
    h.push(InputEvent::kPadDown, 10);
    h.push(InputEvent::kPadDown, 30);
    CHECK_EQ(r.playNote, int(a.padNote(30)));
    h.push(InputEvent::kPadUp, 10);
    CHECK(a.engine.previewBufferActive());
    h.push(InputEvent::kPadUp, 30);
    CHECK(!a.engine.previewBufferActive());
}

TEST(recorder_midi_notes_play_chromatically) {
    // spec 17 §1.3 owner 4: incoming notes use the recorder preview, like
    // its pads (note 60 = own pitch); the last note sounds.
    Harness h;
    App& a = *h.app;
    a.cfg.notesIn = 1;
    toneBlocks(h, 0, 1000);
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    recordSome(h, 40);
    r.startM = 0;
    r.endM = 65535;
    auto send = [&](uint8_t st, uint8_t n, uint8_t v) {
        h.midi.in.insert(h.midi.in.end(), {st, n, v});
        h.run(5);
    };
    send(0x90, 72, 100);
    CHECK(a.engine.previewBufferActive());
    CHECK_EQ(r.playNote, 72);
    CHECK_EQ(a.auditionTrack(uint16_t(App::kAuditionMidiIn + 72)), -1);   // no track voice
    const double expect = double(r.len - 1) / 2.0 + 1.0;
    CHECK(std::fabs(double(r.playFrames) - expect) <= 2.0);
    send(0x90, 60, 100);
    CHECK_EQ(r.playNote, 60);
    CHECK_EQ(r.playFrames, r.len);
    send(0x80, 72, 0);   // not the sounding note
    CHECK(a.engine.previewBufferActive());
    send(0x90, 60, 0);   // velocity-0 note-on = note-off
    CHECK(!a.engine.previewBufferActive());
    CHECK_EQ(r.playNote, -1);
}

// ---- Radio source (spec 01 §5.4-§5.6, spec 05 appendix C §2.4/§2.8) ---------

namespace {
struct FakeTuner final : IRadio {
    float lo = 0, hi = 0, step = 0, freq = 87.5f;
    int tunes = 0;
    void setBand(float a, float b, float s) override { lo = a; hi = b; step = s; }
    bool tune(float mhz) override { freq = mhz; ++tunes; return true; }
    bool seek(bool) override { return true; }
    bool busy() const override { return false; }
    float frequency() const override { return freq; }
};
struct FakeRouting {
    int calls = 0, last = -1;
    std::vector<int> sources;
    static bool fn(void* c, uint8_t src, uint8_t) {
        auto* r = static_cast<FakeRouting*>(c);
        ++r->calls;
        r->last = src;
        r->sources.push_back(src);
        return true;
    }
};
}  // namespace

TEST(recorder_radio_source_powers_tuner_routes_input_and_records) {
    Harness h;
    App& a = *h.app;
    FakeTuner tuner;
    FakeRouting route;
    a.setInputRouting(&FakeRouting::fn, &route, &tuner);
    h.run(20);   // project recorder settings -> working copies
    a.cfg.radioFreq10 = 1011;
    h.run(20);   // -> project 0x13C (spec 02 §3.3)
    CHECK_EQ(a.cfg.radioFreq10, 1011);
    toneBlocks(h, 0, 1u << 30);
    h.tap(btn::SampleRecorder);
    RecorderScreen& r = rec(h);
    CHECK_EQ(route.last, 0);   // line in first
    // b0: Radio -> the HAL routes source 1 (tuner on, codec LINE input), the
    // screen sets the region band and tunes the stored frequency.
    h.tap(btn::F1);
    CHECK_EQ(a.cfg.recSource, 1);
    CHECK_EQ(route.last, 1);
    CHECK(r.sourceOk);
    CHECK_STR(a.ui.bar.label[0].bottom, "Radio");
    CHECK_NEAR(tuner.lo, 87.5, 1e-6);
    CHECK_NEAR(tuner.freq, 101.1, 1e-4);
    CHECK(tuner.tunes >= 1);
    // The tuner's audio arrives as the line input: meter and capture follow it.
    live(h, 200);
    CHECK(r.meter > 0);
    recordSome(h, 40);
    CHECK(a.capture.length() > 0);
    int peak = 0;
    for (uint32_t i = 0; i < a.capture.length(); ++i) peak = std::max(peak, std::abs(int(a.capture.buffer()[i])));
    CHECK(peak > 1000);
    // While Radio stays the source the screen re-routes it about once a second
    // (the HAL re-powers a tuner that dropped out after an I2C failure).
    const int before = route.calls;
    h.run(2100);
    CHECK(route.calls >= before + 2);
    CHECK_EQ(route.last, 1);
    // Leaving the recorder routes the line input again (tuner off).
    h.tap(btn::Pattern);
    CHECK_EQ(route.last, 0);
}
