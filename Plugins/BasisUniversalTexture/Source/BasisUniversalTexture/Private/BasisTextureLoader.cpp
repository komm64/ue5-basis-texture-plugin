#include "BasisTextureLoader.h"

#include "Engine/Texture2D.h"
#include "RenderUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "RHI.h"
#include "Templates/Function.h"
#include "TextureResource.h"

THIRD_PARTY_INCLUDES_START
#pragma warning(push)
#pragma warning(disable: 4800 4267 4244 4456 4127 4100 4701 4668)
#include "basisu_transcoder.h"
#pragma warning(pop)
THIRD_PARTY_INCLUDES_END

// ------------------------------------------------------------------
// UBasisTextureLoader implementation
// ------------------------------------------------------------------
// Normal maps: transcoded to BC7_RGBA.
// Albedo/other: transcoded to BC1_RGB.
// ------------------------------------------------------------------

// KTX2 magic: 0xAB 'K' 'T' 'X' ' ' '2' '0' 0xBB
static bool IsKTX2(const void* pData, uint32 DataSize)
{
    if (DataSize < 12) return false;
    static const uint8 KTX2Magic[8] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB };
    return FMemory::Memcmp(pData, KTX2Magic, 8) == 0;
}

static int64 EstimateBlockCompressedSize(int32 Width, int32 Height, int32 MipLevels, int32 BytesPerBlock)
{
    int64 Total = 0;
    for (int32 L = 0; L < MipLevels; ++L)
    {
        const int32 W = FMath::Max(1, Width >> L);
        const int32 H = FMath::Max(1, Height >> L);
        const int32 BlocksX = (W + 3) / 4;
        const int32 BlocksY = (H + 3) / 4;
        Total += static_cast<int64>(BlocksX) * BlocksY * BytesPerBlock;
    }
    return Total;
}

namespace
{
    struct FResolvedNativeTarget
    {
        EBasisNativeTargetProfile Profile = EBasisNativeTargetProfile::DesktopBC;
        basist::transcoder_texture_format TranscoderFormat = basist::transcoder_texture_format::cTFBC1_RGB;
        EPixelFormat PixelFormat = PF_DXT1;
        FString FormatName = TEXT("BC1_RGB");
        int32 BlockWidth = 4;
        int32 BlockHeight = 4;
        int32 BytesPerBlock = 8;
    };

    EBasisNativeTargetProfile ResolveNativeTargetProfile(EBasisNativeTargetProfile TargetProfile)
    {
        if (TargetProfile != EBasisNativeTargetProfile::DefaultForCurrentPlatform)
        {
            return TargetProfile;
        }

#if PLATFORM_ANDROID || PLATFORM_IOS
        return EBasisNativeTargetProfile::MobileASTC8x8;
#else
        return EBasisNativeTargetProfile::DesktopBC;
#endif
    }

    FResolvedNativeTarget ResolveNativeTarget(
        EBasisTextureSemantic TextureSemantic,
        EBasisNativeTargetProfile TargetProfile)
    {
        FResolvedNativeTarget Target;
        Target.Profile = ResolveNativeTargetProfile(TargetProfile);

        if (Target.Profile == EBasisNativeTargetProfile::MobileASTC8x8)
        {
            Target.TranscoderFormat = basist::transcoder_texture_format::cTFASTC_LDR_8x8_RGBA;
            Target.PixelFormat = PF_ASTC_8x8;
            Target.FormatName = TEXT("ASTC_8x8_RGBA");
            Target.BlockWidth = 8;
            Target.BlockHeight = 8;
            Target.BytesPerBlock = 16;
            return Target;
        }

        if (TextureSemantic == EBasisTextureSemantic::NormalMap)
        {
            Target.TranscoderFormat = basist::transcoder_texture_format::cTFBC7_RGBA;
            Target.PixelFormat = PF_BC7;
            Target.FormatName = TEXT("BC7_RGBA");
            Target.BlockWidth = 4;
            Target.BlockHeight = 4;
            Target.BytesPerBlock = 16;
            return Target;
        }

        Target.TranscoderFormat = basist::transcoder_texture_format::cTFBC1_RGB;
        Target.PixelFormat = PF_DXT1;
        Target.FormatName = TEXT("BC1_RGB");
        Target.BlockWidth = 4;
        Target.BlockHeight = 4;
        Target.BytesPerBlock = 8;
        return Target;
    }

