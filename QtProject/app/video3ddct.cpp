// video3ddct.cpp — implementation. See video3ddct.h for the algorithm overview.

#include "video3ddct.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <sstream>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using std::string;
using std::vector;
namespace fs = std::filesystem;

namespace v3d {

// ffmpeg / ffprobe binaries, overridable at runtime (e.g. to the copy bundled
// inside the .app). Defaults resolve from PATH.
static std::string g_ffmpegPath = "ffmpeg";
static std::string g_ffprobePath = "ffprobe";

void setFfmpegPath(const std::string& p) { g_ffmpegPath = p; }
void setFfprobePath(const std::string& p) { g_ffprobePath = p; }

// Unique temp file path for a single ffmpeg rawvideo dump.
static std::string make_temp_path() {
    static std::atomic<uint64_t> counter{0};
    fs::path p = fs::temp_directory_path();
    p /= ("v3d_" + std::to_string(getpid()) + "_" + std::to_string(counter.fetch_add(1)) + ".raw");
    return p.string();
}

// ---------------------------------------------------------------------------
// TemporalHash
// ---------------------------------------------------------------------------
int TemporalHash::hamming(const TemporalHash& o) const {
    int d = 0;
    for (int i = 0; i < HASH_BYTES; i++)
        d += __builtin_popcount((uint8_t)(bits[i] ^ o.bits[i]));
    return d;
}

float TemporalHash::normalized_distance(const TemporalHash& o) const {
    return (float)hamming(o) / (float)HASH_BITS;
}

// ---------------------------------------------------------------------------
// 1D DCT-II (in-place, length n)
// ---------------------------------------------------------------------------
static void dct1d(double* x, int n) {
    vector<double> out(n);
    for (int k = 0; k < n; k++) {
        double s = 0.0;
        for (int m = 0; m < n; m++)
            s += x[m] * cos(M_PI / n * (m + 0.5) * k);
        out[k] = s;
    }
    for (int k = 0; k < n; k++) x[k] = out[k];
}

static void transpose_0_1(double a[16][16][16]) {
    double t[16][16][16];
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 16; k++)
                t[j][i][k] = a[i][j][k];
    std::memcpy(a, t, sizeof(t));
}
static void transpose_0_2(double a[16][16][16]) {
    double t[16][16][16];
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 16; k++)
                t[k][j][i] = a[i][j][k];
    std::memcpy(a, t, sizeof(t));
}

// Separable 3D-DCT-II (matches similario_core: 3 passes + 2 restore transposes).
static void dct3d(double a[16][16][16]) {
    for (int j = 0; j < 16; j++)
        for (int k = 0; k < 16; k++)
            dct1d(&a[0][j][k], 16);   // axis 0 (frame)
    transpose_0_1(a);
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 16; k++)
            dct1d(&a[i][0][k], 16);   // axis 1 (x)
    transpose_0_2(a);
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            dct1d(&a[i][j][0], 16);   // axis 2 (y)
    transpose_0_2(a);
    transpose_0_1(a);
}

