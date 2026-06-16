#include "BasisTexture.h"
#include "BasisTextureLoader.h"

#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

namespace
{
    constexpr uint32 NativeCacheMagic = 0x424E5443; // BNTC
    constexpr int32 NativeCacheVersion = 4;
    constexpr int32 BasisMetadataCurrentVersion = 6;

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
        const EBasisNativeTargetProfile ResolvedProfile = ResolveNativeTargetProfile(NativeTargetProfile);
        if (ResolvedProfile == EBasisNativeTargetProfile::MobileASTC8x8)
        {
            if (TextureSemantic == EBasisTextureSemantic::NormalMap)
            {
                return TEXT("mobile_astc8x8_normal");
            }
            if (TextureSemantic == EBasisTextureSemantic::Data)
            {
                return TEXT("mobile_astc8x8_data");
            }
            return TextureSemantic == EBasisTextureSemantic::ColorWithAlpha
                ? TEXT("mobile_astc8x8_alpha")
                : TEXT("mobile_astc8x8_color");
        }

        if (ResolvedProfile == EBasisNativeTargetProfile::DesktopBCQuality)
        {
            if (TextureSemantic == EBasisTextureSemantic::NormalMap)
            {
                return TEXT("desktop_bc7_normal");
            }
            if (TextureSemantic == EBasisTextureSemantic::Data)
            {
                return TEXT("desktop_bc1_data");
            }
            return TextureSemantic == EBasisTextureSemantic::ColorWithAlpha
                ? TEXT("desktop_bc7_alpha")
                : TEXT("desktop_bc1_color");
        }

        if (TextureSemantic == EBasisTextureSemantic::NormalMap)
        {
            return TEXT("desktop_bc5_normal");
        }
        if (TextureSemantic == EBasisTextureSemantic::Data)
        {
            return TEXT("desktop_bc1_data");
        }
        return TextureSemantic == EBasisTextureSemantic::ColorWithAlpha
            ? TEXT("desktop_bc3_alpha")
            : TEXT("desktop_bc1_color");
    }

    FString GetExpectedTranscodedFormat(
        EBasisTextureSemantic TextureSemantic,
        EBasisNativeTargetProfile NativeTargetProfile)
    {
        const EBasisNativeTargetProfile ResolvedProfile = ResolveNativeTargetProfile(NativeTargetProfile);
        if (ResolvedProfile == EBasisNativeTargetProfile::MobileASTC8x8)
        {
            return TEXT("ASTC_8x8_RGBA");
        }
        if (ResolvedProfile == EBasisNativeTargetProfile::DesktopBCQuality)
        {
            return (TextureSemantic == EBasisTextureSemantic::Color || TextureSemantic == EBasisTextureSemantic::Data)
                ? TEXT("BC1_RGB")
                : TEXT("BC7_RGBA");
        }
        if (TextureSemantic == EBasisTextureSemantic::NormalMap)
        {
            return TEXT("BC5_RG");
        }
        return TextureSemantic == EBasisTextureSemantic::ColorWithAlpha
            ? TEXT("BC3_RGBA")
            : TEXT("BC1_RGB");
    }

    uint32 GetSourcePayloadCrc(const UBasisTexture* Texture)
    {
        if (Texture->SourcePayloadCrc != 0)
        {
            return static_cast<uint32>(Texture->SourcePayloadCrc);
        }
        if (Texture->BasisData.Num() > 0)
        {
            return FCrc::MemCrc32(Texture->BasisData.GetData(), Texture->BasisData.Num());
        }
        return Texture->ExternalBasisPayloadPath.IsEmpty()
            ? 0
            : FCrc::StrCrc32(*Texture->ExternalBasisPayloadPath);
    }

    FString ResolveExternalBasisPayloadPath(const FString& PayloadPath)
    {
        if (PayloadPath.IsEmpty())
        {
            return FString();
        }
        return FPaths::IsRelative(PayloadPath)
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / PayloadPath)
            : PayloadPath;
    }

    bool LoadBasisPayloadFromPath(const FString& PayloadPath, TArray<uint8>& OutData)
    {
        const FString ResolvedPath = ResolveExternalBasisPayloadPath(PayloadPath);
        return !ResolvedPath.IsEmpty() && FFileHelper::LoadFileToArray(OutData, *ResolvedPath);
    }

    bool LoadBasisPayload(const UBasisTexture* Texture, TArray<uint8>& OutData)
    {
        OutData.Reset();
        if (LoadBasisPayloadFromPath(Texture->ExternalBasisPayloadPath, OutData))
        {
            return true;
        }
        if (Texture->BasisData.Num() > 0)
        {
            OutData = Texture->BasisData;
            return true;
        }
        return false;
    }

    void UpdateSourcePayloadCrcFromData(UBasisTexture* Texture, const TArray<uint8>& SourceData)
    {
        if (Texture && Texture->SourcePayloadCrc == 0 && SourceData.Num() > 0)
        {
            Texture->SourcePayloadCrc = static_cast<int64>(FCrc::MemCrc32(SourceData.GetData(), SourceData.Num()));
        }
    }

    FString BuildNativeCachePath(const UBasisTexture* Texture, const FString& TargetProfile)
    {
        uint32 DataCrc = GetSourcePayloadCrc(Texture);
        if (!Texture->NativeCacheInvalidationKey.IsEmpty())
        {
            DataCrc = FCrc::StrCrc32(*Texture->NativeCacheInvalidationKey, DataCrc);
        }
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
        FString ExternalBasisPayloadPath;
        FString SourceName;
        EBasisTextureSemantic TextureSemantic = EBasisTextureSemantic::Color;
        EBasisNativeTargetProfile NativeTargetProfile = EBasisNativeTargetProfile::DefaultForCurrentPlatform;
        FString CachePath;
        FString TargetProfile;
    };

    struct FNativeCacheWarmupState
    {
        FCriticalSection Mutex;
        int32 NextRequestIndex = 0;
        int32 Processed = 0;
        int32 Succeeded = 0;
        int32 Failed = 0;
        int32 CompletedWorkers = 0;
        int64 CacheSizeBytes = 0;
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

        if (BasisData.Num() == 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("UBasisTexture: no Basis/KTX2 source data available for native cache build: %s"),
                *SourceName);
            return false;
        }

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
        if (BasisMetadataVersion < 4 && TextureSemantic == EBasisTextureSemantic::Color)
        {
            TextureSemantic = UBasisTextureLoader::GuessTextureSemanticFromName(GetName());
        }
        if (BasisMetadataVersion < 5 && SourcePayloadCrc == 0 && BasisData.Num() > 0)
        {
            SourcePayloadCrc = static_cast<int64>(FCrc::MemCrc32(BasisData.GetData(), BasisData.Num()));
        }
        if (BasisMetadataVersion < 6 && TextureSemantic == EBasisTextureSemantic::Color)
        {
            TextureSemantic = UBasisTextureLoader::GuessTextureSemanticFromName(GetName());
        }
        BasisMetadataVersion = BasisMetadataCurrentVersion;
    }
    else if (SourcePayloadCrc == 0 && BasisData.Num() > 0)
    {
        SourcePayloadCrc = static_cast<int64>(FCrc::MemCrc32(BasisData.GetData(), BasisData.Num()));
    }
}

