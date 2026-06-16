#include "BasisTexture.h"
#include "BasisTextureLoader.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace
{
    constexpr uint32 NativeCacheMagic = 0x424E5443; // BNTC
    constexpr int32 NativeCacheVersion = 3;
    constexpr int32 BasisMetadataCurrentVersion = 3;

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

    FString GetNativeCacheTargetProfile(
        EBasisTextureSemantic TextureSemantic,
        EBasisNativeTargetProfile NativeTargetProfile)
    {
        if (ResolveNativeTargetProfile(NativeTargetProfile) == EBasisNativeTargetProfile::MobileASTC8x8)
        {
            return TextureSemantic == EBasisTextureSemantic::NormalMap
                ? TEXT("mobile_astc8x8_normal")
                : TEXT("mobile_astc8x8_color");
        }

        return TextureSemantic == EBasisTextureSemantic::NormalMap
            ? TEXT("desktop_bc7_normal")
            : TEXT("desktop_bc1_color");
    }

    FString BuildNativeCachePath(const UBasisTexture* Texture, const FString& TargetProfile)
    {
        const uint32 DataCrc = Texture->BasisData.Num() > 0
            ? FCrc::MemCrc32(Texture->BasisData.GetData(), Texture->BasisData.Num())
            : 0;
        const FString SafeName = FPaths::MakeValidFileName(Texture->GetName());
        return FPaths::ProjectSavedDir() / TEXT("BasisNativeCache")
            / FString::Printf(TEXT("%s_%08x_%s.ubasisnative"), *SafeName, DataCrc, *TargetProfile);
    }

    FString GetNativeCachePath(const UBasisTexture* Texture)
    {
        return BuildNativeCachePath(
            Texture,
            GetNativeCacheTargetProfile(Texture->TextureSemantic, Texture->NativeTargetProfile));
    }

    FString GetLegacyNativeCachePath(const UBasisTexture* Texture)
    {
        return BuildNativeCachePath(Texture, TEXT("desktop_bc1_bc7"));
    }

    struct FNativeCacheBuildRequest
    {
        TArray<uint8> BasisData;
        FString SourceName;
        EBasisTextureSemantic TextureSemantic = EBasisTextureSemantic::Color;
        EBasisNativeTargetProfile NativeTargetProfile = EBasisNativeTargetProfile::DefaultForCurrentPlatform;
        FString CachePath;
        FString TargetProfile;
    };

    bool SaveNativeCache(
        const FString& CachePath,
        const FString& TargetProfile,
        const FBasisTranscodeInfo& Info,
        const TArray<uint8>& NativeBlocks)
    {
        FBufferArchive Ar;

        uint32 Magic = NativeCacheMagic;
        int32 Version = NativeCacheVersion;
        FString CachedTargetProfile = TargetProfile;
        int32 Width = Info.Width;
        int32 Height = Info.Height;
        int32 MipLevels = Info.MipLevels;
        int64 CompressedFileSize = Info.CompressedFileSize;
        int64 TranscodedSize = Info.TranscodedSize;
        float CompressionRatio = Info.CompressionRatio;
        FString SourceFormat = Info.SourceFormat;
        FString TranscodedFormat = Info.TranscodedFormat;
        int32 NativeBlockSize = NativeBlocks.Num();
        int32 NativeMipCount = Info.NativeMips.Num();

        Ar << Magic;
        Ar << Version;
        Ar << CachedTargetProfile;
        Ar << Width;
        Ar << Height;
        Ar << MipLevels;
        Ar << CompressedFileSize;
        Ar << TranscodedSize;
        Ar << CompressionRatio;
        Ar << SourceFormat;
        Ar << TranscodedFormat;
        Ar << NativeMipCount;
        for (const FBasisNativeMipInfo& MipInfo : Info.NativeMips)
        {
            int32 MipWidth = MipInfo.Width;
            int32 MipHeight = MipInfo.Height;
            int32 MipOffsetBytes = MipInfo.OffsetBytes;
            int32 MipSizeBytes = MipInfo.SizeBytes;
            Ar << MipWidth;
            Ar << MipHeight;
            Ar << MipOffsetBytes;
            Ar << MipSizeBytes;
        }
        Ar << NativeBlockSize;
        Ar.Serialize(const_cast<uint8*>(NativeBlocks.GetData()), NativeBlockSize);

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
        const FString TempPath = CachePath + TEXT(".tmp");
        IFileManager::Get().Delete(*TempPath);

        const bool bSaved = FFileHelper::SaveArrayToFile(Ar, *TempPath);
        Ar.FlushCache();
        Ar.Empty();
        if (!bSaved)
        {
            IFileManager::Get().Delete(*TempPath);
            return false;
        }

        const bool bMoved = IFileManager::Get().Move(*CachePath, *TempPath, true, true);
        if (!bMoved)
        {
            IFileManager::Get().Delete(*TempPath);
        }
        return bMoved;
    }

    bool LoadNativeCache(
        const FString& CachePath,
        const FString& ExpectedTargetProfile,
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
        FString CachedTargetProfile;
        int32 Width = 0;
        int32 Height = 0;
        int32 MipLevels = 0;
        int64 CompressedFileSize = 0;
        int64 TranscodedSize = 0;
        float CompressionRatio = 0.f;
        FString SourceFormat;
        FString TranscodedFormat;
        int32 NativeMipCount = 0;
        int32 NativeBlockSize = 0;
        TArray<FBasisNativeMipInfo> NativeMips;

        Ar << Magic;
        Ar << Version;
        Ar << CachedTargetProfile;
        Ar << Width;
        Ar << Height;
        Ar << MipLevels;
        Ar << CompressedFileSize;
        Ar << TranscodedSize;
        Ar << CompressionRatio;
        Ar << SourceFormat;
        Ar << TranscodedFormat;
        Ar << NativeMipCount;
        if (NativeMipCount <= 0 || NativeMipCount > 32)
        {
            return false;
        }
        NativeMips.SetNum(NativeMipCount);
        for (FBasisNativeMipInfo& MipInfo : NativeMips)
        {
            Ar << MipInfo.Width;
            Ar << MipInfo.Height;
            Ar << MipInfo.OffsetBytes;
            Ar << MipInfo.SizeBytes;
        }
        Ar << NativeBlockSize;

        if (Ar.IsError()
            || Magic != NativeCacheMagic
            || Version != NativeCacheVersion
            || CachedTargetProfile != ExpectedTargetProfile)
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
        if (NativeMips.Num() != MipLevels)
        {
            return false;
        }
        int32 ExpectedOffset = 0;
        for (const FBasisNativeMipInfo& MipInfo : NativeMips)
        {
            if (MipInfo.Width <= 0
                || MipInfo.Height <= 0
                || MipInfo.OffsetBytes != ExpectedOffset
                || MipInfo.SizeBytes <= 0)
            {
                return false;
            }
            ExpectedOffset += MipInfo.SizeBytes;
        }
        if (ExpectedOffset != NativeBlockSize)
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
        OutInfo.NativeMips = NativeMips;
        return true;
    }

    bool BuildNativeCacheFromData(
        const TArray<uint8>& BasisData,
        const FString& SourceName,
        EBasisTextureSemantic TextureSemantic,
        EBasisNativeTargetProfile NativeTargetProfile,
        const FString& CachePath,
        const FString& TargetProfile,
        FBasisTranscodeInfo& OutInfo,
        TArray<uint8>& OutNativeBlocks,
        bool& bOutCacheSaved)
    {
        bOutCacheSaved = false;

        if (!UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
                BasisData,
                SourceName,
                TextureSemantic,
                NativeTargetProfile,
                OutInfo,
                OutNativeBlocks))
        {
            return false;
        }

        if (SaveNativeCache(CachePath, TargetProfile, OutInfo, OutNativeBlocks))
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

    bool BuildNativeCache(
        const UBasisTexture* Texture,
        const FString& CachePath,
        const FString& TargetProfile,
        FBasisTranscodeInfo& OutInfo,
        TArray<uint8>& OutNativeBlocks,
        bool& bOutCacheSaved)
    {
        return BuildNativeCacheFromData(
            Texture->BasisData,
            Texture->GetName(),
            Texture->TextureSemantic,
            Texture->NativeTargetProfile,
            CachePath,
            TargetProfile,
            OutInfo,
            OutNativeBlocks,
            bOutCacheSaved);
    }

    int64 GetCacheFileSize(const FString& CachePath)
    {
        const FFileStatData StatData = IFileManager::Get().GetStatData(*CachePath);
        return StatData.bIsValid ? StatData.FileSize : 0;
    }
}