// 16 grayscale 16x16 frames -> 1000-bit hash (low-frequency 10x10x10 corner).
static TemporalHash compute_hash_from_frames(const std::array<uint8_t, 256> frames[16]) {
    double cube[16][16][16];
    for (int fi = 0; fi < 16; fi++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                cube[fi][x][y] = (double)frames[fi][y * 16 + x] - 128.0;

    dct3d(cube);

    TemporalHash h;
    int bit = 0;
    for (int fi = 0; fi < HASH_SIZE; fi++)
        for (int xi = 0; xi < HASH_SIZE; xi++)
            for (int yi = 0; yi < HASH_SIZE; yi++) {
                if (bit >= HASH_BITS) break;
                if (cube[fi][xi][yi] > 0.0)
                    h.bits[bit / 8] |= (uint8_t)(1u << (bit % 8));
                bit++;
            }
    return h;
}

// ---------------------------------------------------------------------------
// Window positions
// ---------------------------------------------------------------------------
static vector<std::pair<double, double>> compute_window_positions(double dur, const SignatureConfig& cfg) {
    int n = cfg.auto_window_count
                ? std::clamp((int)(dur / 3.0), 3, 40)
                : std::max(1, cfg.window_count);

    double usable_start = std::min(cfg.skip_secs, dur * 0.15);
    double usable_end = std::max(dur - 0.5, usable_start + 1.0);
    double usable = usable_end - usable_start;

    double window_secs = std::min(cfg.window_secs, usable);
    double step = (n > 1) ? (usable - window_secs) / (n - 1) : 0.0;

    vector<std::pair<double, double>> out;
    for (int i = 0; i < n; i++) {
        double start = usable_start + i * step;
        double end = std::min(start + window_secs, dur - 0.1);
        end = std::max(end, start + 0.5);
        out.push_back({start, end});
    }
    return out;
}

// ---------------------------------------------------------------------------
// ffmpeg frame extraction (one process per window, fast seek)
// ---------------------------------------------------------------------------
static string shell_quote(const string& s) {
    string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static bool run_ffmpeg_seeked(const string& path, double start, double dur,
                              const string& vf, int max_frames,
                              vector<uint8_t>& raw, string& err) {
    string outpath = make_temp_path();
    std::ostringstream cmd;
    cmd << g_ffmpegPath
        << " -hide_banner -loglevel error -threads 1"
        << " -ss " << start << " -t " << dur
        << " -i " << shell_quote(path)
        << " -vf " << shell_quote(vf)
        << " -vframes " << max_frames
        << " -f rawvideo -pix_fmt gray " << shell_quote(outpath) << " 2>/dev/null";

    int st = std::system(cmd.str().c_str());
    {
        std::ifstream f(outpath, std::ios::binary);
        if (f) {
            f.seekg(0, std::ios::end);
            std::streamoff sz = f.tellg();
            f.seekg(0, std::ios::beg);
            if (sz > 0) {
                raw.resize((size_t)sz);
                f.read(reinterpret_cast<char*>(raw.data()), sz);
            }
        }
    }
    std::remove(outpath.c_str());
    if (st != 0 && raw.empty()) { err = "ffmpeg failed (exit " + std::to_string(st) + ")"; return false; }
    return true;
}

// Letterbox / cropdetect (ported from similario_core/src/visual/cropdetect.rs)
namespace cropdetect {
struct Crop { int left = 0, right = 0, top = 0, bottom = 0;
    bool empty() const { return left == 0 && right == 0 && top == 0 && bottom == 0; }
    Crop union_with(const Crop& o) const {
        return Crop{std::min(left,o.left), std::min(right,o.right),
                    std::min(top,o.top), std::min(bottom,o.bottom)};
    }
};
static bool is_letterbox_strip(const vector<uint8_t>& p) {
    if (p.empty()) return false;
    int hist[256] = {0};
    for (uint8_t v : p) hist[v]++;
    int mode = 0; int best = -1;
    for (int i = 0; i < 256; i++) if (hist[i] > best) { best = hist[i]; mode = i; }
    if (mode > 32) return false;
    int matching = 0;
    for (uint8_t v : p) if ((int)std::abs(mode - (int)v) <= 16) matching++;
    return (double)matching / p.size() >= 0.90;
}
static Crop detect_letterbox(const uint8_t* frame, int w, int h) {
    auto scan = [&](bool horizontal, int from) -> int {
        int max_strip = horizontal ? h / 2 : w / 2;
        for (int off = 0; off < max_strip; off++) {
            vector<uint8_t> strip;
            if (horizontal) {
                int y = (from == 0) ? off : (h - 1 - off);
                for (int x = 0; x < w; x++) strip.push_back(frame[y * w + x]);
            } else {
                int x = (from == 0) ? off : (w - 1 - off);
                for (int y = 0; y < h; y++) strip.push_back(frame[y * w + x]);
            }
            if (!is_letterbox_strip(strip)) return off;
        }
        return max_strip;
    };
    Crop c;
    c.top = scan(true, 0);
    c.bottom = scan(true, 1);
    c.left = scan(false, 0);
    c.right = scan(false, 1);
    int uw = w - (int)(c.left + c.right);
    int uh = h - (int)(c.top + c.bottom);
    if (uw <= 0 || uh <= 0) return Crop{};
    return c;
}
static Crop detect_letterbox_multi(const vector<vector<uint8_t>>& frames, int w, int h) {
    Crop acc;
    int step = std::max(1, (int)frames.size() / 8);
    bool first = true;
    for (size_t i = 0; i < frames.size(); i += step) {
        Crop c = detect_letterbox(frames[i].data(), w, h);
        acc = first ? c : acc.union_with(c);
        first = false;
    }
    return acc;
}
} // namespace cropdetect

static string detect_crop(const string& path,
                          const vector<std::pair<double,double>>& windows,
                          const SignatureConfig& cfg) {
    if (windows.empty()) return "";
    auto [start, end] = windows[windows.size() / 2];
    double dur = end - start;
    int prescan = 8;
    double fps = prescan / std::max(dur, 0.1);
    int PW = 160, PH = 120;
    string vf = "fps=" + std::to_string(fps) +
                ",scale=" + std::to_string(PW) + ":" + std::to_string(PH) +
                ":flags=bilinear,format=gray";
    vector<uint8_t> raw;
    string err;
    if (!run_ffmpeg_seeked(path, start, dur, vf, prescan, raw, err)) return "";
    int pbytes = PW * PH;
    vector<vector<uint8_t>> frames;
    for (size_t i = 0; i + pbytes <= raw.size(); i += pbytes)
        frames.emplace_back(raw.begin() + i, raw.begin() + i + pbytes);
    if (frames.empty()) return "";
    auto crop = cropdetect::detect_letterbox_multi(frames, PW, PH);
    if (crop.empty()) return "";
    double cx = (double)crop.left / PW;
    double cy = (double)crop.top / PH;
    double cw = 1.0 - (double)(crop.left + crop.right) / PW;
    double ch = 1.0 - (double)(crop.top + crop.bottom) / PH;
    return "crop=iw*" + std::to_string(cw) + ":ih*" + std::to_string(ch) +
           ":iw*" + std::to_string(cx) + ":ih*" + std::to_string(cy) + ",";
}

// Black-frame handling
static bool is_black_frame(const uint8_t* data, int len) {
    int dark = 0;
    for (int i = 0; i < len; i++) if (data[i] <= 0x20) dark++;
    return (double)dark / len >= 0.80;
}

static void replace_black_frames(vector<std::array<uint8_t,256>>& frames, int fpw) {
    int nw = (int)frames.size() / fpw;
    for (int w = 0; w < nw; w++) {
        int base = w * fpw;
        int fb = -1;
        for (int i = 0; i < fpw; i++)
            if (!is_black_frame(frames[base + i].data(), 256)) { fb = i; break; }
        for (int i = 0; i < fpw; i++) {
            if (!is_black_frame(frames[base + i].data(), 256)) continue;
            int rep = -1;
            for (int j = i + 1; j < fpw; j++)
                if (!is_black_frame(frames[base + j].data(), 256)) { rep = j; break; }
            if (rep < 0)
                for (int j = i - 1; j >= 0; j--)
                    if (!is_black_frame(frames[base + j].data(), 256)) { rep = j; break; }
            if (rep < 0) rep = fb;
            if (rep >= 0 && rep != i) frames[base + i] = frames[base + rep];
        }
    }
}

static bool extract_frames_multi_window(const string& path,
                                         const vector<std::pair<double,double>>& windows,
                                         const SignatureConfig& cfg,
                                         vector<std::array<uint8_t,256>>& out_frames,
                                         string& err) {
    const int FPW = FRAMES_PER_WINDOW;
    string crop_filter = cfg.cropdetect ? detect_crop(path, windows, cfg) : "";

    for (size_t wi = 0; wi < windows.size(); wi++) {
        double start = windows[wi].first, end = windows[wi].second;
        double dur = end - start;
        double fps = FPW / std::max(dur, 0.1);
        string vf = "fps=" + std::to_string(fps) + "," + crop_filter +
                    "scale=" + std::to_string(DCT_SIZE) + ":" + std::to_string(DCT_SIZE) +
                    ":flags=bilinear,format=gray";
        vector<uint8_t> raw;
        if (!run_ffmpeg_seeked(path, start, dur, vf, FPW, raw, err)) return false;

        size_t got = raw.size() / 256;
        for (size_t i = 0; i < got && out_frames.size() < (wi + 1) * FPW; i++) {
            std::array<uint8_t,256> f;
            std::memcpy(f.data(), raw.data() + i * 256, 256);
            out_frames.push_back(f);
        }
        while (out_frames.size() < (wi + 1) * FPW)
            out_frames.push_back(std::array<uint8_t,256>{});
    }
    replace_black_frames(out_frames, FPW);
    return true;
}

// ---------------------------------------------------------------------------
// ffprobe metadata
// ---------------------------------------------------------------------------
static bool probe_metadata(const string& path, double& duration_secs, float& aspect, string& err) {
    auto run = [](const string& c) -> string {
        FILE* f = popen(c.c_str(), "r");
        if (!f) return "";
        string s; char b[512];
        while (fgets(b, sizeof(b), f)) s += b;
        pclose(f);
        return s;
    };
    string dcmd = g_ffprobePath + " -v error -show_entries format=duration -of default=nw=1:nk=1 " + shell_quote(path) + " 2>/dev/null";
    string dout = run(dcmd);
    if (dout.empty() || sscanf(dout.c_str(), "%lf", &duration_secs) != 1) {
        err = "ffprobe duration failed"; return false;
    }
    string scmd = g_ffprobePath + " -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 " + shell_quote(path) + " 2>/dev/null";
    string sout = run(scmd);
    int w = 0, h = 0;
    if (sscanf(sout.c_str(), "%d,%d", &w, &h) == 2 && h > 0)
        aspect = (float)w / (float)h;
    else
        aspect = 16.0f / 9.0f;
    return true;
}

// ---------------------------------------------------------------------------
// Public: computeSignature
// ---------------------------------------------------------------------------
bool computeSignature(const string& path, const SignatureConfig& cfg,
                      VideoSignature& out, string& err, uint64_t known_duration_ms) {
    double duration_secs = 0;
    float aspect = 16.0f / 9.0f;
    if (known_duration_ms > 0) {
        duration_secs = (double)known_duration_ms / 1000.0;
    } else if (!probe_metadata(path, duration_secs, aspect, err)) {
        return false;
    }

    auto windows = compute_window_positions(duration_secs, cfg);
    vector<std::array<uint8_t,256>> frames;
    if (!extract_frames_multi_window(path, windows, cfg, frames, err)) return false;

    out.path = path;
    out.duration_ms = (uint64_t)(duration_secs * 1000.0);
    out.aspect_ratio = aspect;

    int fpw = FRAMES_PER_WINDOW;
    for (size_t wi = 0; wi < windows.size(); wi++) {
        std::array<uint8_t,256> win[16];
        for (int i = 0; i < fpw; i++) win[i] = frames[wi * fpw + i];
        TemporalHash th = compute_hash_from_frames(win);
        th.start_ms = (uint64_t)(windows[wi].first * 1000.0);
        th.end_ms = (uint64_t)(windows[wi].second * 1000.0);
        out.visual_hashes.push_back(th);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------
static std::optional<std::pair<float, uint64_t>>
sliding_window_match(const vector<TemporalHash>& clip, const vector<TemporalHash>& source,
                     float tol, float min_match) {
    int cn = (int)clip.size(), sn = (int)source.size();
    if (cn == 0 || sn < cn) return std::nullopt;
    float best = 0; uint64_t best_off = 0;
    for (int off = 0; off <= sn - cn; off++) {
        int matching = 0;
        for (int i = 0; i < cn; i++)
            if (clip[i].normalized_distance(source[off + i]) <= tol) matching++;
        float ratio = (float)matching / cn;
        if (ratio >= min_match) {
            float score = 0;
            for (int i = 0; i < cn; i++)
                score += 1.0f - clip[i].normalized_distance(source[off + i]);
            score /= cn;
            if (score > best) { best = score; best_off = source[off].start_ms; }
        }
    }
    if (best > 0) return std::make_pair(best, best_off);
    return std::nullopt;
}

CompareResult compare(const VideoSignature& a, const VideoSignature& b, const CompareConfig& cfg) {
    CompareResult r;
    const auto& ha = a.visual_hashes;
    const auto& hb = b.visual_hashes;
    if (ha.empty() || hb.empty()) return r;

    const TemporalHash& mid_a = ha[ha.size() / 2];
    const TemporalHash& mid_b = hb[hb.size() / 2];
    if (mid_a.normalized_distance(mid_b) > cfg.tolerance * 1.5f) return r; // prefilter

    if (ha.size() == hb.size()) {
        int matching = 0; float sum = 0;
        for (size_t i = 0; i < ha.size(); i++) {
            float d = ha[i].normalized_distance(hb[i]);
            if (d <= cfg.tolerance) matching++;
            sum += 1.0f - d;
        }
        float ratio = (float)matching / (float)ha.size();
        if (ratio >= cfg.min_matching_windows) {
            float avg = sum / (float)ha.size();
            r.kind = avg >= 0.97f ? SimilarityKind::Identical : SimilarityKind::SameContent;
            r.visual_score = avg;
            return r;
        }
    }

    bool a_shorter = a.duration_ms <= b.duration_ms;
    const VideoSignature& shorter = a_shorter ? a : b;
    const VideoSignature& longer = a_shorter ? b : a;
    double ratio = (double)shorter.duration_ms / (double)std::max<long long>(longer.duration_ms, 1);
    if (ratio >= 0.10) {
        auto sw = sliding_window_match(shorter.visual_hashes, longer.visual_hashes,
                                       cfg.tolerance, cfg.subclip_min_match);
        if (sw) {
            r.kind = SimilarityKind::SubClip;
            r.visual_score = sw->first;
            r.offset_ms = sw->second;
            r.clip_is_b = !a_shorter; // shorter is B => clip_is_b true
            return r;
        }
    }

    float mid_score = 1.0f - mid_a.normalized_distance(mid_b);
    if (mid_score >= 1.0f - cfg.tolerance) {
        r.kind = SimilarityKind::Similar;
        r.visual_score = mid_score;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Grouping (duration buckets + pairwise + anti-daisy-chain clustering)
// ---------------------------------------------------------------------------
const char* kindName(SimilarityKind k) {
    switch (k) {
        case SimilarityKind::None: return "None";
        case SimilarityKind::Identical: return "Identical";
        case SimilarityKind::SameContent: return "SameContent";
        case SimilarityKind::SubClip: return "SubClip";
        case SimilarityKind::Similar: return "Similar";
    }
    return "?";
}

std::vector<Group> findSimilar(const vector<VideoSignature>& sigs, const CompareConfig& cfg) {
    // duration buckets
    vector<size_t> order(sigs.size());
    for (size_t i = 0; i < sigs.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t x, size_t y){ return sigs[x].duration_ms < sigs[y].duration_ms; });

    double tol = cfg.duration_tolerance_pct / 100.0;
    vector<vector<size_t>> buckets;
    vector<size_t> cur;
    double center = 0;
    for (size_t idx : order) {
        double dur = (double)sigs[idx].duration_ms;
        if (cur.empty()) { cur.push_back(idx); center = dur; }
        else if (std::fabs(dur - center) / std::max(center, 1.0) <= tol) cur.push_back(idx);
        else { buckets.push_back(cur); cur = {idx}; center = dur; }
    }
    if (!cur.empty()) buckets.push_back(cur);

    // pairwise within buckets
    struct Pair { size_t a, b; CompareResult r; };
    vector<Pair> pairs;
    for (auto& bkt : buckets)
        for (size_t i = 0; i < bkt.size(); i++)
            for (size_t j = i + 1; j < bkt.size(); j++) {
                CompareResult r = compare(sigs[bkt[i]], sigs[bkt[j]], cfg);
                if (r.kind != SimilarityKind::None)
                    pairs.push_back({bkt[i], bkt[j], r});
            }

    // cluster (representative per group, anti-daisy-chain)
    vector<size_t> group_id(sigs.size(), (size_t)-1);
    vector<size_t> reps;
    vector<std::pair<vector<size_t>, SimilarityKind>> groups;
    for (auto& p : pairs) {
        size_t ga = group_id[p.a], gb = group_id[p.b];
        if (ga == (size_t)-1 && gb == (size_t)-1) {
            size_t gid = groups.size();
            group_id[p.a] = gid; group_id[p.b] = gid;
            reps.push_back(p.a);
            groups.push_back({{p.a, p.b}, p.r.kind});
        } else if (ga != (size_t)-1 && gb == (size_t)-1) {
            group_id[p.b] = ga;
            groups[ga].first.push_back(p.b);
        } else if (ga == (size_t)-1 && gb != (size_t)-1) {
            group_id[p.a] = gb;
            groups[gb].first.push_back(p.a);
        }
        // both assigned: skip merge (avoid daisy chains)
    }

    vector<Group> out;
    for (auto& g : groups)
        if (g.first.size() >= 2)
            out.push_back({g.first, g.second});
    return out;
}

} // namespace v3d