bool UBasisTexture::ValidateRuntimeConfiguration(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
    OutErrors.Reset();
    OutWarnings.Reset();

    const bool bHasEmbeddedPayload = BasisData.Num() > 0;
    const bool bHasExternalPayloadPath = !ExternalBasisPayloadPath.IsEmpty();
    const bool bHasSourcePayload = bHasEmbeddedPayload
        || (bHasExternalPayloadPath && FPaths::FileExists(ResolveExternalBasisPayloadPath(ExternalBasisPayloadPath)));
    const bool bHasNativeCache = HasNativeCache();

    if (!bHasSourcePayload && !bHasNativeCache)
    {
        OutErrors.Add(TEXT("No embedded BasisData, external payload, or native cache is available."));
    }
    if (RuntimeStorageMode == EBasisRuntimeStorageMode::InstallTimeNativeOnly && !bHasNativeCache)
    {
        OutErrors.Add(TEXT("Install-Time Native Only mode requires a warmed native cache before runtime."));
    }
    if (RuntimeStorageMode == EBasisRuntimeStorageMode::InstallTimeNativeOnly
        && SourcePayloadCrc == 0
        && !bHasSourcePayload)
    {
        OutErrors.Add(TEXT("Install-Time Native Only mode without a source payload requires SourcePayloadCrc so native cache lookup remains stable."));
    }
    if (Width <= 0 || Height <= 0)
    {
        OutErrors.Add(TEXT("Texture dimensions are missing or invalid."));
    }
    if (CompressedSize <= 0 && !bHasExternalPayloadPath)
    {
        OutErrors.Add(TEXT("CompressedSize is missing or invalid."));
    }
    else if (bHasEmbeddedPayload && CompressedSize != BasisData.Num())
    {
        OutErrors.Add(TEXT("CompressedSize does not match the stored BasisData byte count."));
    }
    if (SourcePayloadCrc == 0 && bHasExternalPayloadPath && !bHasEmbeddedPayload)
    {
        OutErrors.Add(TEXT("External-only source payload is missing SourcePayloadCrc; native cache lookup would be unstable after source deletion."));
    }
    if (MipLevels <= 0)
    {
        OutErrors.Add(TEXT("MipLevels is missing or invalid."));
    }
    else if (MipLevels == 1)
    {
        OutWarnings.Add(TEXT("Only the base mip is available; UE texture streaming integration is not enabled."));
    }

    const EBasisNativeTargetProfile ResolvedProfile = ResolveNativeTargetProfile(NativeTargetProfile);
    const FString ExpectedFormat = GetExpectedTranscodedFormat(TextureSemantic, NativeTargetProfile);
    if (ResolvedProfile == EBasisNativeTargetProfile::MobileASTC8x8 && SourceFormat.Contains(TEXT("ETC1S")))
    {
        OutErrors.Add(TEXT("Mobile ASTC 8x8 target cannot transcode ETC1S sources to ASTC 8x8. Re-encode as XUASTC/ASTC 8x8 or use a compatible mobile fallback profile."));
    }
    if (ResolvedProfile == EBasisNativeTargetProfile::DesktopBC
        && TextureSemantic == EBasisTextureSemantic::NormalMap
        && SourceFormat.Contains(TEXT(".basis")))
    {
        OutErrors.Add(TEXT("Desktop BC fast normal maps output BC5_RG. Legacy .basis normals require alpha-slice Y data; use KTX2/XUASTC RG normals or Desktop BC Quality."));
    }
    if (!TranscodedFormat.IsEmpty() && TranscodedFormat != ExpectedFormat)
    {
        OutWarnings.Add(FString::Printf(
            TEXT("Stored TranscodedFormat is '%s' but TextureSemantic currently expects '%s'. Reimport or re-save this asset."),
            *TranscodedFormat,
            *ExpectedFormat));
    }

    if (RuntimeStorageMode == EBasisRuntimeStorageMode::DownloadOptimizedNativeCache && !bHasNativeCache)
    {
        OutWarnings.Add(TEXT("Download-Optimized Native Cache mode is enabled, but the native cache has not been warmed yet."));
    }
    if (RuntimeStorageMode == EBasisRuntimeStorageMode::DownloadOptimizedNativeCache && !bHasSourcePayload)
    {
        OutErrors.Add(TEXT("Download-Optimized Native Cache mode requires an embedded or external source payload for cache recovery. Use Install-Time Native Only if the source payload is intentionally discarded."));
    }
    if (RuntimeStorageMode == EBasisRuntimeStorageMode::FootprintOptimized && !bHasSourcePayload)
    {
        OutErrors.Add(TEXT("Footprint-Optimized mode requires an embedded or external Basis/KTX2 source payload."));
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
    FBasisTranscodeInfo Info;
    if (RuntimeStorageMode == EBasisRuntimeStorageMode::FootprintOptimized)
    {
        TArray<uint8> SourceData;
        if (!LoadBasisPayload(this, SourceData))
        {
            UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: no Basis/KTX2 payload available: %s"), *GetName());
            return nullptr;
        }
        UpdateSourcePayloadCrcFromData(this, SourceData);
        return UBasisTextureLoader::LoadBasisTextureFromMemory(
            SourceData,
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

        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: native cache invalid: %s"), *CachePath);
        IFileManager::Get().Delete(*CachePath);
        if (RuntimeStorageMode == EBasisRuntimeStorageMode::InstallTimeNativeOnly)
        {
            UE_LOG(LogTemp, Error,
                TEXT("UBasisTexture::Transcode: Install-Time Native Only mode cannot regenerate cache at runtime: %s"),
                *GetName());
            return nullptr;
        }
        NativeBlocks.Reset();
        Info = FBasisTranscodeInfo();
    }

    if (RuntimeStorageMode == EBasisRuntimeStorageMode::InstallTimeNativeOnly)
    {
        UE_LOG(LogTemp, Error,
            TEXT("UBasisTexture::Transcode: native cache miss in Install-Time Native Only mode: %s"),
            *GetName());
        return nullptr;
    }

    TArray<uint8> SourceData;
    if (!LoadBasisPayload(this, SourceData))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UBasisTexture::Transcode: no Basis/KTX2 payload available to generate native cache: %s"),
            *GetName());
        return nullptr;
    }

    if (SourcePayloadCrc == 0)
    {
        UpdateSourcePayloadCrcFromData(this, SourceData);
        const FString UpdatedCachePath = GetNativeCachePath(this);
        if (UpdatedCachePath != CachePath)
        {
            if (LoadNativeCache(UpdatedCachePath, TargetProfile, Info, NativeBlocks))
            {
                UE_LOG(LogTemp, Log, TEXT("UBasisTexture::Transcode: native cache hit after source CRC update: %s"), *UpdatedCachePath);
                return UBasisTextureLoader::CreateTextureFromNativeBlocks(
                    NativeBlocks,
                    Info,
                    GetName(),
                    TextureSemantic,
                    NativeTargetProfile);
            }
        }
    }

    const FString BuildCachePath = GetNativeCachePath(this);
    if (FPaths::FileExists(BuildCachePath))
    {
        IFileManager::Get().Delete(*BuildCachePath);
    }

    bool bCacheSaved = false;
    if (!BuildNativeCacheFromData(
            SourceData,
            GetName(),
            TextureSemantic,
            NativeTargetProfile,
            BuildCachePath,
            TargetProfile,
            Info,
            NativeBlocks,
            bCacheSaved))
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
    TArray<uint8> SourceData;
    if (!LoadBasisPayload(this, SourceData))
    {
        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::WarmNativeCache: no Basis/KTX2 payload available: %s"), *GetName());
        return false;
    }
    UpdateSourcePayloadCrcFromData(this, SourceData);

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
    if (!BuildNativeCacheFromData(
            SourceData,
            GetName(),
            TextureSemantic,
            NativeTargetProfile,
            CachePath,
            TargetProfile,
            Info,
            NativeBlocks,
            bCacheSaved))
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

bool UBasisTexture::HasSourcePayload() const
{
    return BasisData.Num() > 0
        || (!ExternalBasisPayloadPath.IsEmpty()
            && FPaths::FileExists(ResolveExternalBasisPayloadPath(ExternalBasisPayloadPath)));
}

bool UBasisTexture::DiscardExternalSourcePayload()
{
    if (ExternalBasisPayloadPath.IsEmpty())
    {
        return true;
    }

    if (!HasNativeCache())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UBasisTexture::DiscardExternalSourcePayload: refusing to delete source payload before native cache exists: %s"),
            *GetName());
        return false;
    }

    const FString ResolvedPath = ResolveExternalBasisPayloadPath(ExternalBasisPayloadPath);
    if (ResolvedPath.IsEmpty() || !FPaths::FileExists(ResolvedPath))
    {
        return true;
    }

    if (!IFileManager::Get().Delete(*ResolvedPath, false, true))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UBasisTexture::DiscardExternalSourcePayload: failed to delete %s"),
            *ResolvedPath);
        return false;
    }

    UE_LOG(LogTemp, Log,
        TEXT("UBasisTexture::DiscardExternalSourcePayload: deleted %s"),
        *ResolvedPath);
    return true;
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
    WarmNativeCacheForTexturesAsyncWithProgress(
        Textures,
        FBasisNativeCacheWarmupProgress(),
        OnComplete);
}