void UBasisTexture::PostLoad()
{
    Super::PostLoad();

    if (BasisMetadataVersion < BasisMetadataCurrentVersion)
    {
        if (BasisMetadataVersion < 1)
        {
            TextureSemantic = UBasisTextureLoader::GuessTextureSemanticFromName(GetName());
        }
        if (BasisMetadataVersion < 2 && MipLevels <= 0)
        {
            MipLevels = 1;
        }
        if (BasisMetadataVersion < 3)
        {
            NativeTargetProfile = EBasisNativeTargetProfile::DefaultForCurrentPlatform;
        }
        BasisMetadataVersion = BasisMetadataCurrentVersion;
    }
}

bool UBasisTexture::ValidateRuntimeConfiguration(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
    OutErrors.Reset();
    OutWarnings.Reset();

    if (BasisData.Num() == 0)
    {
        OutErrors.Add(TEXT("BasisData is empty."));
    }
    if (Width <= 0 || Height <= 0)
    {
        OutErrors.Add(TEXT("Texture dimensions are missing or invalid."));
    }
    if (CompressedSize <= 0)
    {
        OutErrors.Add(TEXT("CompressedSize is missing or invalid."));
    }
    else if (CompressedSize != BasisData.Num())
    {
        OutErrors.Add(TEXT("CompressedSize does not match the stored BasisData byte count."));
    }
    if (MipLevels <= 0)
    {
        OutErrors.Add(TEXT("MipLevels is missing or invalid."));
    }
    else if (MipLevels == 1)
    {
        OutWarnings.Add(TEXT("Only the base mip is available; UE texture streaming integration is not enabled."));
    }

    FString ExpectedFormat;
    if (ResolveNativeTargetProfile(NativeTargetProfile) == EBasisNativeTargetProfile::MobileASTC8x8)
    {
        ExpectedFormat = TEXT("ASTC_8x8_RGBA");
    }
    else
    {
        ExpectedFormat = TextureSemantic == EBasisTextureSemantic::NormalMap
            ? TEXT("BC7_RGBA")
            : TEXT("BC1_RGB");
    }
    if (!TranscodedFormat.IsEmpty() && TranscodedFormat != ExpectedFormat)
    {
        OutWarnings.Add(FString::Printf(
            TEXT("Stored TranscodedFormat is '%s' but TextureSemantic currently expects '%s'. Reimport or re-save this asset."),
            *TranscodedFormat,
            *ExpectedFormat));
    }

    if (RuntimeStorageMode == EBasisRuntimeStorageMode::DownloadOptimizedNativeCache && !HasNativeCache())
    {
        OutWarnings.Add(TEXT("Download-Optimized Native Cache mode is enabled, but the native cache has not been warmed yet."));
    }

    return OutErrors.Num() == 0;
}

