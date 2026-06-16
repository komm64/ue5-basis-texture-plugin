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

On ASTC-capable devices, XUASTC LDR 8×8 transcodes directly to native ASTC 8×8 blocks, avoiding the much heavier BC7 path. The current PC measurement projects to roughly ~72 ms per 2048×2048 texture for CPU transcode only.

> **Note**: This demo targets Win64 only. A proper mobile disk size comparison requires a measured mobile cook with Oodle compression as baseline, which is not included in this prototype. Mobile support is part of the [Roadmap](#roadmap).

### Single pak for all platforms (design goal)

A key architectural advantage of the Basis Universal approach is that **one KTX2 file can serve every target platform**. The Basis Universal transcoder supports transcoding to any GPU-native format at runtime:

| Platform / profile | Transcodes to | Cost (measured on PC) |
|---|---|---|
| PC / Console fast runtime | BC1_RGB (color) | ~23 ms / 2048×2048 projected |
| PC / Console fast runtime | BC3_RGBA (color + alpha) | ~22 ms / 2048×2048 projected |
| PC / Console fast runtime | BC5_RG (XUASTC RRRG normal) | ~150 ms / 2048×2048 projected |
| PC / Console quality/native-cache | BC7_RGBA (normal or alpha) | ~254 ms median, ~397 ms worst-case class / 2048×2048 |
| Mobile fast runtime (ASTC source) | ASTC 8×8 | ~72 ms / 2048×2048 projected |
| Mobile (ETC2 fallback) | ETC2 | supported by transcoder |

> **Note**: This demo targets Win64 only. The mobile transcoding path is supported by the underlying `basist::ktx2_transcoder` but has not been tested on device in this prototype. Full multi-platform support is part of the [Roadmap](#roadmap).

Runtime transcode is not intended for bulk startup conversion of hundreds or thousands of textures. Use native cache warm-up, throttled async jobs, or cook/install-time native conversion for large libraries.

In a standard UE5 multi-platform build, the cook process generates separate platform-specific paks — BC5/BC7 for PC, ASTC for iOS/Android — each requiring its own cook pass and storage. With Basis Universal, a single pak could ship to all platforms and transcoding would happen once at load time, simplifying the build pipeline.

### Runtime storage modes

This project targets three deployment policies. The production-oriented path is **Install-Time Native Only**: ship a compact external Basis/KTX2 payload, generate native GPU blocks during install/finalization or first launch, then delete the source payload so runtime loads only BC/ASTC data.

| Mode | Shipping payload | Post-install storage | Runtime cost |
|---|---|---|---|
| **Footprint-Optimized** | Embedded or external Basis/KTX2 | Basis/KTX2 only | Transcode on load |
| **Download-Optimized Native Cache** | Embedded or external Basis/KTX2 | Basis/KTX2 + native BC/ASTC cache | No Basis transcode after cache warm-up; cache can regenerate |
| **Install-Time Native Only** | External Basis/KTX2 install payload | Native BC/ASTC cache only after finalization | No Basis transcode; cache miss is a release error |

Footprint-Optimized mode keeps both download size and installed size small. It is the core value proposition for storage-constrained devices, demos, and texture-heavy projects where a small installed footprint matters.

Download-Optimized Native Cache mode keeps the installer small, then generates platform-native GPU blocks after install or first launch. Installed size grows because the source Basis/KTX2 payload is retained for cache recovery, but subsequent loads skip Basis transcoding and behave closer to ordinary native textures.

Install-Time Native Only mode is for large projects that want installer/download savings without runtime transcode spikes. The asset stores `ExternalBasisPayloadPath` plus a stable `SourcePayloadCrc`, warms `Saved/BasisNativeCache`, and then `DiscardExternalSourcePayload()` or the validation commandlet can remove the external payload. At runtime this mode never regenerates cache; missing or stale native blocks fail loudly.

The prototype exposes this as `RuntimeStorageMode` on each `UBasisTexture`. Games can either let cache files be generated lazily on first load, call `WarmNativeCacheForTextures()` during a first-launch preparation step, call `WarmNativeCacheForTexturesBudgeted()` repeatedly from a loading screen, or call `WarmNativeCacheForTexturesAsyncThrottled()` to populate `Saved/BasisNativeCache` with a bounded number of worker-thread transcodes.

Imported assets also store an editable `TextureSemantic` (`Color`, `Color With Alpha`, `Normal Map`, or `Data / Linear`). The importer guesses the initial value from common filename suffixes such as `_nor`, `_normal`, `_nrm`, `_roughness`, `_metallic`, `_ao`, `_height`, `_mask`, `_alpha`, `_opacity`, and `_rgba`, but runtime transcoding and native cache keys use the stored asset value rather than re-reading the filename. Existing assets created before this metadata existed are migrated on load by applying the same filename guess once.

`NativeTargetProfile` controls the native output family. `Default For Current Platform` resolves to Desktop BC Fast Runtime on desktop builds and ASTC 8x8 on iOS/Android builds. It can also be pinned explicitly:

| Profile | Color | Data / Linear | Color With Alpha | Normal Map | Intent |
|---|---|---|---|---|---|
| Desktop BC Fast Runtime | BC1 | BC1, linear sampling | BC3 | BC5 | lower runtime CPU spikes |
| Desktop BC Quality | BC1 | BC1, linear sampling | BC7 | BC7 | higher quality, best with native cache |
| Mobile ASTC 8x8 | ASTC 8x8 | ASTC 8x8, linear sampling | ASTC 8x8 | ASTC 8x8 | ASTC-capable devices |

The async cache API includes `WarmNativeCacheForTexturesAsyncThrottled()` so large projects can cap concurrent transcodes. Start with 1-2 jobs during gameplay/loading and use higher values only during install-finalization or first-launch preparation screens.

Before a release build, call `ValidateRuntimeConfiguration()` on individual Basis assets, `ValidateRuntimeConfigurationsForTextures()` for a batch, or run the `BasisTextureValidation` commandlet to report blocking metadata errors and production warnings such as missing native cache warm-up or source assets that only contain a base mip.

---

## What is XUASTC LDR?

XUASTC LDR is a supercompressed ASTC format introduced in **Basis Universal v2.10**. It applies Weight Grid DCT + RDO supercompression on top of ASTC blocks, achieving bitrates from 0.3 to 5.7 bpp across block sizes 4×4 through 12×12.

Key properties:
- **Explicitly supports normal maps** (documented in official Basis Universal spec)
- **KTX2 container** with `KTX2_SS_XUASTC_LDR` supercompression scheme
- **Transcodes to many GPU formats** at runtime: BC5, BC7, ASTC, ETC2, etc. Source/target compatibility still matters; for example ETC1S does not transcode directly to ASTC 8x8.
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
| BC1_RGB (ETC1S color) | ~23 ms projected |
| BC3_RGBA (ETC1S color + alpha) | ~22 ms projected |
| BC5_RG (XUASTC RRRG normal, R/A channel mapping) | ~150 ms projected |
| BC7_RGBA (XUASTC normal, quality profile) | ~254 ms median, ~397 ms worst-case class |
| ASTC 8×8 (XUASTC 8×8 source, PC measurement) | ~72 ms projected |

These numbers are CPU transcode only, measured on an i7-9700 using the plugin's vendored Basis Universal transcoder in Release mode, then projected from 1920×1080 + full mips to 2048×2048 by pixel count. They do not include file I/O, transient `UTexture2D` creation, RHI upload, or render-thread synchronization.

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
│           │   │   ├── BasisTextureLoader.cpp   # Transcode to BC1/BC3/BC5/BC7/ASTC
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

For the fast desktop BC5 path, tangent-space normal maps must store X in red and Y in alpha before transcoding to `BC5_RG`:

```
basisu <input>.png -ldr_8x8i -normal_map -separate_rg_to_color_alpha -mipmap -output_file <output>.ktx2
```

`-separate_rg_to_color_alpha` is equivalent to `-swizzle rrrg`: red is replicated into RGB and green is stored in alpha. This matches the Basis Universal BC5 convention, where the transcoder pulls BC5's second channel from alpha. Plain RGB normal KTX2 files are rejected for the Desktop BC Fast Runtime profile because their Y channel would be lost during BC5 transcode. Use `Desktop BC Quality` if the asset should remain ordinary RGB/RGBA and output BC7 instead.

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

Optional pre-package validation:

```powershell
UnrealEditor-Cmd.exe BasisDemo.uproject -run=BasisTextureValidation -Path=/Game -FailOnWarnings
```

Optional validation plus native cache warm-up:

```powershell
UnrealEditor-Cmd.exe BasisDemo.uproject -run=BasisTextureValidation -Path=/Game -WarmCache -FailOnWarnings
```

Install-time native-only preparation for a release candidate:

```powershell
UnrealEditor-Cmd.exe BasisDemo.uproject -run=BasisTextureValidation -Path=/Game `
  -ExternalizePayloads=BasisInstallPayloads `
  -SetInstallTimeNativeOnly `
  -WarmCache `
  -DiscardExternalPayloads `
  -FailOnWarnings
```

This writes embedded `.basis` / `.ktx2` bytes to external install payload files, clears the embedded `BasisData` from the asset package, saves modified packages, warms `Saved/BasisNativeCache`, deletes the external payloads after the cache exists, and finally validates the native-only runtime state. In a shipping installer, the same sequence can be split: ship the external payloads in the installer, run warm-up during install finalization or first launch, then discard them.

```powershell
# Basis build (ETC1S albedo + XUASTC 8x8 normals)
.\package_basis_nobuild.ps1

# Standard build (BC1/BC5 + Oodle Texture RDO)
.\package_standard_timed.ps1
```

### Cook-whitelist Binder workflow

When using `ABasisTextureMaterialBinder` for an existing project, do not discover Basis textures by scanning every material asset under a content folder. That can hard-reference converted textures that the original UE cook would never include, inflating the Basis package and invalidating size comparisons.

Use the Standard cook as the source of truth:

1. Package the Standard build first.
2. Generate a cooked package whitelist from the Standard `.utoc` / `.pak`.
3. Export and encode only materials and source textures whose packages are present in that whitelist.
4. Rebuild the Binder from the filtered manifest and prune stale `BasisRuntime` assets before packaging the Basis build.
5. Verify the final package with a Standard-vs-Basis cook delta report; unmatched Basis-only textures should be zero.

The `.bench` helpers used for the VirtualStudio validation follow this flow:

```powershell
python .bench\build_cooked_package_whitelist.py --container <Standard.utoc> --project-name <Project> --output <CookWhitelist.json>

$env:BASIS_VS_COOKED_WHITELIST = '<CookWhitelist.json>'
UnrealEditor-Cmd.exe <StandardProject.uproject> -run=pythonscript -script=.bench\ue_virtualstudio_basis_export.py -unattended -nullrhi

$env:BASIS_VS_PRUNE_UNLISTED = '1'
UnrealEditor-Cmd.exe <BasisProject.uproject> -run=pythonscript -script=.bench\ue_virtualstudio_basis_import_apply.py -unattended -nullrhi

python .bench\report_basis_cook_delta.py --standard-container <Standard.utoc> --basis-container <Basis.utoc> --encoded-manifest <EncodedManifest.json> --project-name <Project> --fail-on-unmatched
```

---

## Plugin Implementation Notes

- `TextureSemantic = Color` assets transcode to **BC1_RGB** under Desktop BC Fast Runtime and are sampled as sRGB.
- `TextureSemantic = Data / Linear` assets transcode to **BC1_RGB** under Desktop BC Fast Runtime and are sampled linearly. Use this for roughness, metallic, ambient occlusion, height, and packed ORM/mask textures.
- `TextureSemantic = Color With Alpha` assets transcode to **BC3_RGBA** under Desktop BC Fast Runtime and **BC7_RGBA** under Desktop BC Quality.
- `TextureSemantic = Normal Map` assets transcode to **BC5_RG** under Desktop BC Fast Runtime and **BC7_RGBA** under Desktop BC Quality.
- The KTX2 normal-map BC5 path accepts XUASTC `RG` data or alpha-channel Y data (`RRRG` from `basisu -normal_map -separate_rg_to_color_alpha`). Plain RGB normal KTX2 files are rejected for Desktop BC Fast Runtime because BC5 would otherwise lose Y. Legacy `.basis` normal maps need alpha-slice Y data for BC5 or should use Desktop BC Quality.
- `NativeTargetProfile = Mobile ASTC 8x8` transcodes compatible XUASTC/ASTC sources to **ASTC_8x8_RGBA** blocks, with sRGB controlled by `TextureSemantic`. ETC1S sources are rejected for ASTC 8x8 because the Basis transcoder does not support that conversion.
- Imported `UBasisTexture` assets store raw `.basis` / `.ktx2` bytes by default. Release builds can externalize those bytes into removable install payload files through the `BasisTextureValidation` commandlet.
- `ABasisTextureMaterialBinder` can be placed in a map to replace material texture parameters at runtime. This lets production builds keep tiny placeholder `Texture2D` references for cooking while the shipped visual textures live as `UBasisTexture` assets and are transcoded when the level starts.
- `TextureSemantic` controls whether an asset is treated as opaque color, linear data, color with alpha, or normal-map data. The importer guesses the initial value from the filename, but production assets should verify it explicitly in the asset details panel.
- `ValidateRuntimeConfiguration()`, `ValidateRuntimeConfigurationsForTextures()`, and the `BasisTextureValidation` commandlet report asset metadata errors and release-readiness warnings that can be surfaced in editor tooling or a pre-package validation step.
- `RuntimeStorageMode` controls whether an asset transcodes from source on load, retains source bytes plus a regenerable native cache, or runs as Install-Time Native Only with no runtime Basis transcode fallback.
- `ExternalBasisPayloadPath` and `SourcePayloadCrc` let the cache key remain stable after external source payloads are deleted.
- `NativeCacheInvalidationKey` can be set by the project or build pipeline to force cache invalidation across patches or compatibility-breaking changes.
- `WarmNativeCache()`, `WarmNativeCacheForTexturesBudgeted()`, `WarmNativeCacheForTexturesAsync()`, `WarmNativeCacheForTexturesAsyncWithProgress()`, `WarmNativeCacheForTexturesAsyncThrottled()`, `ClearNativeCache()`, `HasNativeCache()`, `HasSourcePayload()`, `DiscardExternalSourcePayload()`, and batch warm/clear/discard helpers provide the workflow for first-launch cache population and cache management.
- Native cache files include a cache version, target GPU profile, and per-mip layout, are keyed by source data, `TextureSemantic`, `NativeTargetProfile`, and `NativeCacheInvalidationKey`, are written through a temporary file, and are discarded/regenerated when invalid or stale.
- Passing `-BasisTiming` on the command line writes per-texture runtime transcode timings to `Saved/BasisTimings/basis_transcode_timings.csv`. The CSV separates setup time from the net `transcode_image_level()` time and does not include disk I/O, `UTexture2D` creation, or RHI upload.
- The transcoder uses `basist::ktx2_transcoder`, which handles UASTC+Zstd, XUASTC LDR, and ETC1S natively (`BASISD_SUPPORT_XUASTC=1` by default).
- `PrivatePCHHeaderFile` is set to a plugin-local PCH to avoid loading the 2+ GB shared UE editor PCH on every incremental build.

---

## Background

This demo was developed to evaluate Basis Universal as a practical texture compression pipeline for Unreal Engine 5, with a focus on:

1. **Accurate comparison** — enabling Oodle Texture RDO on the Standard build ensures a fair baseline
2. **XUASTC LDR discovery** — demonstrating the first known UE5 integration of the XUASTC LDR format
3. **Mobile-first potential** — XUASTC 8×8 → ASTC 8×8 transcoding is much cheaper than XUASTC → BC7 on CPU, adding domain-specific supercompression on top of standard ASTC blocks

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

### 3. Cooked Texture Streaming Integration

The runtime transcode path builds a complete mip chain when the source Basis/KTX2 file contains mip levels. Release verification should confirm the packaged runtime behavior against UE5's cooked texture streaming expectations:

- Encode and store all mip levels in the KTX2 container at cook time
- Confirm runtime-created native mip chains behave correctly with materials, LOD, and streaming settings in packaged builds
- Measure initial VRAM usage and texture residency in large scenes

Cooked texture streaming behavior must be validated in the target UE version and RHI before large open-world production rollout.

### 4. Runtime Storage Policies

The current prototype supports three runtime storage policies:

- **Footprint-Optimized**: keep installed assets as Basis/KTX2 and transcode on load.
- **Download-Optimized Native Cache**: keep the source payload, then persist transcoded native GPU blocks under `Saved/BasisNativeCache` after first use.
- **Install-Time Native Only**: externalize the source payload, warm platform-native GPU blocks, delete the external payload, and make runtime cache misses blocking errors.

The runtime storage implementation includes synchronous, budgeted, and throttled worker-thread cache warm-up helpers, per-mip native cache layout, explicit native target profiles, external install payloads, removable source payloads, and project-driven cache invalidation through `NativeCacheInvalidationKey`. Release verification should cover packaged-build cache lifecycle, platform-specific cache policy, patch/update invalidation behavior, and large-library warm-up scheduling.

---

## License

Plugin source code: MIT
Basis Universal transcoder: Apache 2.0 (Binomial LLC)
zstd: BSD (Meta Platforms)
