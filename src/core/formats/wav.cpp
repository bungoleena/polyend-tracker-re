// WAV import/export, spec 02 §8.1 and §8.3.
#include "formats/wav.h"

#include <cmath>
#include <cstring>

#include "formats/bytes.h"
#include "model/constants.h"

namespace ot {

// ---------------------------------------------------------------- readers

int32_t MemReader::read(void* dst, uint32_t n) {
    size_t avail = pos_ < len_ ? len_ - pos_ : 0;
    size_t c = n < avail ? n : avail;
    if (c) std::memcpy(dst, data_ + pos_, c);
    pos_ += c;
    return int32_t(c);
}

bool MemReader::skip(uint32_t n) {
    if (pos_ + n > len_) {
        pos_ = len_;
        return false;
    }
    pos_ += n;
    return true;
}

int32_t StorageReader::read(void* dst, uint32_t n) {
    int32_t r = fs_.read(h_, dst, n);
    if (r > 0) pos_ += uint32_t(r);
    return r;
}

bool StorageReader::skip(uint32_t n) {
    int32_t sz = fs_.size(h_);
    if (sz < 0 || uint64_t(pos_) + n > uint64_t(sz)) return false;
    if (!fs_.seek(h_, pos_ + n)) return false;
    pos_ += n;
    return true;
}

// ----------------------------------------------------------------- parser

bool wavParseHeader(ByteReader& r, WavInfo& info) {
    info = WavInfo{};
    uint8_t b[16];
    // 1. "RIFF" + size, then the form type.
    if (r.read(b, 8) != 8 || std::memcmp(b, "RIFF", 4) != 0) return false;
    if (r.read(b, 4) != 4) return false;
    const bool formWave = std::memcmp(b, "WAVE", 4) == 0;

    // 2. Up to 10 chunks looking for "fmt " (must precede "data").
    bool haveFmt = false;
    for (uint32_t i = 0; i < wav::kMaxChunkScan && !haveFmt; ++i) {
        if (r.read(b, 8) != 8) return false;
        uint32_t size = rdU32(b + 4);
        uint32_t padded = size + (size & 1);
        if (std::memcmp(b, "fmt ", 4) == 0) {
            if (size < 16) return false;
            if (r.read(b, 16) != 16) return false;
            info.format = rdU16(b + 0);
            info.channels = rdU16(b + 2);
            info.sampleRate = rdU32(b + 4);
            info.byteRate = rdU32(b + 8);
            info.blockAlign = rdU16(b + 12);
            info.bitsPerSample = rdU16(b + 14);
            if (padded > 16 && !r.skip(padded - 16)) return false;
            haveFmt = true;
        } else if (!r.skip(padded)) {
            return false;
        }
    }
    if (!haveFmt) {
        info = WavInfo{};
        return false;
    }
    // 3. Up to 10 more chunks looking for "data".
    for (uint32_t i = 0; i < wav::kMaxChunkScan; ++i) {
        if (r.read(b, 8) != 8) break;
        uint32_t size = rdU32(b + 4);
        if (std::memcmp(b, "data", 4) == 0) {
            info.dataSize = size;
            info.dataOffset = r.position();
            info.formWave = formWave;
            info.valid = true;
            return true;
        }
        if (!r.skip(size + (size & 1))) break;
    }
    // 4. Not found: header invalid (all zero).
    info = WavInfo{};
    return false;
}

bool wavParseHeader(const uint8_t* file, size_t len, WavInfo& info) {
    MemReader r(file, len);
    return wavParseHeader(r, info);
}

bool wavIsSupported(const WavInfo& i) {
    if (!i.valid || !i.formWave) return false;
    if (i.channels != 1 && i.channels != 2) return false;
    if (i.sampleRate == 0) return false;  // guard against division by zero
    if (i.format == wav::kFormatPcm)
        return i.bitsPerSample == 8 || i.bitsPerSample == 16 || i.bitsPerSample == 24 || i.bitsPerSample == 32;
    if (i.format == wav::kFormatFloat) return i.bitsPerSample == 32;
    return false;
}

bool wavIsFastPath(const WavInfo& i) {
    return wavIsSupported(i) && i.format == wav::kFormatPcm && i.bitsPerSample == 16 && i.channels == 1 &&
           i.sampleRate == kSampleRate;
}

uint32_t wavBytesPerFrame(const WavInfo& i) { return uint32_t(i.channels) * (i.bitsPerSample / 8u); }

uint32_t wavSourceFrames(const WavInfo& i) {
    uint32_t bpf = wavBytesPerFrame(i);
    return bpf ? i.dataSize / bpf : 0;
}

uint32_t wavResampledFrames(uint32_t srcFrames, uint32_t srcRate) {
    return srcRate ? uint32_t(uint64_t(srcFrames) * kSampleRate / srcRate) : 0;
}

int16_t wavDecodeSample(const uint8_t* p, uint16_t format, uint16_t bits) {
    if (format == wav::kFormatFloat) {
        // (int16)(int)(x * 32767) (spec 07 Q8): truncation; |x| > 1 wraps
        // through the int16 cast instead of saturating. NaN and values
        // outside the int32 range (undefined in C) give 0 (OpenTracker guard).
        const float x = rdF32(p) * 32767.0f;
        if (!(x > -2147483648.0f && x < 2147483648.0f)) return 0;
        return int16_t(uint16_t(uint32_t(int32_t(x))));
    }
    switch (bits) {
        case 8: return int16_t((int(p[0]) - 128) * 256);
        case 16: return rdI16(p);
        case 24: return rdI16(p + 1);  // top 16 bits
        case 32: return rdI16(p + 2);  // top 16 bits
        default: return 0;
    }
}

// --------------------------------------------------------------- resampler

void CubicResampler::begin(uint32_t srcRate, uint32_t srcFrames, int16_t* dst) {
    *this = CubicResampler{};
    srcRate_ = srcRate ? srcRate : kSampleRate;
    srcFrames_ = srcFrames;
    outTotal_ = wavResampledFrames(srcFrames, srcRate_);
    dst_ = dst;
}

int32_t CubicResampler::src(int64_t j) const {
    if (j < 0 || uint64_t(j) >= srcFrames_ || uint64_t(j) >= received_) return 0;
    return hist_[j & 3];
}

// Source position of output frame i in single precision (spec 07 Q8):
// t = i * ratio, n = floor(t), mu = t - n.
static inline int64_t srcIndex(uint64_t i, uint32_t srcRate, float* mu) {
    const float ratio = float(srcRate) / float(kSampleRate);
    const float t = float(i) * ratio;
    const float n = std::floor(t);
    if (mu) *mu = t - n;
    return int64_t(n);
}

bool CubicResampler::ready(uint64_t i) const {
    if (ended_) return true;
    const int64_t ni = srcIndex(i, srcRate_, nullptr);
    const uint64_t n = ni < 0 ? 0 : uint64_t(ni);
    uint64_t need = n + 2;
    if (need >= srcFrames_) need = srcFrames_ - 1;
    return need < received_;
}

void CubicResampler::emit() {
    while (outNext_ < outTotal_ && ready(outNext_)) {
        // spec 02 §8.1 / 07 Q8: t = i * srcRate/44100, n = floor(t), mu = t - n.
        float mu = 0.0f;
        const int64_t n = srcIndex(outNext_, srcRate_, &mu);
        const float p0 = float(src(n - 1)), p1 = float(src(n)), p2 = float(src(n + 1)), p3 = float(src(n + 2));
        const float a0 = p3 - p2 - p0 + p1;
        const float a1 = p0 - p1 - a0;
        const float a2 = p2 - p0;
        const float a3 = p1;
        const float y = ((a0 * mu + a1) * mu + a2) * mu + a3;
        // Truncation toward zero, then clamp (spec 07 Q8).
        int32_t v = int32_t(y);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        dst_[outNext_++] = int16_t(v);
    }
}

void CubicResampler::push(int16_t s) {
    if (received_ >= srcFrames_) return;
    hist_[received_ & 3] = s;
    ++received_;
    emit();
}

void CubicResampler::finish() {
    ended_ = true;
    emit();
}

// ---------------------------------------------------------------- importer

WavImportPlan wavPlanImport(const WavInfo& info, const WavImportOptions& opt) {
    WavImportPlan p;
    const uint32_t total = wavSourceFrames(info);
    uint32_t s = uint32_t(uint64_t(total) * opt.startMarker / 65535u);
    uint32_t e = uint32_t(uint64_t(total) * opt.endMarker / 65535u);
    if (e < s) e = s;
    p.srcStart = s;
    p.srcFrames = e - s;
    p.outFrames = wavResampledFrames(p.srcFrames, info.sampleRate);
    p.outChannels = (opt.stereo && info.channels == 2) ? 2 : 1;
    return p;
}

FmtStatus WavImporter::begin(const WavInfo& info, const WavImportOptions& opt, int16_t* out,
                             size_t outCapSamples) {
    *this = WavImporter{};
    if (!wavIsSupported(info)) return FmtStatus::Unsupported;
    info_ = info;
    plan_ = wavPlanImport(info, opt);
    if (size_t(plan_.outFrames) * plan_.outChannels > outCapSamples) return FmtStatus::NoSpace;
    bpf_ = wavBytesPerFrame(info);
    rs_[0].begin(info.sampleRate, plan_.srcFrames, out);
    if (plan_.outChannels == 2) rs_[1].begin(info.sampleRate, plan_.srcFrames, out + plan_.outFrames);
    return FmtStatus::Ok;
}

void WavImporter::frame(const uint8_t* f) {
    if (frameIdx_ >= plan_.srcStart && frameIdx_ < uint64_t(plan_.srcStart) + plan_.srcFrames) {
        const uint16_t bps = info_.bitsPerSample / 8;
        // Channel 0 always; channel 1 only in stereo-import mode. Mono import
        // of a stereo file keeps the left channel only, no averaging (spec 07 Q8).
        rs_[0].push(wavDecodeSample(f, info_.format, info_.bitsPerSample));
        if (plan_.outChannels == 2) rs_[1].push(wavDecodeSample(f + bps, info_.format, info_.bitsPerSample));
    }
    ++frameIdx_;
}

void WavImporter::feed(const uint8_t* bytes, size_t n) {
    if (!bpf_) return;
    if (carryLen_) {
        while (n && carryLen_ < bpf_) {
            carry_[carryLen_++] = *bytes++;
            --n;
        }
        if (carryLen_ < bpf_) return;
        frame(carry_);
        carryLen_ = 0;
    }
    while (n >= bpf_) {
        frame(bytes);
        bytes += bpf_;
        n -= bpf_;
    }
    while (n) {
        carry_[carryLen_++] = *bytes++;
        --n;
    }
}

uint32_t WavImporter::availableFrames() const {
    uint32_t a = rs_[0].produced();
    if (plan_.outChannels == 2 && rs_[1].produced() < a) a = rs_[1].produced();
    return a < plan_.outFrames ? a : plan_.outFrames;
}

uint32_t WavImporter::finish() {
    rs_[0].finish();
    if (plan_.outChannels == 2) rs_[1].finish();
    return plan_.outFrames;
}

FmtStatus wavImportMemory(const uint8_t* file, size_t len, const WavImportOptions& opt, int16_t* out,
                          size_t outCapSamples, WavImportResult& res) {
    res = WavImportResult{};
    WavInfo info;
    if (!wavParseHeader(file, len, info)) return FmtStatus::Unsupported;
    WavImporter imp;
    FmtStatus st = imp.begin(info, opt, out, outCapSamples);
    if (st != FmtStatus::Ok) return st;
    size_t avail = info.dataOffset < len ? len - info.dataOffset : 0;
    if (avail > info.dataSize) avail = info.dataSize;
    imp.feed(file + info.dataOffset, avail);
    res.frames = imp.finish();
    res.stereo = imp.plan().outChannels == 2;
    return FmtStatus::Ok;
}

FmtStatus wavImportFile(IStorage& fs, const char* path, const WavImportOptions& opt, int16_t* out,
                        size_t outCapSamples, WavImportResult& res) {
    res = WavImportResult{};
    IStorage::Handle h = fs.open(path, IStorage::kRead);
    if (h < 0) return FmtStatus::NotFound;
    WavInfo info;
    StorageReader r(fs, h);
    if (!wavParseHeader(r, info)) {
        fs.close(h);
        return FmtStatus::Unsupported;
    }
    WavImporter imp;
    FmtStatus st = imp.begin(info, opt, out, outCapSamples);
    if (st != FmtStatus::Ok) {
        fs.close(h);
        return st;
    }
    // The firmware streams 0x7F80-byte chunks; a smaller stack buffer is used
    // here (chunking is not observable in the result).
    uint8_t buf[2048];
    uint32_t left = info.dataSize;
    while (left) {
        uint32_t want = left < sizeof(buf) ? left : uint32_t(sizeof(buf));
        int32_t got = fs.read(h, buf, want);
        if (got <= 0) break;
        imp.feed(buf, size_t(got));
        left -= uint32_t(got);
    }
    fs.close(h);
    res.frames = imp.finish();
    res.stereo = imp.plan().outChannels == 2;
    return FmtStatus::Ok;
}

// ------------------------------------------------------------------ writer

void wavMakeHeader(uint8_t out[wav::kHeaderSize], uint16_t channels, uint32_t dataBytes) {
    const uint16_t blockAlign = uint16_t(channels * 2);
    std::memcpy(out, "RIFF", 4);
    wrU32(out + 4, dataBytes + 36);
    std::memcpy(out + 8, "WAVE", 4);
    std::memcpy(out + 12, "fmt ", 4);
    wrU32(out + 16, 16);
    wrU16(out + 20, wav::kFormatPcm);
    wrU16(out + 22, channels);
    wrU32(out + 24, kSampleRate);
    wrU32(out + 28, kSampleRate * blockAlign);
    wrU16(out + 32, blockAlign);
    wrU16(out + 34, 16);
    std::memcpy(out + 36, "data", 4);
    wrU32(out + 40, dataBytes);
}

size_t wavEncode(const int16_t* interleaved, uint32_t frames, uint16_t channels, uint8_t* out, size_t cap) {
    const size_t data = size_t(frames) * channels * 2;
    if (cap < wav::kHeaderSize + data) return 0;
    wavMakeHeader(out, channels, uint32_t(data));
    for (size_t i = 0; i < size_t(frames) * channels; ++i)
        wrU16(out + wav::kHeaderSize + 2 * i, uint16_t(interleaved[i]));
    return wav::kHeaderSize + data;
}

bool WavWriter::open(IStorage& fs, const char* path, uint16_t channels) {
    if (h_ >= 0 || (channels != 1 && channels != 2)) return false;
    if (fs.exists(path)) fs.remove(path);
    h_ = fs.open(path, IStorage::kWrite);
    if (h_ < 0) return false;
    fs_ = &fs;
    channels_ = channels;
    frames_ = 0;
    uint8_t hdr[wav::kHeaderSize];
    wavMakeHeader(hdr, channels, 0);
    if (fs.write(h_, hdr, sizeof(hdr)) != int32_t(sizeof(hdr))) {
        fs.close(h_);
        h_ = -1;
        return false;
    }
    return true;
}

bool WavWriter::write(const int16_t* interleaved, uint32_t frames) {
    if (h_ < 0) return false;
    uint8_t buf[256];
    size_t total = size_t(frames) * channels_;
    size_t i = 0;
    while (i < total) {
        size_t n = 0;
        for (; n < sizeof(buf) / 2 && i < total; ++n, ++i) wrU16(buf + 2 * n, uint16_t(interleaved[i]));
        if (fs_->write(h_, buf, uint32_t(2 * n)) != int32_t(2 * n)) return false;
    }
    frames_ += frames;
    return true;
}

bool WavWriter::close() {
    if (h_ < 0) return false;
    uint8_t hdr[wav::kHeaderSize];
    wavMakeHeader(hdr, channels_, frames_ * channels_ * 2u);
    bool ok = fs_->seek(h_, 0) && fs_->write(h_, hdr, sizeof(hdr)) == int32_t(sizeof(hdr));
    fs_->close(h_);
    h_ = -1;
    return ok;
}

}  // namespace ot