bool UBasisTexture::ValidateRuntimeConfigurationsForTextures(
    const TArray<UBasisTexture*>& Textures,
    int32& OutValid,
    int32& OutInvalid,
    TArray<FString>& OutMessages)
{
    OutValid = 0;
    OutInvalid = 0;
    OutMessages.Reset();

    for (const UBasisTexture* Texture : Textures)
    {
        if (!Texture)
        {
            ++OutInvalid;
            OutMessages.Add(TEXT("<null>: invalid Basis texture reference."));
            continue;
        }

        TArray<FString> Errors;
        TArray<FString> Warnings;
        const bool bValid = Texture->ValidateRuntimeConfiguration(Errors, Warnings);
        if (bValid)
        {
            ++OutValid;
        }
        else
        {
            ++OutInvalid;
        }

        for (const FString& Error : Errors)
        {
            OutMessages.Add(FString::Printf(TEXT("%s: ERROR: %s"), *Texture->GetName(), *Error));
        }
        for (const FString& Warning : Warnings)
        {
            OutMessages.Add(FString::Printf(TEXT("%s: WARNING: %s"), *Texture->GetName(), *Warning));
        }
    }

    return OutInvalid == 0;
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
        return UBasisTextureLoader::LoadBasisTextureFromMemory(
            BasisData,
            GetName(),
            TextureSemantic,
            NativeTargetProfile,
            Info);
    }

    TArray<uint8> NativeBlocks;
    const FString TargetProfile = GetNativeCacheTargetProfile(TextureSemantic, NativeTargetProfile);
    const FString CachePath = GetNativeCachePath(this);
    if (LoadNativeCache(CachePath, TargetProfile, Info, NativeBlocks))
    {
        UE_LOG(LogTemp, Log, TEXT("UBasisTexture::Transcode: native cache hit: %s"), *CachePath);
        if (UTexture2D* CachedTexture =
                UBasisTextureLoader::CreateTextureFromNativeBlocks(
                    NativeBlocks,
                    Info,
                    GetName(),
                    TextureSemantic,
                    NativeTargetProfile))
        {
            return CachedTexture;
        }

        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: native cache invalid, regenerating: %s"), *CachePath);
        IFileManager::Get().Delete(*CachePath);
        NativeBlocks.Reset();
        Info = FBasisTranscodeInfo();
    }

    bool bCacheSaved = false;
    if (!BuildNativeCache(this, CachePath, TargetProfile, Info, NativeBlocks, bCacheSaved))
    {
        return nullptr;
    }

    return UBasisTextureLoader::CreateTextureFromNativeBlocks(
        NativeBlocks,
        Info,
        GetName(),
        TextureSemantic,
        NativeTargetProfile);
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
    const FString TargetProfile = GetNativeCacheTargetProfile(TextureSemantic, NativeTargetProfile);
    const FString CachePath = GetNativeCachePath(this);
    if (LoadNativeCache(CachePath, TargetProfile, Info, NativeBlocks))
    {
        return true;
    }
    if (FPaths::FileExists(CachePath))
    {
        IFileManager::Get().Delete(*CachePath);
    }

    bool bCacheSaved = false;
    if (!BuildNativeCache(this, CachePath, TargetProfile, Info, NativeBlocks, bCacheSaved))
    {
        return false;
    }
    return bCacheSaved;
}

