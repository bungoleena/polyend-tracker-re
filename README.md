# OpenTracker

A clean-room reimplementation of the original Polyend Tracker firmware
(NXP MK66FX1M0), written from behavioural specs in `../docs/specs/`. It
contains no vendor code, and it runs on real hardware.

## Licence

OpenTracker's own code is MIT (see `LICENSE` at the repo root); third-party code keeps its own licence.

## Screenshots

Captured on a real Tracker running OpenTracker 1.9.3 (Shift + Power):

| Song | Config: firmware |
|---|---|
| ![Song screen](../docs/screenshots/song.png) | ![Config, firmware page](../docs/screenshots/config-firmware.png) |

![Games](../docs/screenshots/games.png)

## What to install

Everything the firmware needs is in `third_party/`: the PJRC Teensy core,
FatFs, and the font data for the simulator. Nothing else has to be downloaded
except the tools below.

| Tool | Needed for | Tested version | Arch / Debian package |
|---|---|---|---|
| `arm-none-eabi-gcc` + newlib (incl. `nano.specs`) | firmware | 16.2 (10.3 or newer should work) | `arm-none-eabi-gcc arm-none-eabi-newlib` / `gcc-arm-none-eabi libnewlib-arm-none-eabi` |
| GNU make | everything | 4.4 | `make` |
| Python 3 | image check (`tools/check_image.py`) | 3.14 | `python` / `python3` |
| g++ (C++17, with AddressSanitizer) | host tests | 16.2 | `gcc` / `g++` |
| SDL2 dev files | simulator | 2.32 | `sdl2` / `libsdl2-dev` |
| ALSA dev files (optional) | simulator MIDI ports | 1.2 | `alsa-lib` / `libasound2-dev` |

For the device itself you only need the stock firmware file
`PolyendTracker_1.9.2.ptf` from polyend.com, as the way back.

## Build

```
make                 # firmware  -> build/PolyendTrackerOTv196.ptf (OpenTracker 1.9.6)
make VERSION=0.2.0   # set the version (the file name stays 24 characters)
make SELFTEST=1      # hardware selftest -> build/selftest/PolyendTracker_OTst<N>.ptf
make -C tests        # host unit tests
make sim             # desktop simulator -> build/sim/opentracker-sim (see sim/README.md)
make clean
```

Every image goes through `tools/check_image.py`. It refuses an image unless:
- its flash-security bytes match stock, so the chip can't be locked;
- it stays inside the stock flash range;
- its name is exactly 24 characters.

## Install

Put the `.ptf` in `/Firmware` on the SD card. Install it with Config >
Firmware update, or write its name into `/Firmware/ot_restore.txt` and
power-cycle. Go back to stock the same way with `PolyendTracker_1.9.2.ptf`.

## Differences from the original