void UBasisTexture::WarmNativeCacheForTexturesAsyncWithProgress(
    const TArray<UBasisTexture*>& Textures,
    FBasisNativeCacheWarmupProgress OnProgress,
    FBasisNativeCacheWarmupComplete OnComplete)
{
    WarmNativeCacheForTexturesAsyncThrottled(Textures, 1, OnProgress, OnComplete);
}

void UBasisTexture::WarmNativeCacheForTexturesAsyncThrottled(
    const TArray<UBasisTexture*>& Textures,
    int32 MaxConcurrentTranscodes,
    FBasisNativeCacheWarmupProgress OnProgress,
    FBasisNativeCacheWarmupComplete OnComplete)
{
    TArray<FNativeCacheBuildRequest> Requests;
    int32 InitialFailed = 0;

    for (const UBasisTexture* Texture : Textures)
    {
        if (!Texture || (Texture->BasisData.Num() == 0 && Texture->ExternalBasisPayloadPath.IsEmpty()))
        {
            ++InitialFailed;
            continue;
        }

        FNativeCacheBuildRequest Request;
        Request.BasisData = Texture->BasisData;
        Request.ExternalBasisPayloadPath = Texture->ExternalBasisPayloadPath;
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
            [OnProgress, OnComplete, InitialFailed]() mutable
            {
                OnProgress.ExecuteIfBound(InitialFailed, InitialFailed, 0, InitialFailed);
                OnComplete.ExecuteIfBound(0, InitialFailed, 0);
            });
        return;
    }

    const int32 WorkerCount = FMath::Clamp(MaxConcurrentTranscodes, 1, Requests.Num());
    TSharedRef<TArray<FNativeCacheBuildRequest>, ESPMode::ThreadSafe> SharedRequests =
        MakeShared<TArray<FNativeCacheBuildRequest>, ESPMode::ThreadSafe>(MoveTemp(Requests));
    TSharedRef<FNativeCacheWarmupState, ESPMode::ThreadSafe> State =
        MakeShared<FNativeCacheWarmupState, ESPMode::ThreadSafe>();
    State->Processed = InitialFailed;
    State->Failed = InitialFailed;

    const int32 Total = SharedRequests->Num() + InitialFailed;
    for (int32 WorkerIndex = 0; WorkerIndex < WorkerCount; ++WorkerIndex)
    {
        Async(EAsyncExecution::ThreadPool,
            [SharedRequests, State, Total, WorkerCount, OnProgress, OnComplete]() mutable
            {
                while (true)
                {
                    int32 RequestIndex = INDEX_NONE;
                    {
                        FScopeLock Lock(&State->Mutex);
                        if (State->NextRequestIndex >= SharedRequests->Num())
                        {
                            break;
                        }
                        RequestIndex = State->NextRequestIndex++;
                    }

                    const FNativeCacheBuildRequest& Request = (*SharedRequests)[RequestIndex];
                    bool bSucceeded = false;
                    int64 CacheSizeBytesForRequest = 0;

                    FBasisTranscodeInfo Info;
                    TArray<uint8> NativeBlocks;
                    if (LoadNativeCache(Request.CachePath, Request.TargetProfile, Info, NativeBlocks))
                    {
                        bSucceeded = true;
                        CacheSizeBytesForRequest = GetCacheFileSize(Request.CachePath);
                    }
                    else
                    {
                        if (FPaths::FileExists(Request.CachePath))
                        {
                            IFileManager::Get().Delete(*Request.CachePath);
                        }

                        bool bCacheSaved = false;
                        TArray<uint8> SourceData = Request.BasisData;
                        if (SourceData.Num() == 0)
                        {
                            LoadBasisPayloadFromPath(Request.ExternalBasisPayloadPath, SourceData);
                        }
                        if (BuildNativeCacheFromData(
                                SourceData,
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
                            bSucceeded = true;
                            CacheSizeBytesForRequest = GetCacheFileSize(Request.CachePath);
                        }
                    }

                    int32 Processed = 0;
                    int32 Succeeded = 0;
                    int32 Failed = 0;
                    {
                        FScopeLock Lock(&State->Mutex);
                        if (bSucceeded)
                        {
                            ++State->Succeeded;
                            State->CacheSizeBytes += CacheSizeBytesForRequest;
                        }
                        else
                        {
                            ++State->Failed;
                        }
                        Processed = ++State->Processed;
                        Succeeded = State->Succeeded;
                        Failed = State->Failed;
                    }

                    AsyncTask(ENamedThreads::GameThread,
                        [OnProgress, Processed, Total, Succeeded, Failed]() mutable
                        {
                            OnProgress.ExecuteIfBound(Processed, Total, Succeeded, Failed);
                        });
                }

                int32 Succeeded = 0;
                int32 Failed = 0;
                int64 CacheSizeBytes = 0;
                bool bLastWorker = false;
                {
                    FScopeLock Lock(&State->Mutex);
                    bLastWorker = ++State->CompletedWorkers == WorkerCount;
                    if (bLastWorker)
                    {
                        Succeeded = State->Succeeded;
                        Failed = State->Failed;
                        CacheSizeBytes = State->CacheSizeBytes;
                    }
                }

                if (bLastWorker)
                {
                    AsyncTask(ENamedThreads::GameThread,
                        [OnComplete, Succeeded, Failed, CacheSizeBytes]() mutable
                        {
                            OnComplete.ExecuteIfBound(Succeeded, Failed, CacheSizeBytes);
                        });
                }
            });
    }
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

void UBasisTexture::DiscardExternalSourcePayloadsForTextures(
    const TArray<UBasisTexture*>& Textures,
    int32& OutDiscarded,
    int32& OutFailed)
{
    OutDiscarded = 0;
    OutFailed = 0;

    for (UBasisTexture* Texture : Textures)
    {
        if (!Texture)
        {
            ++OutFailed;
            continue;
        }

        if (Texture->DiscardExternalSourcePayload())
        {
            ++OutDiscarded;
        }
        else
        {
            ++OutFailed;
        }
    }
}