    int32 ComputeMipBlockCount(int32 Width, int32 Height, const FResolvedNativeTarget& Target)
    {
        const int32 BlocksX = (Width + Target.BlockWidth - 1) / Target.BlockWidth;
        const int32 BlocksY = (Height + Target.BlockHeight - 1) / Target.BlockHeight;
        return BlocksX * BlocksY;
    }

    bool AppendTranscodedMip(
        TArray<uint8>& OutNativeBlocks,
        FBasisTranscodeInfo& OutInfo,
        int32 MipWidth,
        int32 MipHeight,
        const FResolvedNativeTarget& Target,
        TFunctionRef<bool(void*, uint32)> TranscodeMip)
    {
        const int32 NumBlocks = ComputeMipBlockCount(MipWidth, MipHeight, Target);
        const int32 MipSizeBytes = NumBlocks * Target.BytesPerBlock;
        if (NumBlocks <= 0 || MipSizeBytes <= 0)
        {
            return false;
        }

        FBasisNativeMipInfo MipInfo;
        MipInfo.Width = MipWidth;
        MipInfo.Height = MipHeight;
        MipInfo.OffsetBytes = OutNativeBlocks.Num();
        MipInfo.SizeBytes = MipSizeBytes;

        OutNativeBlocks.SetNumUninitialized(MipInfo.OffsetBytes + MipInfo.SizeBytes);
        if (!TranscodeMip(OutNativeBlocks.GetData() + MipInfo.OffsetBytes, static_cast<uint32>(NumBlocks)))
        {
            OutNativeBlocks.SetNum(MipInfo.OffsetBytes);
            return false;
        }

        OutInfo.NativeMips.Add(MipInfo);
        return true;
    }

    TArray<FBasisNativeMipInfo> GetValidatedMipLayout(const FBasisTranscodeInfo& Info, int32 NativeBlockSize)
    {
        TArray<FBasisNativeMipInfo> Mips = Info.NativeMips;
        if (Mips.Num() == 0 && Info.Width > 0 && Info.Height > 0 && NativeBlockSize == Info.TranscodedSize)
        {
            FBasisNativeMipInfo Mip0;
            Mip0.Width = Info.Width;
            Mip0.Height = Info.Height;
            Mip0.OffsetBytes = 0;
            Mip0.SizeBytes = NativeBlockSize;
            Mips.Add(Mip0);
        }

        int32 ExpectedOffset = 0;
        for (const FBasisNativeMipInfo& Mip : Mips)
        {
            if (Mip.Width <= 0 || Mip.Height <= 0 || Mip.OffsetBytes != ExpectedOffset || Mip.SizeBytes <= 0)
            {
                Mips.Reset();
                return Mips;
            }
            ExpectedOffset += Mip.SizeBytes;
        }

        if (ExpectedOffset != NativeBlockSize)
        {
            Mips.Reset();
        }
        return Mips;
    }
}

UTexture2D* UBasisTextureLoader::LoadBasisTexture(const FString& FilePath, FBasisTranscodeInfo& OutInfo)
{
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: failed to load: %s"), *FilePath);
        return nullptr;
    }

    return LoadBasisTextureFromMemory(FileData, FilePath, OutInfo);
}

EBasisTextureSemantic UBasisTextureLoader::GuessTextureSemanticFromName(const FString& SourceName)
{
    const FString BaseName = FPaths::GetBaseFilename(SourceName).ToLower();
    const bool bLooksLikeNormal =
        BaseName.Contains(TEXT("_nor_"))
        || BaseName.EndsWith(TEXT("_nor"))
        || BaseName.Contains(TEXT("_normal_"))
        || BaseName.EndsWith(TEXT("_normal"))
        || BaseName.Contains(TEXT("_nrm_"))
        || BaseName.EndsWith(TEXT("_nrm"));

    return bLooksLikeNormal
        ? EBasisTextureSemantic::NormalMap
        : EBasisTextureSemantic::Color;
}