bool UBasisTexture::ClearNativeCache()
{
    const FString CachePath = GetNativeCachePath(this);
    const FString LegacyCachePath = GetLegacyNativeCachePath(this);
    bool bSucceeded = true;

    if (!FPaths::FileExists(CachePath))
    {
        if (FPaths::FileExists(LegacyCachePath))
        {
            bSucceeded = IFileManager::Get().Delete(*LegacyCachePath) && bSucceeded;
        }
        return bSucceeded;
    }

    bSucceeded = IFileManager::Get().Delete(*CachePath) && bSucceeded;
    if (LegacyCachePath != CachePath && FPaths::FileExists(LegacyCachePath))
    {
        bSucceeded = IFileManager::Get().Delete(*LegacyCachePath) && bSucceeded;
    }
    return bSucceeded;
}

bool UBasisTexture::HasNativeCache() const
{
    return FPaths::FileExists(GetNativeCachePath(this));
}

int64 UBasisTexture::GetNativeCacheSizeBytes() const
{
    return GetCacheFileSize(GetNativeCachePath(this));
}

void UBasisTexture::WarmNativeCacheForTextures(
    const TArray<UBasisTexture*>& Textures,
    int32& OutSucceeded,
    int32& OutFailed,
    int64& OutCacheSizeBytes)
{
    int32 NextIndex = 0;
    WarmNativeCacheForTexturesBudgeted(
        Textures,
        0,
        Textures.Num(),
        NextIndex,
        OutSucceeded,
        OutFailed,
        OutCacheSizeBytes);
}