- Image names are exactly 24 characters, because the bootloader ignores longer names.
- At boot the SD card is checked first for `ot_restore.txt`, so there is always a way back to stock.
- USB identity is 1209:0001 "OpenTracker".
- Screens follow the stock layout, colours and font metrics pixel for pixel, but the fonts are open substitutes (Roboto Mono, OFL; Roboto, Apache 2.0) and the icons are our own. Note names are drawn without a dash ("C5", "C#5"), as stock draws them.
- Hardware init fixes: I2S clock divider and start order, pad-LED I2C timeout.
- Diagnostics over USB serial.
- Saving replaces only the project's pattern, metadata and instrument files; it does not wipe its sample folders.
- Opening old (1.3-era) projects imports their WAV samples straight from the project folder instead of copying them into `Workspace/samples` first; the result is the same.
- Project open, save and autosave run as background jobs with a progress popup. A file that cannot be read is reported, never loaded silently as empty.
- No Polyend branding, help text or fonts.
- Config help texts (shown while a value is edited) are OpenTracker's own wording; the option and value labels are the stock ones.
- OpenTracker's own, where the specs only give meanings:
  - the metronome click;
  - the Sample Editor effect details that spec 18 §2.3 leaves open: the EQ peaking-band coefficients (RBJ, Q = f/bw) and its dB → linear gain conversion, the flanger's per-sample tap (offset + depth·sin, linearly interpolated), the beat time-stretch target length ((steps + micro step) sixteenths at the tempo) and the WSOLA overlap/seek sizes, the note time-stretch granule length (granule ms · 44.1 samples), the flanger mix ((x + tap) / 2), the Normalizer on a silent selection (copied unchanged; stock divides by a zero peak), the fade start/end values (linear 0→1 and 1→0 over the selection), overdrive type 0 above 2/3 FS (clip), the limiter's pre-smoothing coefficient, and the overdrive type labels. Everything else in the 17 effects follows stock: the formulas, parameter conversions, truncation with int16 saturation, the multi-tap Delay that lengthens the sample, the bitcrusher hold, PJRC-style chorus taps, limiter/compressor envelopes, wavetable-smoother crossfades and the one-level undo;
  - the auto-name word list (stock makes a unique `name_NNNN`); the naming keyboard's key layout, labels and text cursor are stock (spec 16 §B.1);
  - the picker look (including the FX description panel, whose state is kept until power-off rather than in the config) and the MIDI-Mapping hint text;
