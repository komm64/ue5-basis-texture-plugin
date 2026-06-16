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
    OutInfo = FBasisTranscodeInfo();

    if (SourceData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: no source data: %s"), *SourceName);
        return nullptr;
    }

    OutInfo.CompressedFileSize = SourceData.Num();

    const void*  pData    = SourceData.GetData();
    const uint32 DataSize = static_cast<uint32>(SourceData.Num());
    const bool bIsNormalMap = SourceName.Contains(TEXT("_nor_"), ESearchCase::IgnoreCase);

    uint32 Width = 0, Height = 0;
    TArray<uint8> Pixels;

    // ---- 2. Detect format and transcode ------------------------------
    if (IsKTX2(pData, DataSize))
    {
        // --- KTX2 path (supports UASTC+Zstd, XUASTC LDR, ETC1S) ---
        basist::ktx2_transcoder KTrans;
        if (!KTrans.init(pData, DataSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 init failed: %s"), *SourceName);
            return nullptr;
        }
        if (!KTrans.start_transcoding())
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 start_transcoding failed"));
            return nullptr;
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
            Pixels.SetNumUninitialized(NumBlocks * 16);

            if (!KTrans.transcode_image_level(
                    0, 0, 0,
                    Pixels.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC7_RGBA))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 BC7 transcode failed"));
                return nullptr;
            }
        }
        else
        {
            // BC1_RGB: 8 bytes per 4x4 block
            OutInfo.TranscodedFormat = TEXT("BC1_RGB");
            const uint32 BlocksX   = (Width  + 3) / 4;
            const uint32 BlocksY   = (Height + 3) / 4;
            const uint32 NumBlocks = BlocksX * BlocksY;
            Pixels.SetNumUninitialized(NumBlocks * 8);

            if (!KTrans.transcode_image_level(
                    0, 0, 0,
                    Pixels.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC1_RGB))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: KTX2 BC1 transcode failed"));
                return nullptr;
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
            return nullptr;
        }

        basist::basisu_file_info FileInfo;
        if (!Trans.get_file_info(pData, DataSize, FileInfo))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: get_file_info failed"));
            return nullptr;
        }
        if (!Trans.start_transcoding(pData, DataSize))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: start_transcoding failed"));
            return nullptr;
        }

        basist::basisu_image_info ImgInfo;
        if (!Trans.get_image_info(pData, DataSize, ImgInfo, 0))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: get_image_info failed"));
            return nullptr;
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
            Pixels.SetNumUninitialized(NumBlocks * 16);

            if (!Trans.transcode_image_level(
                    pData, DataSize, 0, 0,
                    Pixels.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC7_RGBA))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: BC7 transcode failed"));
                return nullptr;
            }
        }
        else
        {
            OutInfo.TranscodedFormat = TEXT("BC1_RGB");
            Pixels.SetNumUninitialized(NumBlocks * 8);

            if (!Trans.transcode_image_level(
                    pData, DataSize, 0, 0,
                    Pixels.GetData(), NumBlocks,
                    basist::transcoder_texture_format::cTFBC1_RGB))
            {
                UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: BC1 transcode failed"));
                return nullptr;
            }
        }
    }

    // ---- 4. Create UTexture2D ----------------------------------------
    // Normal maps use BC7 as a transient-texture workaround; BC5 remains the
    // intended production target once integrated into the UE texture pipeline.
    const EPixelFormat PixelFmt = bIsNormalMap ? PF_BC7 : PF_DXT1;

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PixelFmt);
    if (!Texture)
    {
        UE_LOG(LogTemp, Warning, TEXT("BasisTextureLoader: CreateTransient failed"));
        return nullptr;
    }

    // Write pixel data into mip 0
    {
        FTexture2DMipMap& Mip0 = Texture->GetPlatformData()->Mips[0];
        void* MipData = Mip0.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num());
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

    return Texture;
}

int64 UBasisTextureLoader::EstimateBC7Size(int32 Width, int32 Height, int32 MipLevels)
{
    return EstimateBlockCompressedSize(Width, Height, MipLevels, 16);
}