void UBasisTexture::WarmNativeCacheForTexturesAsync(
    const TArray<UBasisTexture*>& Textures,
    FBasisNativeCacheWarmupComplete OnComplete)
{
    TArray<FNativeCacheBuildRequest> Requests;
    int32 InitialFailed = 0;

    for (const UBasisTexture* Texture : Textures)
    {
        if (!Texture || Texture->BasisData.Num() == 0)
        {
            ++InitialFailed;
            continue;
        }

        FNativeCacheBuildRequest Request;
        Request.BasisData = Texture->BasisData;
        Request.SourceName = Texture->GetName();
        Request.TextureSemantic = Texture->TextureSemantic;
        Request.NativeTargetProfile = Texture->NativeTargetProfile;
        Request.CachePath = GetNativeCachePath(Texture);
        Request.TargetProfile = GetNativeCacheTargetProfile(Texture->TextureSemantic, Texture->NativeTargetProfile);
        Requests.Add(MoveTemp(Request));
    }

    if (Requests.Num() == 0)
    {
        AsyncTask(ENamedThreads::GameThread,
            [OnComplete, InitialFailed]() mutable
            {
                OnComplete.ExecuteIfBound(0, InitialFailed, 0);
            });
        return;
    }

    Async(EAsyncExecution::ThreadPool,
        [Requests = MoveTemp(Requests), InitialFailed, OnComplete]() mutable
        {
            int32 Succeeded = 0;
            int32 Failed = InitialFailed;
            int64 CacheSizeBytes = 0;

            for (const FNativeCacheBuildRequest& Request : Requests)
            {
                FBasisTranscodeInfo Info;
                TArray<uint8> NativeBlocks;
                if (LoadNativeCache(Request.CachePath, Request.TargetProfile, Info, NativeBlocks))
                {
                    ++Succeeded;
                    CacheSizeBytes += GetCacheFileSize(Request.CachePath);
                    continue;
                }
                if (FPaths::FileExists(Request.CachePath))
                {
                    IFileManager::Get().Delete(*Request.CachePath);
                }

                bool bCacheSaved = false;
                if (BuildNativeCacheFromData(
                        Request.BasisData,
                        Request.SourceName,
                        Request.TextureSemantic,
                        Request.NativeTargetProfile,
                        Request.CachePath,
                        Request.TargetProfile,
                        Info,
                        NativeBlocks,
                        bCacheSaved)
                    && bCacheSaved)
                {
                    ++Succeeded;
                    CacheSizeBytes += GetCacheFileSize(Request.CachePath);
                }
                else
                {
                    ++Failed;
                }
            }

            AsyncTask(ENamedThreads::GameThread,
                [OnComplete, Succeeded, Failed, CacheSizeBytes]() mutable
                {
                    OnComplete.ExecuteIfBound(Succeeded, Failed, CacheSizeBytes);
                });
        });
}

bool UBasisTexture::WarmNativeCacheForTexturesBudgeted(
    const TArray<UBasisTexture*>& Textures,
    int32 StartIndex,
    int32 MaxTextures,
    int32& OutNextIndex,
    int32& OutSucceeded,
    int32& OutFailed,
    int64& OutCacheSizeBytes)
{
    OutSucceeded = 0;
    OutFailed = 0;
    OutCacheSizeBytes = 0;

    const int32 SafeStartIndex = FMath::Clamp(StartIndex, 0, Textures.Num());
    const int32 RemainingTextures = Textures.Num() - SafeStartIndex;
    const int32 TextureBudget = FMath::Clamp(MaxTextures, 0, RemainingTextures);
    const int32 EndIndex = SafeStartIndex + TextureBudget;

    for (int32 Index = SafeStartIndex; Index < EndIndex; ++Index)
    {
        UBasisTexture* Texture = Textures[Index];
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

    OutNextIndex = EndIndex;
    return OutNextIndex >= Textures.Num();
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