UTexture2D* UBasisTextureLoader::LoadBasisTextureFromMemory(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    FBasisTranscodeInfo& OutInfo)
{
    return LoadBasisTextureFromMemory(
        SourceData,
        SourceName,
        GuessTextureSemanticFromName(SourceName),
        OutInfo);
}

UTexture2D* UBasisTextureLoader::LoadBasisTextureFromMemory(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    EBasisTextureSemantic TextureSemantic,
    FBasisTranscodeInfo& OutInfo)
{
    return LoadBasisTextureFromMemory(
        SourceData,
        SourceName,
        TextureSemantic,
        EBasisNativeTargetProfile::DefaultForCurrentPlatform,
        OutInfo);
}

UTexture2D* UBasisTextureLoader::LoadBasisTextureFromMemory(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    EBasisTextureSemantic TextureSemantic,
    EBasisNativeTargetProfile TargetProfile,
    FBasisTranscodeInfo& OutInfo)
{
    TArray<uint8> NativeBlocks;
    if (!TranscodeBasisTextureToNativeBlocks(SourceData, SourceName, TextureSemantic, TargetProfile, OutInfo, NativeBlocks))
    {
        return nullptr;
    }

    return CreateTextureFromNativeBlocks(NativeBlocks, OutInfo, SourceName, TextureSemantic, TargetProfile);
}

bool UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    FBasisTranscodeInfo& OutInfo,
    TArray<uint8>& OutNativeBlocks)
{
    return TranscodeBasisTextureToNativeBlocks(
        SourceData,
        SourceName,
        GuessTextureSemanticFromName(SourceName),
        OutInfo,
        OutNativeBlocks);
}

bool UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    EBasisTextureSemantic TextureSemantic,
    FBasisTranscodeInfo& OutInfo,
    TArray<uint8>& OutNativeBlocks)
{
    return TranscodeBasisTextureToNativeBlocks(
        SourceData,
        SourceName,
        TextureSemantic,
        EBasisNativeTargetProfile::DefaultForCurrentPlatform,
        OutInfo,
        OutNativeBlocks);
}