- The headphone level is fixed at 0.7; the stock level is unknown.
- The Loader's SD preview decodes into free sample memory, so there is no preview when memory is full; stock streams it.
- Master EQ, Bass Boost and Space run in the SGTL5000 codec's DSP (DAP), as in stock 1.9.2 (spec 18 §1): the same register values are written at boot, on every edit and on project load. Because the DAP sits after the mix, offline renders and exports do not contain them, as in stock. The simulator has no equivalent DSP. The audible result is defined by the codec and has not yet been compared against a stock unit.
- The line-in gate and compressor on the delay/reverb sends use the stock settings (gate −32/−38 dB, compressor −40 dB, ratio 35), but the code is OpenTracker's own version of the Audio Dynamics design, so the curves may differ slightly. They run only while the line input has signal and a line-in send is above 0 (spec 04 §5.6).
- Power: hold for 1.5 s to switch off; "off" is a low-power stop. The EEPROM power flag works as in stock: after switching off, reconnecting power keeps the unit off until the power button is pressed (the `ot_restore.txt` check still runs first). The "Shutting down" popup shows from the press with a bar that fills over the 1.5 s hold and closes on an early release, as in stock; its exact look (the stock progress-popup style) is not taken from a stock screenshot.
- Factory test (Config code F8,F1,F7,F2 or pads 47,10,45,8; spec 05 §2.12): the ten steps are stock's, but the pass rules are OpenTracker's own (the spec has none). Screen and pad lights are judged by eye (F8 pass, F7 fail). Inputs passes when all 33 buttons, 48 pads and both encoder directions were seen; holding Shift 1.5 s skips it. RAM runs 10 passes over the free sample memory only. MIDI expects a DIN loop cable. Radio checks that the tuner accepts 90.0 MHz. Battery is always skipped (no fuel gauge). Audio records 1 s each to `line_in_test.wav` and `mic_test.wav` at the card root. USB/SD writes and verifies a 16 KB file; USB is not checked. F1 leaves at any time. The test never writes the EEPROM, and it ends back in Config. `make SELFTEST=1` remains the boot-time hardware test.
- `/.develop` overlay (Config): CPU shows the audio render load (min/max/avg/last, as a percentage of the block budget, over 1 s windows; Play resets it). In place of free heap / max stack it shows the SDRAM left by the memory layout and the longest main-loop pass. The simulator shows "CPU n/a".
- Master Limiter "Reduction" meter: the mean limiter gain of each block, shown in dB on a 0–24 dB bar (the stock scale is unknown).
- Config "Current version" reads "OpenTracker <version>".
- Not included:
  - the Manual viewer (unreachable in stock);
  - USB mass storage, as in stock 1.9.2: the USB Storage Mode page is shown on interface event 10 like stock (its title is stock; the rest of the "Do not unplug…" warning sentence and the Exit button on F8 are OpenTracker's own), but the card is not handed to the host because the stock hardware hook does nothing and has no mass-storage class. No stock trigger for the event is known (spec 01 §6.1). The simulator shows it with the `usbstorage on` script command;
  - battery / fuel-gauge readout.
- The USB ID 1209:0001 is the pid.codes test ID.
- Config "Display font": Original uses Roboto Mono and New uses Roboto, both open fonts rendered to the stock font metrics. If the font upload fails at boot, the display controller's built-in fonts are used instead.
- MIDI CC fixes:
  - Master level CCs (71–82) mark the project as changed, so autosave keeps them.
  - Instrument CCs set the exact target value; the encoder's step limits (LFO speed ±5, Tune ±10 → octave) do not apply.
  - Instrument CCs (and CC 16–19) to an empty sample slot are ignored, so no instrument file is created (the spec does not say).
- MOD / IT import and `.it` export fix the stock defects (spec 11):
  - MOD: every 4-channel signature (M.K., M!K!, FLT4, 4CHN), 2–8-channel files and the 15-sample layout are read; more than 8 channels is refused with a message before the project is touched.
  - MOD samples are converted to 16 bit at full length and notes use the matching offset (stock stores the 8-bit bytes as half-length 16-bit frames).
  - MOD finetuned and extended-octave periods map to the nearest note, and the sample finetune becomes the instrument finetune (stock drops them).
  - MOD song = the MOD song length (stock copies all 128 orders); a one-pattern module no longer reads an extra pattern before the samples.
  - MOD Fxx speed and tempo both convert to BPM = 6·tempo/speed, written as a Tempo FX of BPM/2 with no doubling; the project tempo is the song's start tempo, and a pattern entered at another tempo restates it on row 0.
  - MOD 9xx / IT Oxx sample offset is converted relative to the sample length (stock: xx/256 of the sample).
  - IT: the IMPM magic is checked, and sample-mode files (no instruments) import their samples as instruments.
  - IT 8-bit samples use the 44100 Hz reference like 16-bit (stock 22050 Hz: one octave high); unsigned, delta, stereo and IT 2.14/2.15 compressed samples are decoded.
  - IT ping-pong without the loop flag is one-shot; the sustain loop is used when there is no normal loop.
  - IT pattern note/instrument memory is per channel, a note without an instrument uses the channel's instrument, instrument 48 is usable, and note off/cut/fade become OFF/CUT/FADE (stock: note 127).
  - IT "+++" orders are skipped, the song ends at "---", and an empty pattern in the song becomes 64 empty rows; the project tempo is 6·IT/IS.
  - IT effects are imported into FX slot 0 (stock drops them all): Axx/Txx → Tempo, Cxx → Break, Gxx → Glide, Oxx → Position, Xxx → Panning.
  - Import and export run as background jobs with the progress popup; a file that cannot be imported is reported and the current project is kept.
  - `.it` export creates `Mod/` when it is missing (stock fails).
  - `.it` export reads each instrument's own sample data (stock's running cursor shifted every sample after a stereo one); stereo instruments export as stereo samples, and empty slots have no "sample present" flag.
  - `.it` export filter envelope: it checks envelope 2's own LFO flag (stock reads envelope 0's), uses the ±32 range for sustain, and keeps the filter flag in LFO mode; pan/filter LFOs use their own rate table.
  - `.it` export writes the song up to its end and patterns 1–200 (stock: 200 slots and patterns 1–199), sets the stereo flag with full separation, and maps volume 50 (0 dB) to 64 (stock: half volume).
  - `.it` export keeps both volume and panning FX (panning as Xxx), writes FX on steps without a note, exports Tempo/Break/Glide/Position as Txx/C00/Gxx/Oxx, restates the project tempo on each pattern when Tempo FX are used, skips MIDI-instrument steps and writes fade as note fade.
  - `.it` export above 255 BPM uses speed 3 (stock wraps the tempo byte), and patterns are read from memory instead of reloading each file into the edit buffer.
