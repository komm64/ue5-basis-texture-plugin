#include "BasisTextureLoader.h"

#include "Engine/Texture2D.h"
#include "RenderUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "RHI.h"
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
// Normal maps (filename contains "nor"): transcoded to BC7_RGBA.
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

UTexture2D* UBasisTextureLoader::LoadBasisTextureFromMemory(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
    FBasisTranscodeInfo& OutInfo)
{
    TArray<uint8> NativeBlocks;
    if (!TranscodeBasisTextureToNativeBlocks(SourceData, SourceName, OutInfo, NativeBlocks))
    {
        return nullptr;
    }

    return CreateTextureFromNativeBlocks(NativeBlocks, OutInfo, SourceName);
}

bool UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
    const TArray<uint8>& SourceData,
    const FString& SourceName,
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
    const bool bIsNormalMap = SourceName.Contains(TEXT("_nor_"), ESearchCase::IgnoreCase);

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

        Width  = KTrans.get_width();
        Height = KTrans.get_height();

        OutInfo.Width       = static_cast<int32>(Width);
        OutInfo.Height      = static_cast<int32>(Height);
        OutInfo.MipLevels   = 1;
        {
            const basist::basis_tex_format Fmt = KTrans.get_basis_tex_format();
            const char* FmtName = basist::basis_get_tex_format_name(Fmt);
            OutInfo.SourceFormat = FString::Printf(TEXT("%hs (.ktx2)"), FmtName);
        }

        if (bIsNormalMap)
        {
            // BC5 is the natural desktop target for normal maps, but this UE5.7
            // transient texture path rendered incorrectly with PF_BC5. Use BC7
            // here as a demo workaround until the cooked texture pipeline path
            // can produce platform-native normal maps directly.
            // BC7_RGBA: 16 bytes per 4x4 block, all channels preserved.
            OutInfo.TranscodedFormat = TEXT("BC7_RGBA");
            const uint32 BlocksX   = (Width  + 3) / 4;
            const uint32 BlocksY   = (Height + 3) / 4;
            const uint32 NumBlocks = BlocksX * BlocksY;
            OutNativeBlocks.SetNumUninitialized(NumBlocks * 16);

            if (!KTrans.transcode_image_level(
                    0, 0, 0,
                    OutNativeBlocks.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC7_RGBA))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 BC7 transcode failed"));
                return false;
            }
        }
        else
        {
            // BC1_RGB: 8 bytes per 4x4 block
            OutInfo.TranscodedFormat = TEXT("BC1_RGB");
            const uint32 BlocksX   = (Width  + 3) / 4;
            const uint32 BlocksY   = (Height + 3) / 4;
            const uint32 NumBlocks = BlocksX * BlocksY;
            OutNativeBlocks.SetNumUninitialized(NumBlocks * 8);

            if (!KTrans.transcode_image_level(
                    0, 0, 0,
                    OutNativeBlocks.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC1_RGB))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 BC1 transcode failed"));
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

        OutInfo.Width       = static_cast<int32>(Width);
        OutInfo.Height      = static_cast<int32>(Height);
        OutInfo.MipLevels   = 1;
        OutInfo.SourceFormat = (FileInfo.m_tex_format == basist::basis_tex_format::cUASTC_LDR_4x4)
                               ? TEXT("UASTC (.basis)") : TEXT("ETC1S (.basis)");

        const uint32 BlocksX   = (Width  + 3) / 4;
        const uint32 BlocksY   = (Height + 3) / 4;
        const uint32 NumBlocks = BlocksX * BlocksY;

        if (bIsNormalMap)
        {
            OutInfo.TranscodedFormat = TEXT("BC7_RGBA");
            OutNativeBlocks.SetNumUninitialized(NumBlocks * 16);

            if (!Trans.transcode_image_level(
                    pData, DataSize, 0, 0,
                    OutNativeBlocks.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC7_RGBA))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: BC7 transcode failed"));
                return false;
            }
        }
        else
        {
            OutInfo.TranscodedFormat = TEXT("BC1_RGB");
            OutNativeBlocks.SetNumUninitialized(NumBlocks * 8);

            if (!Trans.transcode_image_level(
                    pData, DataSize, 0, 0,
                    OutNativeBlocks.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC1_RGB))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: BC1 transcode failed"));
                return false;
            }
        }
    }

    // ---- 5. Fill stats -----------------------------------------------
    const int32 BytesPerBlock = bIsNormalMap ? 16 : 8;
    OutInfo.TranscodedSize  = EstimateBlockCompressedSize(Width, Height, 1, BytesPerBlock);
    OutInfo.CompressionRatio = static_cast<float>(OutInfo.TranscodedSize)
                             / static_cast<float>(OutInfo.CompressedFileSize);

    UE_LOG(LogTemp, Log,
        TEXT("BasisTextureLoader: loaded %s [%ux%u] %s -> %s | disk=%lld bytes | gpu=%lld bytes | ratio=%.1fx"),
        *FPaths::GetCleanFilename(SourceName),
        Width, Height,
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

    const bool bIsNormalMap = SourceName.Contains(TEXT("_nor_"), ESearchCase::IgnoreCase);
    EPixelFormat PixelFmt = PF_Unknown;
    if (Info.TranscodedFormat == TEXT("BC7_RGBA"))
    {
        PixelFmt = PF_BC7;
    }
    else if (Info.TranscodedFormat == TEXT("BC1_RGB"))
    {
        PixelFmt = PF_DXT1;
    }

    if (PixelFmt == PF_Unknown)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BasisTextureLoader: unsupported native block format for %s: %s"),
            *SourceName, *Info.TranscodedFormat);
        return nullptr;
    }

    UTexture2D* Texture = UTexture2D::CreateTransient(Info.Width, Info.Height, PixelFmt);
    if (!Texture)
    {
        UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: CreateTransient failed"));
        return nullptr;
    }

    {
        FTexture2DMipMap& Mip0 = Texture->GetPlatformData()->Mips[0];
        void* MipData = Mip0.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, NativeBlocks.GetData(), NativeBlocks.Num());
        Mip0.BulkData.Unlock();
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
    Texture->NeverStream = true;
    Texture->UpdateResource();

    return Texture;
}

int64 UBasisTextureLoader::EstimateBC7Size(int32 Width, int32 Height, int32 MipLevels)
{
    return EstimateBlockCompressedSize(Width, Height, MipLevels, 16);
}