bool UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    EBasisTextureSemantic TextureSemantic,
    EBasisNativeTargetProfile TargetProfile,
    FBasisTranscodeInfo& OutInfo,
    TArray<uint8>& OutNativeBlocks)
{
    OutInfo = FBasisTranscodeInfo();
    OutNativeBlocks.Reset();

    if (SourceData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: no source data: %s"), *SourceName);
        return false;
    }

    OutInfo.CompressedFileSize = SourceData.Num();

    const void*  pData    = SourceData.GetData();
    const uint32 DataSize = static_cast<uint32>(SourceData.Num());
    const FResolvedNativeTarget Target = ResolveNativeTarget(TextureSemantic, TargetProfile);

    uint32 Width = 0, Height = 0;

    // ---- 2. Detect format and transcode ------------------------------
    if (IsKTX2(pData, DataSize))
    {
        // --- KTX2 path (supports UASTC+Zstd, XUASTC LDR, ETC1S) ---
        basist::ktx2_transcoder KTrans;
        if (!KTrans.init(pData, DataSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 init failed: %s"), *SourceName);
            return false;
        }
        if (!KTrans.start_transcoding())
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 start_transcoding failed"));
            return false;
        }
        if (KTrans.get_faces() != 1 || KTrans.get_layers() > 1)
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: texture arrays and cubemaps are not supported yet: %s"), *SourceName);
            return false;
        }

        Width  = KTrans.get_width();
        Height = KTrans.get_height();

        OutInfo.Width       = static_cast<int32>(Width);
        OutInfo.Height      = static_cast<int32>(Height);
        OutInfo.MipLevels   = static_cast<int32>(KTrans.get_levels());
        {
            const basist::basis_tex_format Fmt = KTrans.get_basis_tex_format();
            const char* FmtName = basist::basis_get_tex_format_name(Fmt);
            OutInfo.SourceFormat = FString::Printf(TEXT("%hs (.ktx2)"), FmtName);
        }
        OutInfo.TranscodedFormat = Target.FormatName;

        for (uint32 LevelIndex = 0; LevelIndex < KTrans.get_levels(); ++LevelIndex)
        {
            basist::ktx2_image_level_info LevelInfo;
            if (!KTrans.get_image_level_info(LevelInfo, LevelIndex, 0, 0))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 mip metadata failed: %s mip=%u"), *SourceName, LevelIndex);
                return false;
            }

            if (!AppendTranscodedMip(
                    OutNativeBlocks,
                    OutInfo,
                    static_cast<int32>(LevelInfo.m_orig_width),
                    static_cast<int32>(LevelInfo.m_orig_height),
                    Target,
                    [&KTrans, LevelIndex, &Target](void* OutputBlocks, uint32 NumBlocks)
                    {
                        return KTrans.transcode_image_level(
                            LevelIndex,
                            0,
                            0,
                            OutputBlocks,
                            NumBlocks,
                            Target.TranscoderFormat);
                    }))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 %s transcode failed: %s mip=%u"),
                    *Target.FormatName, *SourceName, LevelIndex);
                return false;
            }
        }
    }
    else
    {
        // --- Legacy .basis path (ETC1S or UASTC without supercompression) ---
        basist::basisu_transcoder Trans;
        if (!Trans.validate_header(pData, DataSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: invalid .basis header: %s"), *SourceName);
            return false;
        }

        basist::basisu_file_info FileInfo;
        if (!Trans.get_file_info(pData, DataSize, FileInfo))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: get_file_info failed"));
            return false;
        }
        if (!Trans.start_transcoding(pData, DataSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: start_transcoding failed"));
            return false;
        }

        basist::basisu_image_info ImgInfo;
        if (!Trans.get_image_info(pData, DataSize, ImgInfo, 0))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: get_image_info failed"));
            return false;
        }

        Width  = ImgInfo.m_width;
        Height = ImgInfo.m_height;
        const uint32 ReportedMipLevels = Trans.get_total_image_levels(pData, DataSize, 0);
        const uint32 TotalMipLevels = ReportedMipLevels > 0 ? ReportedMipLevels : 1;

        OutInfo.Width       = static_cast<int32>(Width);
        OutInfo.Height      = static_cast<int32>(Height);
        OutInfo.MipLevels   = static_cast<int32>(TotalMipLevels);
        OutInfo.SourceFormat = (FileInfo.m_tex_format == basist::basis_tex_format::cUASTC_LDR_4x4)
                               ? TEXT("UASTC (.basis)") : TEXT("ETC1S (.basis)");
        OutInfo.TranscodedFormat = Target.FormatName;

        for (uint32 LevelIndex = 0; LevelIndex < TotalMipLevels; ++LevelIndex)
        {
            basist::basisu_image_level_info LevelInfo;
            if (!Trans.get_image_level_info(pData, DataSize, LevelInfo, 0, LevelIndex))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: .basis mip metadata failed: %s mip=%u"), *SourceName, LevelIndex);
                return false;
            }

            if (!AppendTranscodedMip(
                    OutNativeBlocks,
                    OutInfo,
                    static_cast<int32>(LevelInfo.m_orig_width),
                    static_cast<int32>(LevelInfo.m_orig_height),
                    Target,
                    [&Trans, pData, DataSize, LevelIndex, &Target](void* OutputBlocks, uint32 NumBlocks)
                    {
                        return Trans.transcode_image_level(
                            pData,
                            DataSize,
                            0,
                            LevelIndex,
                            OutputBlocks,
                            NumBlocks,
                            Target.TranscoderFormat);
                    }))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: .basis %s transcode failed: %s mip=%u"),
                    *Target.FormatName, *SourceName, LevelIndex);
                return false;
            }
        }
    }

    // ---- 5. Fill stats -----------------------------------------------
    OutInfo.MipLevels = OutInfo.NativeMips.Num();
    OutInfo.TranscodedSize = OutNativeBlocks.Num();
    OutInfo.CompressionRatio = static_cast<float>(OutInfo.TranscodedSize)
                             / static_cast<float>(OutInfo.CompressedFileSize);

    UE_LOG(LogTemp, Log,
        TEXT("BasisTextureLoader: loaded %s [%ux%u mips=%d] %s -> %s | disk=%lld bytes | gpu=%lld bytes | ratio=%.1fx"),
        *FPaths::GetCleanFilename(SourceName),
        Width, Height,
        OutInfo.MipLevels,
        *OutInfo.SourceFormat,
        *OutInfo.TranscodedFormat,
        OutInfo.CompressedFileSize,
        OutInfo.TranscodedSize,
        OutInfo.CompressionRatio);

    return true;
}

