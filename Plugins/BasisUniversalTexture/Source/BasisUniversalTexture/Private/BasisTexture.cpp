#include "BasisTexture.h"
#include "BasisTextureLoader.h"

#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace
{
    constexpr uint32 NativeCacheMagic = 0x424E5443; // BNTC
    constexpr int32 NativeCacheVersion = 1;
    static const TCHAR* NativeCacheTargetProfile = TEXT("desktop_bc1_bc7");

    FString GetNativeCachePath(const UBasisTexture* Texture)
    {
        const uint32 DataCrc = Texture->BasisData.Num() > 0
            ? FCrc::MemCrc32(Texture->BasisData.GetData(), Texture->BasisData.Num())
            : 0;
        const FString SafeName = FPaths::MakeValidFileName(Texture->GetName());
        return FPaths::ProjectSavedDir() / TEXT("BasisNativeCache")
            / FString::Printf(TEXT("%s_%08x_%s.ubasisnative"), *SafeName, DataCrc, NativeCacheTargetProfile);
    }

    bool SaveNativeCache(
        const FString& CachePath,
        const FBasisTranscodeInfo& Info,
        const TArray<uint8>& NativeBlocks)
    {
        FBufferArchive Ar;

        uint32 Magic = NativeCacheMagic;
        int32 Version = NativeCacheVersion;
        int32 Width = Info.Width;
        int32 Height = Info.Height;
        int32 MipLevels = Info.MipLevels;
        int64 CompressedFileSize = Info.CompressedFileSize;
        int64 TranscodedSize = Info.TranscodedSize;
        float CompressionRatio = Info.CompressionRatio;
        FString SourceFormat = Info.SourceFormat;
        FString TranscodedFormat = Info.TranscodedFormat;
        int32 NativeBlockSize = NativeBlocks.Num();

        Ar << Magic;
        Ar << Version;
        Ar << Width;
        Ar << Height;
        Ar << MipLevels;
        Ar << CompressedFileSize;
        Ar << TranscodedSize;
        Ar << CompressionRatio;
        Ar << SourceFormat;
        Ar << TranscodedFormat;
        Ar << NativeBlockSize;
        Ar.Serialize(const_cast<uint8*>(NativeBlocks.GetData()), NativeBlockSize);

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
        const bool bSaved = FFileHelper::SaveArrayToFile(Ar, *CachePath);
        Ar.FlushCache();
        Ar.Empty();
        return bSaved;
    }

    bool LoadNativeCache(
        const FString& CachePath,
        FBasisTranscodeInfo& OutInfo,
        TArray<uint8>& OutNativeBlocks)
    {
        TArray<uint8> CacheBytes;
        if (!FFileHelper::LoadFileToArray(CacheBytes, *CachePath))
        {
            return false;
        }

        FMemoryReader Ar(CacheBytes, true);

        uint32 Magic = 0;
        int32 Version = 0;
        int32 Width = 0;
        int32 Height = 0;
        int32 MipLevels = 0;
        int64 CompressedFileSize = 0;
        int64 TranscodedSize = 0;
        float CompressionRatio = 0.f;
        FString SourceFormat;
        FString TranscodedFormat;
        int32 NativeBlockSize = 0;

        Ar << Magic;
        Ar << Version;
        Ar << Width;
        Ar << Height;
        Ar << MipLevels;
        Ar << CompressedFileSize;
        Ar << TranscodedSize;
        Ar << CompressionRatio;
        Ar << SourceFormat;
        Ar << TranscodedFormat;
        Ar << NativeBlockSize;

        if (Ar.IsError() || Magic != NativeCacheMagic || Version != NativeCacheVersion)
        {
            return false;
        }
        if (NativeBlockSize <= 0 || Ar.Tell() + NativeBlockSize > Ar.TotalSize())
        {
            return false;
        }
        if (Width <= 0 || Height <= 0 || MipLevels <= 0 || TranscodedSize != NativeBlockSize)
        {
            return false;
        }

        OutNativeBlocks.SetNumUninitialized(NativeBlockSize);
        Ar.Serialize(OutNativeBlocks.GetData(), NativeBlockSize);
        if (Ar.IsError())
        {
            OutNativeBlocks.Reset();
            return false;
        }

        OutInfo = FBasisTranscodeInfo();
        OutInfo.Width = Width;
        OutInfo.Height = Height;
        OutInfo.MipLevels = MipLevels;
        OutInfo.CompressedFileSize = CompressedFileSize;
        OutInfo.TranscodedSize = TranscodedSize;
        OutInfo.CompressionRatio = CompressionRatio;
        OutInfo.SourceFormat = SourceFormat;
        OutInfo.TranscodedFormat = TranscodedFormat;
        return true;
    }

    bool BuildNativeCache(
        const UBasisTexture* Texture,
        const FString& CachePath,
        FBasisTranscodeInfo& OutInfo,
        TArray<uint8>& OutNativeBlocks,
        bool& bOutCacheSaved)
    {
        bOutCacheSaved = false;

        if (!UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
                Texture->BasisData, Texture->GetName(), OutInfo, OutNativeBlocks))
        {
            return false;
        }

        if (SaveNativeCache(CachePath, OutInfo, OutNativeBlocks))
        {
            UE_LOG(LogTemp, Log, TEXT("UBasisTexture: native cache saved: %s"), *CachePath);
            bOutCacheSaved = true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UBasisTexture: failed to save native cache: %s"), *CachePath);
        }

        return true;
    }
}