- Performance fixes:
  - Loading a per-track pattern no longer overwrites the next song or queued pattern that is preloading.
  - In play modes C1–C20, a missed roll on selector 2 now clears the previous roll (`kFixStockRollCarry`).
  - A per-track pattern queued to switch exactly at a song advance is kept, not lost.
  - "Back" play mode wraps to the active pattern length, including the Pattern length FX.
  - A MIDI CC (51–62) on a Pattern length slot applies the length; stock only shows the value.
  - The encoder never stores into the MIDI-CC live row; stock writes past the slot there.
  - Shift+Play while playing queues every track back to the current pattern at its own loop end (the stock sync-point action is not specified).
  - Track labels: a highlighted label means the track's FX are on and dim text means the track is muted, as the 1.9.2 manual shows; the spec only says the colour shows the mute state (spec 06 §3.3).
  - When you leave for another screen while playing, the FX are reverted at the next pattern end (or Stop), and the pattern simply plays on. Stock re-queues the current pattern first (spec 16 §A.15); the result sounds the same.
- Config is kept in the stock EEPROM area (same 0xF4-byte image); an `OpenTracker/config.bin` from an older build is imported once, and is used instead only when the EEPROM is not ready.
- Recorder source, gains, monitor and radio frequency are saved per project (stock bytes 0x134–0x13F); a project that never stored them gets gain 50 and monitor on.
- Recorder pads play the recording chromatically one at a time (the last pad pressed sounds) through the file-preview player: the selection is resampled into the free sample memory behind the recording, so a transposition longer than that memory is cut short, and with none left it plays at its own pitch (appendix C §2.11).
- Radio tuner (spec 22 §2): the reset, power-up, region and tune register sequences are stock's, with three differences. A tune that the tuner never finishes is abandoned after 1 s (a seek after 15 s) and logged as `radio: tune FAILED`; stock waits forever. The tuner is enabled only while Radio is the selected source; stock enables it on every Recorder entry. RDS and its interrupt pin (PTE28) are not set up, because OpenTracker shows no RDS text and, like stock, polls the STC bit to finish a tune.
- Song paste (Shift+Copy) with the grid focused puts the clip's top-left at the selected slot and track, as the 1.9.2 manual describes; the spec only says "at the cursor" (spec 06 §2.3).
- Song F1 "Loop Pattern"/"Continue Song" still pending when you leave the Song screen is applied at once instead of at the next step (spec 06 §2.3).
- Track Mixer meters read the engine's block peak (scaled to sine RMS) every 10 ms for the 10-reading mean; stock reads an RMS analyser (spec 06 §4.4).
- A non-looping selection preview stops after exactly its selected rows; stock counts `sel.end` master steps, so a selection not starting at step 0 replays rows (spec 03 §2.1).
- Export / Render selection / Samples Cancel (F8) deletes the partly written file and its export folder (or the stems folder); what stock leaves behind is not specified. While rendering, other key presses are ignored (spec 05 appendix E1.4).
- Render selection "render & load" (keyboard F7) stops with "Not enough instruments" before rendering when no sample slot is free (stock checks after rendering); after loading, the Sample Loader opens with the new instrument selected, as in stock (spec 16 §B.8).
- The File browser cover image is shown only for a project selected in the SD Card list, centred right of that list (over the Recent list); the spec gives no position (spec 05 appendix E1.6).
- Auto slice: the onset-refinement window is clamped to the sample end (stock reads up to ~6144 frames past it), the slice count is cleared when detection starts, and detection advances only while Sample Playback is shown (leaving the screen finishes it at once) (spec 13 §F).
- Horizontal pattern arrangement (spec 06 §1.2 gives only the window rule): tracks are drawn as bands (4, or 8 with a column-view mask) and the 15 visible steps as columns; each band stacks the shown columns as lines.
- The FX description texts (FX list, F7/F8 "Description") are OpenTracker's own wording.
- Pattern top-bar short FX names are stock (STRINGS_DIFF item 9); the strings diff lists one "Slide up/down" entry, so the two labels "Slide up" and "Slide down" are an assumption.
- Value labels use the stock wording (STRINGS_DIFF §2): filter "Disabled", "Wavetable Position" / "Granular Position", LFO speeds "128 steps" … "1/64 step" (the plural above one step, including "3/2 steps", is assumed for the entries between the two listed ends), EQ "3000 Hz" / "9900 Hz", "LimiterAtt" / "LimiterRls" / "LimiterThr" / "Gain Reduce", saturation "Hard Clip" / "Soft Clip" / "Saturate" / "Drive+", recorder "Mic LG" / "Mic HG", editor "WT Smoother" / "Window Num" / "Window Len" / "Micro-step", Mixer rename "Edit" and Loader F8 "Import" / "Normal".
- MIDI notes in (spec 17 §1.3): on Sample Playback, Sample Editor and Sample Recorder the note goes to that screen's own preview instead of the track voices. Where spec 17 §1.12 leaves the owner semantics open (SPEC GAP): Sample Playback plays the current instrument at the note on its single b0 preview voice (the last note sounds, Beat Slice selects no slice); the Sample Editor starts its F7 preview of the processed selection at its own pitch; the Recorder plays the recording chromatically like its pads (note 60 = own pitch, the last note sounds). Only the sounding note's note-off stops the preview. Live MIDI-instrument notes follow Config "MIDI out" like the sequencer, and MIDI step entry depends only on the sequencer not playing (an armed, stopped sequencer still takes it).
- Note popup F1..F8 (stock 1.9.2 strings): the 8 labels name a window of notes, and a press plays the note and picks it. With Shift held, F1/F2/F7/F8 are Oct-/Note-/Note+/Oct+ and move the window by -12/-1/+1/+12, the direction the labels name; the code's sign convention is not verified. The window starts at the popup's pre-selected note; where stock starts it is not known.
- Fill window: fields, names and buttons are stock (spec 16 §B.3); the window layout (lists above F1 Where?, F3 Scale / Fx Type and F4 Fill Type) is OpenTracker's own.
- Hold Master from another screen: once held (hold time, Jog or another key) the Master main page opens with Volume, so Hold Master + Jog sets the master volume (the stock handling of this case is not specified). Hold Song from another screen shows Song with the tempo focused and returns on release, as in stock (spec 16 §A.7).
- Pattern grid: FX cells are the FX symbol + a 3-character value, as in stock (spec 16 §A.8); the 3-character Arp / LFO / Chord value forms are OpenTracker's own ("3/16" is ".19"; roll cells "R  4", "Rv 2" … "RR16"); Pattern + the same column key twice clears the column-view mask (spec 06 §1.9).
- Games (File > Games, spec 14):
  - The NES emulator is OpenTracker's own scanline-based core (`src/core/games/`); stock uses the GPL Nofrendo core. It supports mappers 0, 1, 2, 3, 4 (MMC3) and 7; stock supports 34. Raster effects are accurate to the line, not the dot.
  - A ROM that cannot be loaded (unreadable, too large, bad header, unsupported mapper) shows an error and stays in the browser. Stock shows nothing and enters a black game screen with the music muted.
  - The ROM and emulator memory live in the free part of the sample memory. If the project's samples leave too little room, the whole sample memory is lent to the game: the project is autosaved first, the autosave is held while playing, and the samples are reloaded (background Workspace load) when the game ends. In that case a screen key ends the game instead of suspending it.
  - A suspended game is resumed only if its memory is untouched; after a sample import or project load it is ended with a message.
  - The frame is 256 x 240 colours from an NTSC signal model, RGB332, at 60.1 Hz (stock: 224 core lines, 60.0 Hz). Only rows that changed are sent to the display, at RAM_G 384,000 (stock 840,000, which overlaps the cover image).
  - ROMs are loaded by their listed file name; stock appends ".nes" to the shortened display name.
