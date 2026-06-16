# Basis Universal Texture Plugin for Unreal Engine 5

Proof-of-concept UE5 plugin and demo project for runtime Basis Universal / KTX2 texture transcoding.

## Motivation

Install size directly affects user acquisition and retention across virtually all game categories. Players who hesitate to download a large game, or who delete it during a storage cleanup, are customers lost. Keeping package size as small as possible is a genuine business concern, not just a technical preference.

### The problem with today's GPU texture formats

UE5 developers can already choose compression formats per texture — BC1, BC5, BC7, ASTC, and so on. The PC-targeting BC family offers only three distinct bitrates:

| Format | Bitrate | Notes |
|---|---|---|
| BC1 | 4 bpp | fixed |
| BC5 | 8 bpp | fixed |
| BC7 | 8 bpp | fixed |

There is no intermediate option for PC. A developer who wants something between BC1 and BC7 has no choice.

ASTC is more flexible — it supports 14 block sizes spanning roughly 0.9 to 8 bpp — but **ASTC is not natively supported on PC (DX11/DX12)**. Shipping ASTC to PC requires software decoding or a separate BC fallback, which means developers targeting both PC and mobile must still maintain two separate format pipelines and two pak files.

Regardless of format, developers must make separate format decisions for each target platform — BC7 for PC, ASTC for mobile — duplicating cook passes and pak storage.

Unreal Engine 5's **Oodle Texture RDO** improves on this by making GPU block data more compressible inside the pak, with negligible quality cost at typical settings. But it is still bound by these fixed bitrate floors — it cannot compress below what the GPU format physically requires.

### What Basis Universal makes possible

**Basis Universal** introduces a fundamentally different tradeoff space. XUASTC LDR alone spans **0.3 to 5.7 bpp** continuously across block sizes 4×4 through 12×12:

| Block size | Bitrate |
|---|---|
| XUASTC 4×4 | 5.7 bpp |
| XUASTC 6×6 | ~2.5 bpp |
| XUASTC 8×8 | 2.0 bpp |
| XUASTC 10×10 | 1.3 bpp |
| XUASTC 12×12 | 0.9 bpp |

This continuous space means a developer can allocate quality precisely — higher bitrate for hero assets seen up close, lower bitrate for distant or secondary textures — rather than jumping between coarse fixed formats.

More importantly, **a single Basis Universal (KTX2) asset transcodes to any GPU-native format at runtime**: BC5/BC7 on PC, ASTC on mobile. The developer never specifies a platform-specific format. One asset, one cook pass, one pak file serves all platforms.

