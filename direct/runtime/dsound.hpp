#pragma once
// Native implementation of the original plugin/dsoundex.hpi (DirectSound).
//
// The game reaches this plugin through ds_init, ds_load, ds_memload, ds_play,
// ds_loop, ds_stop, ds_getp, ds_stat, ds_vol, ds_dup and ds_bye. This header
// reproduces the plugin's observable contract: 1024 buffer slots, WAVE parsing,
// ACM conversion of compressed WAVE data to PCM (MS ADPCM is what the game
// ships), DirectSound buffer sizes and return values, the volume/pan
// conversions, and DirectSound buffer state (cursor, status, looping,
// duplication). A software mixer renders the playing buffers for the platform
// output. See docs/dsound-abi.md for the recovered contract and evidence.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cp2 {
namespace dsound {

constexpr int kSlots = 1024;  // 0x100051d8 .. 0x100071d8, 8 bytes per slot

// DirectSound constants used by the plugin.
constexpr std::uint32_t kStatusPlaying = 0x1;
constexpr std::uint32_t kStatusLooping = 0x4;
constexpr std::uint32_t kPlayLooping = 0x1;
constexpr std::int32_t kVolumeMin = -10000;
constexpr std::uint32_t kBufferFlags = 0x180e8;  // CTRLFREQUENCY|CTRLPAN|CTRLVOLUME|STATIC|GETCURRENTPOSITION2|GLOBALFOCUS

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint32_t bytes_per_second = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits = 0;
};

// Shared PCM storage: DuplicateSoundBuffer shares memory between buffers.
struct Storage {
    Format format;
    std::vector<std::uint8_t> bytes;
};

struct Buffer {
    std::shared_ptr<Storage> storage;
    std::uint32_t position = 0;  // play cursor in bytes
    double fraction = 0.0;       // sub-frame resampling phase
    bool playing = false;
    bool looping = false;
    std::int32_t volume = 0;     // hundredths of a decibel, 0..-10000
    std::int32_t pan = 0;        // -10000..10000
    std::uint32_t frequency = 0; // playback rate in Hz
};

// ---------------------------------------------------------------------------
// MS ADPCM decoding as performed by the Windows ACM codec for WAVE_FORMAT_ADPCM.

struct AdpcmInfo {
    std::uint16_t samples_per_block = 0;
    std::vector<std::int16_t> coefficients;  // pairs
};

inline bool adpcm_info(const std::uint8_t* fmt, std::uint32_t size, const Format& format, AdpcmInfo& info) {
    if (size < 22 || format.channels < 1 || format.channels > 2 || format.block_align < 7u * format.channels) return false;
    std::uint16_t extra, count;
    std::memcpy(&extra, fmt + 16, 2);
    std::memcpy(&info.samples_per_block, fmt + 18, 2);
    std::memcpy(&count, fmt + 20, 2);
    if (extra < 4 + 4u * count || size < 22u + 4u * count || count == 0) return false;
    info.coefficients.resize(2u * count);
    std::memcpy(info.coefficients.data(), fmt + 22, 4u * count);
    const std::uint32_t expected = 2u + (format.block_align - 7u * format.channels) * 8u / (4u * format.channels);
    return info.samples_per_block == expected;
}

inline std::int16_t clamp16(std::int32_t value) {
    return static_cast<std::int16_t>(value < -32768 ? -32768 : value > 32767 ? 32767 : value);
}

