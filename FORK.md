# Fork / Modified Version Notice — `leooooooooo/video-simili-duplicate-cleaner`

> **中文摘要（见下方英文正式声明）**
> 本仓库是 Théophane Mayaud 开发的 **Video simili duplicate cleaner** 的**修改版 / 衍生版（fork）**，
> 原项目以 **GNU GPL v3** 许可证发布。本仓库同样以 **GPL v3** 发布，并保留原作者版权与署名。
> 本仓库**不是**官方 App Store 版本，仅作个人备份与二次开发使用。
> 本人在原项目基础上新增/修改了若干功能（见下方“修改内容清单”），新增代码同样以 GPL v3 授权。

---

## 1. Original work（原始作品）

| Item | Detail |
| --- | --- |
| **Project** | Video simili duplicate cleaner |
| **Original author** | Théophane Mayaud |
| **Original repository** | https://github.com/theophanemayaud/video-simili-duplicate-cleaner |
| **Original license** | GNU General Public License **version 3 (GPL v3)**, 29 June 2007 |
| **Copyright** | Copyright (C) 2020–2026 Théophane Mayaud |

This fork is based on the upstream project and is a **modified/derivative work** under GPL v3
§5 ("Modified Source Versions"). The original author's copyright and license are preserved
unaltered in [`LICENSE.md`](LICENSE.md) and [`CREDITS.md`](CREDITS.md).

## 2. This fork（本衍生版本）

| Item | Detail |
| --- | --- |
| **Fork maintainer** | `leooooooooo` (GitHub: https://github.com/leooooooooo) |
| **Fork repository** | https://github.com/leooooooooo/video-simili-duplicate-cleaner |
| **License of modifications** | GNU GPL v3 (same as upstream) |
| **Year of modifications** | 2026 |
| **Status** | Personal backup + secondary development. **Not** the official App Store release. |

In accordance with **GPL v3 §5(a)**, this repository prominently states that it is a changed
version and notes the year of modification. In accordance with **GPL v3 §5(c)**, all original
copyright, license, and attribution notices are retained.

## 3. List of modifications（修改内容清单）

Custom features added / changed by the fork maintainer in 2026:

- **Delete Both** — a single confirm-then-delete action that removes *both* the left and right
  video files at once in the manual comparison window.
- **Move & Replace** — `leftMoveReplace` (move the left file into the right folder and delete the
  right file) and `rightMoveReplace` (mirror) for symmetric, replace-style reorganization.
- **Native multi-folder selection** — macOS native `NSOpenPanel` for selecting multiple folders.
- **Direct-deletion default** — default action deletes files directly rather than only to trash.
- **Only-different-folders filter** — a filter that only shows pairs whose files are in different folders.
- **Clickable file path** — the file path in the comparison window is clickable to reveal the file.
- **Removed forced App Store receipt validation** — the local build no longer calls
  `exit(173)` when no App Store receipt is present (`mainwindow.cpp`), so the locally built
  binary runs without the Mac App Store.
- **CMake / Homebrew build branch** — adjusted CMake logic to support local Homebrew-based builds.
- **3D-DCT comparison mode** — a new selectable algorithm ("3D-DCT") added alongside pHash/SSIM.
  It is a C++ port of the visual **3D-DCT video hashing** used by Czkawka's `similario_core` engine
  (MIT licensed, © Rafał Mikrut / qarmin — compatible with GPL v3). It splits each video into
  temporal windows, builds a 16×16×16 cube of grayscale frames, applies a separable 3D-DCT-II, and
  binarizes the low-frequency corner into a 1000-bit-per-window hash. Comparison uses Hamming distance
  plus duration bucketing and **sliding-window SubClip detection**, which catches mid-clip extracts and
  intro/outro ads that pHash/SSIM cannot. In this port, the window count is **auto-proportional to
  duration** (not fixed) so the longer source gets more windows than the shorter clip — this is what
  makes arbitrary middle-segment detection actually work. The ffmpeg binary is bundled inside the
  `.app` (resolved at runtime) and the video duration is taken from the app's metadata, so no ffprobe
  call is needed.
  - New files: `QtProject/app/video3ddct.h`, `QtProject/app/video3ddct.cpp`
  - Wired into: `prefs.h` (new `_3DDCT` mode + `_threshold3DDCT`), `Video::dct3dSignature()` (cached),
    `videopairmatcher.cpp` (3D branch in `match()`), `mainwindow.cpp`/`comparison.cpp` + their `.ui`
    (third radio button), `main.cpp` (bundled-ffmpeg path), and the build/packaging script (ffmpeg
    bundled into the `.app`).

### Modified source files（被修改的源文件）

The following tracked files were modified for this fork (dates: 2026):

- `QtProject/CMakeLists.txt`
- `QtProject/app/comparison/comparison.cpp`
- `QtProject/app/comparison/comparison.h`
- `QtProject/app/comparison/comparison.ui`
- `QtProject/app/mainwindow.cpp`
- `QtProject/app/mainwindow.h`
- `QtProject/app/mainwindow.ui`
- `QtProject/app/obj-c.h`
- `QtProject/app/obj-c.mm`
- `QtProject/app/prefs.h`
- `QtProject/app/main.cpp`
- `QtProject/app/video.cpp`
- `QtProject/app/comparison/internal/videopairmatcher.cpp`
- `QtProject/app/comparison/internal/videopairmatcher.h`
- `QtProject/app/video3ddct.h` *(new)*
- `QtProject/app/video3ddct.cpp` *(new)*

## 4. Removed content（相对上游删除的内容）

- The `samples/` folder (demo videos / screenshots) has been **removed** from this fork. The
  `samples/...` image links in the original README therefore do not resolve in this repository.

## 5. Source code availability（源代码提供）

Per GPL v3 §6, the complete corresponding source code of this fork is available in this public
repository: https://github.com/leooooooooo/video-simili-duplicate-cleaner

## 6. Note on binary redistribution（关于二进制分发的说明）

The upstream `README.md` contains a *request* from the original author: "please … do not
redistribute the binary." This is a **courtesy request**, not a license term — the GPL v3
grants the right to distribute binaries provided the source is offered under the same license.
This fork currently distributes **source only** (no binary) on GitHub, which fully complies with
GPL v3. If binaries are ever distributed, the source must remain available under GPL v3.

## 7. Third-party components（第三方组件）

- **Vidupe** — Copyright (C) 2018–2019 Kristian Koskimäki, GPL. (Original inspiration / code.)
- **SSIM implementation** — Copyright (c) 2018 Ruofei Du, MIT License (bundled in
  `QtProject/app/comparison/internal/ssim.cpp`).
- **App icon** — Flaticon author *xnimrodx*.

See [`CREDITS.md`](CREDITS.md) and [`DEPENDENCIES.md`](DEPENDENCIES.md) for full details.

---

*This notice is provided to satisfy the attribution and "modified version" requirements of the
GNU General Public License v3. It does not limit the rights granted by that license.*
