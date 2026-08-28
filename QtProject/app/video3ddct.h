// video3ddct.h
//
// Standalone C++ port of Czkawka's "3D algorithm" — the visual 3D-DCT video
// hashing used by similario_core (the engine Czkawka v12.0 switched to).
//
// Algorithm (faithful to similario_core, MIT — Rafał Mikrut / qarmin):
//   1. Split the video into N temporal windows (skip intros via skip_secs).
//   2. In each window, extract FRAMES_PER_WINDOW (16) grayscale 16x16 frames
//      via ffmpeg, across N temporal windows.
//   3. Stack the 16 frames into a 16x16x16 cube, center pixels to [-128,127],
//      apply a separable 3D-DCT-II (3 passes + transpositions).
//   4. Take the 10x10x10 low-frequency corner, binarize (coeff > 0) -> 1000-bit hash.
//   5. Compare hashes with Hamming distance + duration bucketing + sliding-window
//      subclip detection.
//
// Integrated into video-simili-duplicate-cleaner as a selectable comparison
// mode ("3D-DCT"). ffmpeg must be resolvable at runtime: by default from PATH,
// or via setFfmpegPath() to the ffmpeg binary bundled inside the .app.
//
// Improvement over the literal similario port: window_count can be "auto"
// (proportional to duration) so the longer video gets more windows than a
// shorter clip, which is what makes sliding-window SubClip detection actually
// able to find arbitrary middle segments (a fixed window_count degenerates to
// offset 0 and misses them).

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <optional>

namespace v3d {

constexpr int DCT_SIZE = 16;             // 16x16x16 cube
constexpr int HASH_SIZE = 10;            // 10x10x10 -> 1000 bits
constexpr int FRAMES_PER_WINDOW = 16;
constexpr int HASH_BITS = HASH_SIZE * HASH_SIZE * HASH_SIZE; // 1000
constexpr int HASH_BYTES = (HASH_BITS + 7) / 8;              // 125

struct SignatureConfig {
    double skip_secs = 15.0;     // skip this many seconds at the start (intro/credits)
    int    window_count = 0;     // 0 = auto (proportional to duration); >0 = fixed
    double window_secs = 6.0;    // seconds extracted per window
    bool   cropdetect = true;    // letterbox cropdetect
    bool   auto_window_count = true; // if true, window_count is ignored (duration/3, clamped 3..40)
};

// 1000-bit hash (125 bytes, LSB-first bit ordering, matching similario's Lsb0).
struct TemporalHash {
    uint64_t start_ms = 0;
    uint64_t end_ms = 0;
    std::array<uint8_t, HASH_BYTES> bits{};

    int hamming(const TemporalHash& o) const;
    float normalized_distance(const TemporalHash& o) const;
};

struct VideoSignature {
    std::string path;
    uint64_t duration_ms = 0;
    float aspect_ratio = 16.0f / 9.0f;
    std::vector<TemporalHash> visual_hashes;
};

enum class SimilarityKind { None, Identical, SameContent, SubClip, Similar };

struct CompareConfig {
    float  tolerance = 0.30f;          // max Hamming fraction to consider a window match
    double duration_tolerance_pct = 20.0;
    float  min_matching_windows = 0.60f; // for SameContent
    float  subclip_min_match = 0.50f;     // for SubClip
};

struct CompareResult {
    SimilarityKind kind = SimilarityKind::None;
    float visual_score = 0.0f; // 0..1 (1 = identical)
    int64_t offset_ms = 0;     // where the clip starts inside the source (SubClip)
    bool clip_is_b = false;    // true if file B is the shorter clip
};

// Compute a video signature. Returns false on error (message in `err`).
// If `known_duration_ms` > 0 it is used directly (avoids an ffprobe call);
// otherwise duration/aspect are probed with ffprobe.
bool computeSignature(const std::string& path, const SignatureConfig& cfg,
                      VideoSignature& out, std::string& err, uint64_t known_duration_ms = 0);

// Override the ffmpeg / ffprobe binaries used for frame extraction. Defaults to
// "ffmpeg"/"ffprobe" resolved from PATH. In the packaged .app these point at the
// ffmpeg binary bundled next to the executable.
void setFfmpegPath(const std::string& p);
void setFfprobePath(const std::string& p);

// Pairwise comparison.
CompareResult compare(const VideoSignature& a, const VideoSignature& b,
                      const CompareConfig& cfg = {});

// Cluster signatures into groups of similar files (duration buckets + pairwise).
// Returns vector of (member indices, kind). Needs the original sig vector.
struct Group { std::vector<size_t> members; SimilarityKind kind; };
std::vector<Group> findSimilar(const std::vector<VideoSignature>& sigs,
                               const CompareConfig& cfg = {});

const char* kindName(SimilarityKind k);

} // namespace v3d