// Decodes whole blocks plus a final partial block into 16-bit PCM.
inline void adpcm_decode(const std::uint8_t* data, std::uint32_t size, const Format& format, const AdpcmInfo& info,
                         std::uint8_t* out, std::uint32_t out_size) {
    static const std::int32_t adaptation[16] = {230, 230, 230, 230, 307, 409, 512, 614,
                                                768, 614, 512, 409, 307, 230, 230, 230};
    const unsigned channels = format.channels;
    std::uint32_t written = 0;
    auto emit = [&](std::int16_t sample) {
        if (written + 2 <= out_size) std::memcpy(out + written, &sample, 2);
        written += 2;
    };
    for (std::uint32_t offset = 0; offset + 7u * channels <= size; offset += format.block_align) {
        const std::uint32_t block = size - offset < format.block_align ? size - offset : format.block_align;
        const std::uint8_t* p = data + offset;
        std::int32_t coef1[2], coef2[2], delta[2], sample1[2], sample2[2];
        const std::size_t coefficient_count = info.coefficients.size() / 2;
        for (unsigned c = 0; c < channels; ++c) {
            std::size_t predictor = p[c];
            if (predictor >= coefficient_count) predictor = 0;
            coef1[c] = info.coefficients[predictor * 2];
            coef2[c] = info.coefficients[predictor * 2 + 1];
        }
        p += channels;
        auto read16 = [&]() {
            std::int16_t v;
            std::memcpy(&v, p, 2);
            p += 2;
            return static_cast<std::int32_t>(v);
        };
        for (unsigned c = 0; c < channels; ++c) delta[c] = read16();
        for (unsigned c = 0; c < channels; ++c) sample1[c] = read16();
        for (unsigned c = 0; c < channels; ++c) sample2[c] = read16();
        for (unsigned c = 0; c < channels; ++c) emit(static_cast<std::int16_t>(sample2[c]));
        for (unsigned c = 0; c < channels; ++c) emit(static_cast<std::int16_t>(sample1[c]));
        const std::uint32_t nibble_bytes = block - 7u * channels;
        std::uint32_t frames = 2;
        unsigned channel = 0;
        for (std::uint32_t i = 0; i < nibble_bytes * 2 && frames < info.samples_per_block; ++i) {
            const std::uint8_t byte = data[offset + 7u * channels + i / 2];
            const std::int32_t nibble = (i & 1) ? (byte & 0x0f) : (byte >> 4);
            const std::int32_t signed_nibble = nibble >= 8 ? nibble - 16 : nibble;
            const std::int32_t predicted = (sample1[channel] * coef1[channel] + sample2[channel] * coef2[channel]) >> 8;
            const std::int16_t sample = clamp16(predicted + signed_nibble * delta[channel]);
            emit(sample);
            delta[channel] = (adaptation[nibble] * delta[channel]) >> 8;
            if (delta[channel] < 16) delta[channel] = 16;
            sample2[channel] = sample1[channel];
            sample1[channel] = sample;
            if (++channel == channels) {
                channel = 0;
                ++frames;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// WAVE parsing (0x10002960) and conversion (0x10002ad0 / 0x10003100).

struct Wave {
    Format format;
    std::vector<std::uint8_t> pcm;
};

// Parses a RIFF/WAVE image. available bounds the readable bytes (the original
// trusts the RIFF sizes); bytes past it read as zero.
inline bool parse_wave(const std::uint8_t* data, std::size_t available, Wave& wave, std::string& error) {
    auto byte_at = [&](std::size_t i) -> std::uint8_t { return i < available ? data[i] : 0; };
    auto u32 = [&](std::size_t i) {
        return static_cast<std::uint32_t>(byte_at(i)) | (static_cast<std::uint32_t>(byte_at(i + 1)) << 8) |
               (static_cast<std::uint32_t>(byte_at(i + 2)) << 16) | (static_cast<std::uint32_t>(byte_at(i + 3)) << 24);
    };
    if (u32(0) != 0x46464952u || u32(8) != 0x45564157u) { error = "not a RIFF/WAVE image"; return false; }
    const std::uint64_t end = 4ull + u32(4);  // the original's end pointer is data + 4 + riff size
    std::uint64_t p = 12, fmt = 0, payload = 0;
    std::uint32_t fmt_size = 0, payload_size = 0;
    bool found = false;
    while (p < end && !found) {
        const std::uint32_t id = u32(static_cast<std::size_t>(p));
        const std::uint32_t size = u32(static_cast<std::size_t>(p + 4));
        p += 8;
        if (id == 0x20746d66u) {  // "fmt "
            if (!fmt) {
                if (size < 14) { error = "fmt chunk shorter than 14 bytes"; return false; }
                fmt = p;
                fmt_size = size;
                if (payload && payload_size) found = true;
            }
        } else if (id == 0x61746164u) {  // "data"
            if (!payload || !payload_size) {
                payload = p;
                payload_size = size;
                if (fmt) found = true;
            }
        }
        p += (static_cast<std::uint64_t>(size) + 1) & ~1ull;
    }
    if (!found) { error = "WAVE image has no fmt/data pair"; return false; }
    std::vector<std::uint8_t> header(fmt_size);
    for (std::uint32_t i = 0; i < fmt_size; ++i) header[i] = byte_at(static_cast<std::size_t>(fmt + i));
    Format& f = wave.format;
    std::memcpy(&f.tag, &header[0], 2);
    std::memcpy(&f.channels, &header[2], 2);
    std::memcpy(&f.rate, &header[4], 4);
    std::memcpy(&f.bytes_per_second, &header[8], 4);
    std::memcpy(&f.block_align, &header[12], 2);
    f.bits = fmt_size >= 16 ? static_cast<std::uint16_t>(header[14] | (header[15] << 8)) : 0;
    std::vector<std::uint8_t> source(payload_size);
    for (std::uint32_t i = 0; i < payload_size; ++i) source[i] = byte_at(static_cast<std::size_t>(payload + i));
    if (f.tag == 1) {
        wave.pcm.swap(source);
        return true;
    }
    if (f.tag == 2) {
        // acmFormatSuggest(PCM) -> same rate/channels, 16 bits; buffer size from
        // acmStreamSize(source length).
        AdpcmInfo info;
        if (!adpcm_info(header.data(), fmt_size, f, info)) { error = "unsupported MS ADPCM format"; return false; }
        const std::uint32_t whole = payload_size / f.block_align;
        const std::uint32_t frames = whole * info.samples_per_block;
        Format pcm;
        pcm.tag = 1;
        pcm.channels = f.channels;
        pcm.rate = f.rate;
        pcm.bits = 16;
        pcm.block_align = static_cast<std::uint16_t>(2u * f.channels);
        pcm.bytes_per_second = pcm.rate * pcm.block_align;
        wave.pcm.assign(static_cast<std::size_t>(frames) * pcm.block_align, 0);
        adpcm_decode(source.data(), whole * f.block_align, f, info, wave.pcm.data(), static_cast<std::uint32_t>(wave.pcm.size()));
        wave.format = pcm;
        return true;
    }
    error = "WAVE format tag " + std::to_string(f.tag) + " needs an ACM codec that is not implemented";
    return false;
}

// CreateSoundBuffer validation for the formats DirectSound accepts.
inline bool valid_buffer(const Format& f, std::size_t bytes) {
    return f.tag == 1 && (f.channels == 1 || f.channels == 2) && (f.bits == 8 || f.bits == 16) &&
           f.block_align == f.channels * f.bits / 8 && f.rate >= 100 && f.rate <= 200000 && bytes >= 4 &&
           bytes <= 0x0fffffff;
}

// ds_vol (0x100018c0): 1..256 -> trunc(log10(v / 256) * 2000); <1 -> -10000.
inline std::int32_t volume_from_level(std::int32_t level) {
    if (level < 1) return kVolumeMin;
    if (level > 256) level = 256;
    return static_cast<std::int32_t>(std::log10(level * 0.00390625) * 2000.0);
}

// ds_pan (0x10001830): |p| clamped to 127 -> trunc(pow(10000, |p| / 127)), signed.
inline std::int32_t pan_from_level(std::int32_t level) {
    if (level == 0) return 0;
    const bool left = level < 0;
    std::int32_t magnitude = left ? -level : level;
    if (magnitude > 127) magnitude = 127;
    const std::int32_t value = static_cast<std::int32_t>(std::pow(10000.0, magnitude * 0.007874015748031496));
    return left ? -value : value;
}

// ---------------------------------------------------------------------------
// Plugin state.

class Device {
public:
    // ds_init (0x10001a10). The plugin requests a primary buffer with
    // CTRLFREQUENCY, which DirectSound rejects, so ds_init returns -1 after the
    // device and cooperative level are established.
    int init(int /*frequency*/, int /*channels*/) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) release_all();
        initialized_ = true;
        return -1;
    }

    bool initialized() const { return initialized_; }

    // ds_load (0x10001430): returns -(PCM bytes) on success, 0 on failure.
    int load(int slot, const std::uint8_t* data, std::size_t size, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid(slot)) { error = "slot outside the plugin's 1024 buffers"; return 0; }
        release(slot);
        if (!initialized_) { error = "ds_init has not run"; return 0; }
        if (!data) { error = "file not found"; return 0; }
        Wave wave;
        if (!parse_wave(data, size, wave, error)) return 0;
        const int bytes = static_cast<int>(wave.pcm.size());
        if (!create(slot, wave)) { error = "DirectSound rejected the buffer format"; return 0; }
        return -bytes;
    }

    // ds_memload (0x100014d0): returns 0 on success, -1 on failure.
    int memload(int slot, const std::uint8_t* data, std::size_t available, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!valid(slot)) { error = "slot outside the plugin's 1024 buffers"; return -1; }
        release(slot);
        Wave wave;
        if (!parse_wave(data, available, wave, error)) return -1;
        if (!create(slot, wave)) { error = "DirectSound rejected the buffer format"; return -1; }
        return 0;
    }

    // ds_play (0x100016c0): stop, seek, play. ds_loop passes the looping flag.
    int play(int slot, int position, int flags) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return -1;
        b->playing = false;
        set_position(*b, position);
        b->playing = true;
        b->looping = (flags & kPlayLooping) != 0;
        return 0;
    }

    int stop(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return 0;
        b->playing = false;
        b->looping = false;
        return 0;
    }

    // ds_stat (0x10001a80): -(DirectSound status flags).
    int status(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return 0;
        std::uint32_t flags = 0;
        if (b->playing) flags |= kStatusPlaying | (b->looping ? kStatusLooping : 0);
        return -static_cast<int>(flags);
    }

    // ds_getp (0x100017c0): -(play cursor in bytes).
    int position(int slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return 0;
        return -static_cast<int>(b->position);
    }

    int set_position_command(int slot, int position) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return 0;
        return set_position(*b, position) ? 0 : -1;
    }

    int volume(int slot, int level) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return 0;
        b->volume = volume_from_level(level);
        return 0;
    }

    int pan(int slot, int level) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* b = buffer(slot);
        if (!b) return 0;
        b->pan = pan_from_level(level);
        return 0;
    }

    // ds_dup (0x10001670): duplicate src into dst; shares PCM memory and copies
    // volume, pan and frequency; the duplicate starts stopped at position 0.
    int duplicate(int destination, int source) {
        std::lock_guard<std::mutex> lock(mutex_);
        Buffer* from = buffer(source);
        if (!from || !valid(destination)) return -1;
        if (destination == source) return 0;  // the original releases the source first
        release(destination);
        Buffer copy;
        copy.storage = from->storage;
        copy.volume = from->volume;
        copy.pan = from->pan;
        copy.frequency = from->frequency;
        buffers_[destination].reset(new Buffer(copy));
        return 0;
    }

    // ds_bye (0x10001960): stops every buffer; buffers stay loaded.
    int stop_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& b : buffers_)
            if (b) b->playing = b->looping = false;
        return 0;
    }

    // Read-only access for tests.
    const Buffer* inspect(int slot) const { return valid(slot) ? buffers_[slot].get() : nullptr; }

    // Mixes every playing buffer into interleaved stereo float frames at
    // output_rate, advancing cursors. DirectSound volumes and pans attenuate
    // in hundredths of a decibel; non-looping buffers stop and rewind at end.
    void render(float* out, std::size_t frames, double output_rate) {
        std::memset(out, 0, frames * 2 * sizeof(float));
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& owned : buffers_) {
            Buffer* b = owned.get();
            if (!b || !b->playing) continue;
            const Format& f = b->storage->format;
            const std::vector<std::uint8_t>& pcm = b->storage->bytes;
            const std::uint32_t total = static_cast<std::uint32_t>(pcm.size() / f.block_align);
            if (!total) { b->playing = false; continue; }
            const double gain = std::pow(10.0, b->volume / 2000.0);
            const double left = gain * (b->pan > 0 ? std::pow(10.0, -b->pan / 2000.0) : 1.0);
            const double right = gain * (b->pan < 0 ? std::pow(10.0, b->pan / 2000.0) : 1.0);
            const double step = static_cast<double>(b->frequency) / output_rate;
            std::uint32_t frame = b->position / f.block_align;
            double phase = b->fraction;
            for (std::size_t i = 0; i < frames; ++i) {
                const std::uint32_t next = frame + 1 < total ? frame + 1 : (b->looping ? 0 : frame);
                float l0, r0, l1, r1;
                sample(pcm, f, frame, l0, r0);
                sample(pcm, f, next, l1, r1);
                out[i * 2] += static_cast<float>((l0 + (l1 - l0) * phase) * left);
                out[i * 2 + 1] += static_cast<float>((r0 + (r1 - r0) * phase) * right);
                phase += step;
                while (phase >= 1.0) {
                    phase -= 1.0;
                    if (++frame >= total) {
                        if (b->looping) {
                            frame = 0;
                        } else {
                            b->playing = false;
                            frame = 0;
                            phase = 0.0;
                            break;
                        }
                    }
                }
                if (!b->playing) break;
            }
            b->position = frame * f.block_align;
            b->fraction = phase;
        }
    }