UTexture2D* UBasisTexture::Transcode()
{
    if (BasisData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: no data"));
        return nullptr;
    }

    FBasisTranscodeInfo Info;
    if (RuntimeStorageMode == EBasisRuntimeStorageMode::FootprintOptimized)
    {
        return UBasisTextureLoader::LoadBasisTextureFromMemory(BasisData, GetName(), Info);
    }

    TArray<uint8> NativeBlocks;
    const FString CachePath = GetNativeCachePath(this);
    if (LoadNativeCache(CachePath, Info, NativeBlocks))
    {
        UE_LOG(LogTemp, Log, TEXT("UBasisTexture::Transcode: native cache hit: %s"), *CachePath);
        if (UTexture2D* CachedTexture =
                UBasisTextureLoader::CreateTextureFromNativeBlocks(NativeBlocks, Info, GetName()))
        {
            return CachedTexture;
        }

        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: native cache invalid, regenerating: %s"), *CachePath);
        IFileManager::Get().Delete(*CachePath);
        NativeBlocks.Reset();
        Info = FBasisTranscodeInfo();
    }

    bool bCacheSaved = false;
    if (!BuildNativeCache(this, CachePath, Info, NativeBlocks, bCacheSaved))
    {
        return nullptr;
    }

    return UBasisTextureLoader::CreateTextureFromNativeBlocks(NativeBlocks, Info, GetName());
}

bool UBasisTexture::WarmNativeCache()
{
    if (BasisData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::WarmNativeCache: no data"));
        return false;
    }

    FBasisTranscodeInfo Info;
    TArray<uint8> NativeBlocks;
    const FString CachePath = GetNativeCachePath(this);
    if (LoadNativeCache(CachePath, Info, NativeBlocks))
    {
        return true;
    }
    if (FPaths::FileExists(CachePath))
    {
        IFileManager::Get().Delete(*CachePath);
    }

    bool bCacheSaved = false;
    if (!BuildNativeCache(this, CachePath, Info, NativeBlocks, bCacheSaved))
    {
        return false;
    }
    return bCacheSaved;
}

bool UBasisTexture::ClearNativeCache()
{
    const FString CachePath = GetNativeCachePath(this);
    if (!FPaths::FileExists(CachePath))
    {
        return true;
    }

    return IFileManager::Get().Delete(*CachePath);
}

bool UBasisTexture::HasNativeCache() const
{
    return FPaths::FileExists(GetNativeCachePath(this));
}

int64 UBasisTexture::GetNativeCacheSizeBytes() const
{
    const FFileStatData StatData = IFileManager::Get().GetStatData(*GetNativeCachePath(this));
    return StatData.bIsValid ? StatData.FileSize : 0;
}

void UBasisTexture::WarmNativeCacheForTextures(
    const TArray<UBasisTexture*>& Textures,
    int32& OutSucceeded,
    int32& OutFailed,
    int64& OutCacheSizeBytes)
{
    OutSucceeded = 0;
    OutFailed = 0;
    OutCacheSizeBytes = 0;

    for (UBasisTexture* Texture : Textures)
    {
        if (!Texture)
        {
            ++OutFailed;
            continue;
        }

        if (Texture->WarmNativeCache())
        {
            ++OutSucceeded;
            OutCacheSizeBytes += Texture->GetNativeCacheSizeBytes();
        }
        else
        {
            ++OutFailed;
        }
    }
}

void UBasisTexture::ClearNativeCacheForTextures(
    const TArray<UBasisTexture*>& Textures,
    int32& OutCleared,
    int32& OutFailed)
{
    OutCleared = 0;
    OutFailed = 0;

    for (UBasisTexture* Texture : Textures)
    {
        if (!Texture)
        {
            ++OutFailed;
            continue;
        }

        if (Texture->ClearNativeCache())
        {
            ++OutCleared;
        }
        else
        {
            ++OutFailed;
        }
    }
}