The long-term goal of this project is to expose this capability directly in the UE5 texture pipeline: the developer sets a quality level, and the engine handles format selection and platform transcoding automatically. This repository is the proof-of-concept demonstrating these properties in UE5, along with a [Roadmap](#roadmap) toward full pipeline integration.

---

## Results

All comparisons are made against a **fair baseline**: the Standard build has Oodle Texture RDO enabled (`FinalRDOLambda=30`), which is Epic's own technology for optimizing GPU texture block data before pak compression.

| | Standard (BC1/BC5 + Oodle RDO) | Basis (ETC1S + XUASTC LDR 8x8) |
|---|---|---|
| **Pak size** | 178.0 MB | **152.7 MB (−14.5%)** |
| **Texture data in pak** | 39.1 MB | **13.8 MB (−64.7%)** |

> The pak contains substantial shared shader data (~138 MB) identical in both builds. The texture-only reduction of **−64.7%** represents the actual compression improvement, which translates to proportionally larger savings in texture-heavy real-world projects.

### Visual Comparison

The following screenshots were taken at the same sun angle (Pitch −35°, Yaw 60°) with no emissive contribution — direct lighting only.

**Brick wall (2048×2048)**

| Standard (BC1/BC5 + Oodle RDO) | Basis (ETC1S + XUASTC LDR 8×8) |
|:---:|:---:|
| ![Standard brick](docs/screenshots/standard_brick.png) | ![Basis brick](docs/screenshots/basis_brick.png) |

**Brown leather (2048×2048)**

| Standard (BC1/BC5 + Oodle RDO) | Basis (ETC1S + XUASTC LDR 8×8) |
|:---:|:---:|
| ![Standard leather](docs/screenshots/standard_leather.png) | ![Basis leather](docs/screenshots/basis_leather.png) |

> **Known limitation**: Some textures (particularly very light or highly saturated colors) show brightness differences between the two builds due to an unresolved sRGB handling issue with runtime-transcoded transient textures in UE5 5.7. The mid-tone textures shown above are not affected. This will be investigated and fixed in a post-submission update.

### Mobile (ASTC devices)

On ASTC-capable devices, XUASTC LDR 8×8 decompresses directly to native ASTC 8×8 blocks with nearly zero transcoding overhead (~33 ms for supercompression removal only, measured on PC).

> **Note**: This demo targets Win64 only. A proper mobile disk size comparison requires a measured mobile cook with Oodle compression as baseline, which is not included in this prototype. Mobile support is part of the [Roadmap](#roadmap).

### Single pak for all platforms (design goal)

A key architectural advantage of the Basis Universal approach is that **one KTX2 file can serve every target platform**. The Basis Universal transcoder supports transcoding to any GPU-native format at runtime:

| Platform | Transcodes to | Cost (measured on PC) |
|---|---|---|
| PC / Console (DX12/Vulkan) | BC7_RGBA (normal) | ~397 ms / 2048×2048 |
| PC / Console (DX12/Vulkan) | BC1 (albedo) | ~20 ms / 2048×2048 |
| Mobile (ASTC) | ASTC 8×8 | ~33 ms / 2048×2048 |
| Mobile (ETC2 fallback) | ETC2 | supported by transcoder |

> **Note**: This demo targets Win64 only. The mobile transcoding path is supported by the underlying `basist::ktx2_transcoder` but has not been tested on device in this prototype. Full multi-platform support is part of the [Roadmap](#roadmap).

In a standard UE5 multi-platform build, the cook process generates separate platform-specific paks — BC5/BC7 for PC, ASTC for iOS/Android — each requiring its own cook pass and storage. With Basis Universal, a single pak could ship to all platforms and transcoding would happen once at load time, simplifying the build pipeline.

### Runtime storage modes

This project targets two deployment policies built on the same cooked Basis payload:

| Mode | Shipping payload | Post-install storage | Runtime cost |
|---|---|---|---|
| **Footprint-Optimized** | Basis/KTX2 | Basis/KTX2 only | Transcode on load |
| **Download-Optimized Native Cache** | Basis/KTX2 | Basis/KTX2 + native BC/ASTC cache generated after first use | No Basis transcode after cache warm-up |

Footprint-Optimized mode keeps both download size and installed size small. It is the core value proposition for storage-constrained devices, demos, and texture-heavy projects where a small installed footprint matters.

Download-Optimized Native Cache mode keeps the installer small, then generates platform-native GPU blocks after install or first launch. Installed size grows toward the native BC/ASTC footprint, but subsequent loads skip Basis transcoding and behave closer to ordinary native textures.

The prototype exposes this as `RuntimeStorageMode` on each `UBasisTexture`. Games can either let cache files be generated lazily on first load, call `WarmNativeCacheForTextures()` during a first-launch preparation step, or call `WarmNativeCacheForTexturesBudgeted()` repeatedly from a loading screen to populate `Saved/BasisNativeCache` in small batches.

---

## What is XUASTC LDR?

XUASTC LDR is a supercompressed ASTC format introduced in **Basis Universal v2.10**. It applies Weight Grid DCT + RDO supercompression on top of ASTC blocks, achieving bitrates from 0.3 to 5.7 bpp across block sizes 4×4 through 12×12.

Key properties:
- **Explicitly supports normal maps** (documented in official Basis Universal spec)
- **KTX2 container** with `KTX2_SS_XUASTC_LDR` supercompression scheme
- **Transcodes to any GPU format** at runtime: BC5, BC7, ASTC, ETC2, etc.
- **`BASISD_SUPPORT_XUASTC=1` is the default** in the Basis Universal transcoder

This demo uses **XUASTC LDR 8×8 (2 bpp)** for normal maps and **ETC1S** for albedo textures.

### Quality

| Format | PSNR (2048×2048 normal map) |
|---|---|
| UASTC LDR 4×4 | 33.4 dB |
| XUASTC LDR 8×8 | 27.2 dB |

XUASTC LDR 8×8 at 27.2 dB PSNR was visually acceptable in this demo scene under typical viewing conditions. Results may vary with close-up inspection or grazing-angle lighting.

### Runtime transcoding (per 2048×2048 texture)

| Target format | Time |
|---|---|
| BC7_RGBA (normal maps) | ~397 ms |
| BC1_RGB (albedo) | ~20 ms |
| ASTC 8×8 (mobile, PC measurement) | ~33 ms |

---

## Project Structure

```
BasisDemo.uproject
├── Config/
│   └── DefaultEngine.ini          # Oodle Texture RDO settings
├── Plugins/
│   └── BasisUniversalTexture/     # UE5 plugin
│       └── Source/
│           ├── BasisUniversalTexture/
│           │   ├── Private/
│           │   │   ├── BasisTextureLoader.cpp   # Transcode to BC7/BC1
│           │   │   ├── BasisTexture.cpp
│           │   │   └── ThirdParty/
│           │   │       ├── BasisUniversal/      # basis_universal v2.10 transcoder
│           │   │       └── zstd/                # zstd single-file decoder
│           │   └── Public/
│           └── BasisUniversalTextureEditor/
└── Source/
    └── BasisDemo/
        ├── GalleryActor.cpp        # Runtime gallery: Standard vs Basis side-by-side
        └── GalleryActor.h
```

---

## Requirements

- Unreal Engine 5.7
- Visual Studio 2022
- [basisu CLI](https://github.com/BinomialLLC/basis_universal/releases) (for encoding KTX2 files)
- Source textures: 2K PNG normal maps and albedo maps

---

## Workflow

### 1. Encode normal maps to XUASTC LDR 8×8

```powershell
.\encode_normals_xuastc8x8.ps1
```

Encodes all `*nor_dx*.png` files in `source_textures/` to KTX2 using:
```
basisu <input>.png -ldr_8x8i -quality 128 -effort 6 -output_file <output>.ktx2
```

> **Note**: The `-normal_map` flag is intentionally omitted. With `-normal_map`, basisu stores the Y component in the Alpha channel for better UASTC compression. However, this causes incorrect channel mapping when transcoding to BC5_RG at runtime (the G channel receives garbage data). Omitting `-normal_map` keeps XY in the standard RG channels.

### 2. Deploy KTX2 files

```powershell
.\deploy_xuastc8x8.ps1
```

Copies encoded KTX2 files to the UE project's `basis_textures/` source directory.

### 3. Reimport into UE5

```powershell
.\run_reimport_normals.ps1
```

Runs `reimport_normals_uastc.py` via `UnrealEditor-Cmd.exe` to reimport KTX2 assets.

### 4. Package

```powershell
# Basis build (ETC1S albedo + XUASTC 8x8 normals)
.\package_basis_nobuild.ps1

# Standard build (BC1/BC5 + Oodle Texture RDO)
.\package_standard_timed.ps1
```

---

## Plugin Implementation Notes

- Normal maps (filename contains `_nor_`) are transcoded to **BC7_RGBA** at runtime.
- All other textures (albedo) are transcoded to **BC1_RGB** at runtime.
- **Note on normal map format**: BC5_RG would be the preferred format (0.5 bpp vs BC7's 1 bpp, higher per-channel precision for 2-channel data), and the Standard build uses BC5 for its normal maps. However, transcoding XUASTC LDR to BC5_RG at runtime produced incorrect lighting regardless of channel layout or material sampler configuration. BC7_RGBA transcodes all channels correctly and resolves the issue. The root cause (likely a UE5 runtime behavior difference between transient `PF_BC5` textures and cooked BC5 assets) remains under investigation.
- Imported `UBasisTexture` assets store the raw `.basis` / `.ktx2` bytes and transcode directly from memory; `LoadBasisTexture(FilePath)` remains as a standalone demo wrapper.
- `RuntimeStorageMode` controls whether an imported asset stays in Footprint-Optimized mode or writes native GPU blocks into `Saved/BasisNativeCache` for Download-Optimized Native Cache mode.
- `WarmNativeCache()`, `WarmNativeCacheForTexturesBudgeted()`, `ClearNativeCache()`, `HasNativeCache()`, and batch warm/clear helpers provide the prototype workflow for first-launch cache population and cache management.
- Native cache files include a cache version and target GPU profile, are written through a temporary file, and are discarded/regenerated when invalid or stale.
- The transcoder uses `basist::ktx2_transcoder`, which handles UASTC+Zstd, XUASTC LDR, and ETC1S natively (`BASISD_SUPPORT_XUASTC=1` by default).
- `PrivatePCHHeaderFile` is set to a plugin-local PCH to avoid loading the 2+ GB shared UE editor PCH on every incremental build.

---

## Background

This demo was developed to evaluate Basis Universal as a practical texture compression pipeline for Unreal Engine 5, with a focus on:

1. **Accurate comparison** — enabling Oodle Texture RDO on the Standard build ensures a fair baseline
2. **XUASTC LDR discovery** — demonstrating the first known UE5 integration of the XUASTC LDR format
3. **Mobile-first potential** — XUASTC 8×8 → ASTC 8×8 transcoding is nearly free on mobile hardware (~33 ms measured on PC), adding domain-specific supercompression on top of standard ASTC blocks

---

## Roadmap

This repository is a proof of concept. The goal is to evolve it into a fully integrated UE5 texture compression pipeline with a simple developer experience: **set a quality level, and the engine handles the rest** — format selection, platform transcoding, and pak optimization — automatically.

### 1. UE5 Texture Pipeline Integration

Currently, Basis Universal assets are imported through a custom factory (`BasisTextureFactory`) and loaded via a separate loader class. This requires a parallel asset management workflow alongside standard `UTexture2D` assets.

The target architecture is a native UE5 `ITextureFormat` plugin that hooks into the standard compression pipeline:

- **Quality-driven format selection**: The developer selects a quality level (or size budget) per texture in the Compression Settings dropdown. The plugin automatically chooses the optimal XUASTC LDR block size (4×4 through 12×12) or ETC1S to meet that target — no manual format knowledge required.
- **Platform-transparent transcoding**: At runtime, the plugin transcodes each KTX2 asset to the platform-native GPU format (BC5/BC7 on PC/console, ASTC on mobile) without any developer input. A single cooked asset serves all platforms.
- **Zero workflow changes**: Encoding happens at cook time via `ITextureFormatModule`. The cooked assets are standard `UTexture2D` objects, fully compatible with materials, streaming, and LOD.

This unifies what is currently a fragmented decision — "which format for which platform?" — into a single quality dial that works across all targets.

### 2. Automatic Per-Texture Optimization

Today, developers must manually assess each texture's role (hero vs. secondary, close-up vs. distant) and assign formats accordingly. A production-ready integration will include tooling to automate this:

- Analyze each texture's usage context (material slot, LOD range, screen-space coverage)
- Assign quality targets automatically based on perceptual importance
- Distribute a project-wide size budget optimally across the texture library

This kind of per-texture quality allocation is only possible with a continuous bitrate space like XUASTC LDR. Fixed-bitrate GPU formats (BC1, BC7) cannot offer it.

### 3. Mip Streaming Support

The current implementation loads only mip level 0 (full resolution). A production-ready integration requires full mip chain support and compatibility with UE5's texture streaming system:

- Encode and store all mip levels in the KTX2 container at cook time
- At runtime, integrate with `FStreamableRenderResourceState` so UE5's streaming manager can request individual mip levels on demand
- Reduce initial VRAM usage and support large open-world scenes where texture streaming is critical

Mip streaming support is a prerequisite for any production use case.

### 4. Runtime Storage Policies

The current prototype stores imported Basis Universal bytes in `UBasisTexture` and supports two runtime storage policies:

- **Footprint-Optimized**: keep installed assets as Basis/KTX2 and transcode on load.
- **Download-Optimized Native Cache**: keep the shipping payload small, then persist transcoded native GPU blocks under `Saved/BasisNativeCache` after first use.

The prototype includes synchronous cache warm-up helpers for first-launch preparation. A production implementation still needs async cache warm-up, cache invalidation tied to cooked asset versions, platform-specific cache locations, and direct integration with cooked `UTexture2D` bulk data so runtime loading behaves like native UE texture streaming instead of a transient texture demo path.

---

## License

Plugin source code: MIT
Basis Universal transcoder: Apache 2.0 (Binomial LLC)
zstd: BSD (Meta Platforms)