UTexture2D* UBasisTextureLoader::CreateTextureFromNativeBlocks(
    const TArray<uint8>& NativeBlocks,
    const FBasisTranscodeInfo& Info,
    const FString& SourceName)
{
    return CreateTextureFromNativeBlocks(
        NativeBlocks,
        Info,
        SourceName,
        GuessTextureSemanticFromName(SourceName));
}

UTexture2D* UBasisTextureLoader::CreateTextureFromNativeBlocks(
    const TArray<uint8>& NativeBlocks,
    const FBasisTranscodeInfo& Info,
    const FString& SourceName,
    EBasisTextureSemantic TextureSemantic)
{
    return CreateTextureFromNativeBlocks(
        NativeBlocks,
        Info,
        SourceName,
        TextureSemantic,
        EBasisNativeTargetProfile::DefaultForCurrentPlatform);
}

UTexture2D* UBasisTextureLoader::CreateTextureFromNativeBlocks(
    const TArray<uint8>& NativeBlocks,
    const FBasisTranscodeInfo& Info,
    const FString& SourceName,
    EBasisTextureSemantic TextureSemantic,
    EBasisNativeTargetProfile TargetProfile)
{
    if (Info.Width <= 0 || Info.Height <= 0 || Info.TranscodedSize <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BasisTextureLoader: invalid native texture metadata for %s"),
            *SourceName);
        return nullptr;
    }
    if (NativeBlocks.Num() != Info.TranscodedSize)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BasisTextureLoader: native block size mismatch for %s: got=%d expected=%lld"),
            *SourceName, NativeBlocks.Num(), Info.TranscodedSize);
        return nullptr;
    }

    const FResolvedNativeTarget Target = ResolveNativeTarget(TextureSemantic, TargetProfile);
    if (Info.TranscodedFormat != Target.FormatName)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BasisTextureLoader: native format mismatch for %s: got=%s expected=%s"),
            *SourceName, *Info.TranscodedFormat, *Target.FormatName);
        return nullptr;
    }

    const TArray<FBasisNativeMipInfo> Mips = GetValidatedMipLayout(Info, NativeBlocks.Num());
    if (Mips.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BasisTextureLoader: invalid native mip layout for %s"),
            *SourceName);
        return nullptr;
    }

    const bool bIsNormalMap = TextureSemantic == EBasisTextureSemantic::NormalMap;

    UTexture2D* Texture = UTexture2D::CreateTransient(Info.Width, Info.Height, Target.PixelFormat);
    if (!Texture)
    {
        UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: CreateTransient failed"));
        return nullptr;
    }

    Texture->GetPlatformData()->Mips.Empty();
    for (const FBasisNativeMipInfo& MipInfo : Mips)
    {
        FTexture2DMipMap* Mip = new(Texture->GetPlatformData()->Mips) FTexture2DMipMap();
        Mip->SizeX = MipInfo.Width;
        Mip->SizeY = MipInfo.Height;

        void* MipData = Mip->BulkData.Lock(LOCK_READ_WRITE);
        MipData = Mip->BulkData.Realloc(MipInfo.SizeBytes);
        FMemory::Memcpy(MipData, NativeBlocks.GetData() + MipInfo.OffsetBytes, MipInfo.SizeBytes);
        Mip->BulkData.Unlock();
    }

    if (bIsNormalMap)
    {
        Texture->CompressionSettings = TC_Normalmap;
        Texture->SRGB = false;
    }
    else
    {
        Texture->SRGB = true;
    }
    Texture->NeverStream = Mips.Num() <= 1;
    Texture->UpdateResource();

    return Texture;
}

int64 UBasisTextureLoader::EstimateBC7Size(int32 Width, int32 Height, int32 MipLevels)
{
    return EstimateBlockCompressedSize(Width, Height, MipLevels, 16);
}