private:
    mutable std::mutex mutex_;
    bool initialized_ = false;
    std::unique_ptr<Buffer> buffers_[kSlots];

    static bool valid(int slot) { return slot >= 0 && slot < kSlots; }

    Buffer* buffer(int slot) { return valid(slot) ? buffers_[slot].get() : nullptr; }

    void release(int slot) { buffers_[slot].reset(); }

    void release_all() {
        for (auto& b : buffers_) b.reset();
    }

    bool create(int slot, Wave& wave) {
        if (!valid_buffer(wave.format, wave.pcm.size())) return false;
        std::unique_ptr<Buffer> b(new Buffer());
        b->storage = std::make_shared<Storage>();
        b->storage->format = wave.format;
        b->storage->bytes.swap(wave.pcm);
        b->frequency = wave.format.rate;
        buffers_[slot] = std::move(b);
        return true;
    }

    // SetCurrentPosition: rejects offsets past the buffer, aligns to a frame.
    static bool set_position(Buffer& b, int position) {
        const std::size_t size = b.storage->bytes.size();
        if (position < 0 || static_cast<std::size_t>(position) >= size) return false;
        b.position = static_cast<std::uint32_t>(position) / b.storage->format.block_align * b.storage->format.block_align;
        b.fraction = 0.0;
        return true;
    }

    static void sample(const std::vector<std::uint8_t>& pcm, const Format& f, std::uint32_t frame, float& l, float& r) {
        const std::size_t at = static_cast<std::size_t>(frame) * f.block_align;
        if (f.bits == 16) {
            std::int16_t a, c;
            std::memcpy(&a, &pcm[at], 2);
            c = a;
            if (f.channels == 2) std::memcpy(&c, &pcm[at + 2], 2);
            l = a / 32768.0f;
            r = c / 32768.0f;
        } else {
            l = (pcm[at] - 128) / 128.0f;
            r = f.channels == 2 ? (pcm[at + 1] - 128) / 128.0f : l;
        }
    }
};

}  // namespace dsound
}  // namespace cp2
