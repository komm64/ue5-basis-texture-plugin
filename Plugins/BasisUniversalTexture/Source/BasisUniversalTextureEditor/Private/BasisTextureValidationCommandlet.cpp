#include "BasisTextureValidationCommandlet.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BasisTexture.h"
#include "FileHelpers.h"
#include "Misc/Crc.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
    bool IsKtx2Payload(const TArray<uint8>& Data)
    {
        if (Data.Num() < 12)
        {
            return false;
        }

        static const uint8 Ktx2Magic[8] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB };
        return FMemory::Memcmp(Data.GetData(), Ktx2Magic, sizeof(Ktx2Magic)) == 0;
    }

    FString ResolvePayloadRoot(const FString& PayloadRoot)
    {
        if (PayloadRoot.IsEmpty())
        {
            return FString();
        }

        return FPaths::IsRelative(PayloadRoot)
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / PayloadRoot)
            : FPaths::ConvertRelativePathToFull(PayloadRoot);
    }

    FString ToProjectRelativePayloadPath(const FString& FullPath)
    {
        FString RelativePath = FullPath;
        if (FPaths::MakePathRelativeTo(RelativePath, *FPaths::ProjectDir()))
        {
            FPaths::NormalizeFilename(RelativePath);
            return RelativePath;
        }
        return FullPath;
    }

    FString BuildExternalPayloadPath(
        const FString& ResolvedPayloadRoot,
        const FAssetData& AssetData,
        const UBasisTexture* Texture)
    {
        FString PackageRelativePath = AssetData.PackageName.ToString();
        PackageRelativePath.RemoveFromStart(TEXT("/"));
        const FString Extension = IsKtx2Payload(Texture->BasisData) ? TEXT(".ktx2") : TEXT(".basis");
        return (ResolvedPayloadRoot / PackageRelativePath) + Extension;
    }
}

UBasisTextureValidationCommandlet::UBasisTextureValidationCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UBasisTextureValidationCommandlet::Main(const FString& Params)
{
    const bool bWarmCache = FParse::Param(*Params, TEXT("WarmCache"));
    const bool bDiscardExternalPayloads = FParse::Param(*Params, TEXT("DiscardExternalPayloads"));
    const bool bSetInstallTimeNativeOnly = FParse::Param(*Params, TEXT("SetInstallTimeNativeOnly"));
    const bool bFailOnWarnings = FParse::Param(*Params, TEXT("FailOnWarnings"));

    FString PackagePath = TEXT("/Game");
    FParse::Value(*Params, TEXT("Path="), PackagePath);

    FString ExternalPayloadRoot;
    bool bExternalizePayloads = FParse::Value(*Params, TEXT("ExternalizePayloads="), ExternalPayloadRoot);
    if (!bExternalizePayloads && FParse::Param(*Params, TEXT("ExternalizePayloads")))
    {
        ExternalPayloadRoot = TEXT("BasisInstallPayloads");
        bExternalizePayloads = true;
    }

    const FString ResolvedExternalPayloadRoot = ResolvePayloadRoot(ExternalPayloadRoot);
    if (bExternalizePayloads && ResolvedExternalPayloadRoot.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("BasisTextureValidation: -ExternalizePayloads requires a payload root."));
        return 1;
    }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
    AssetRegistry.SearchAllAssets(true);

    FARFilter Filter;
    Filter.PackagePaths.Add(*PackagePath);
    Filter.ClassPaths.Add(UBasisTexture::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> AssetDataList;
    AssetRegistry.GetAssets(Filter, AssetDataList);

    UE_LOG(LogTemp, Display,
        TEXT("BasisTextureValidation: scanning %d Basis textures under %s"),
        AssetDataList.Num(),
        *PackagePath);

    int32 ValidCount = 0;
    int32 InvalidCount = 0;
    int32 WarningCount = 0;
    int32 ExternalizedCount = 0;
    int32 ExternalizeFailedCount = 0;
    int32 WarmedCount = 0;
    int32 WarmFailedCount = 0;
    int32 DiscardedCount = 0;
    int32 DiscardFailedCount = 0;
    int64 CacheSizeBytes = 0;
    TArray<UPackage*> PackagesToSave;

    for (const FAssetData& AssetData : AssetDataList)
    {
        UBasisTexture* Texture = Cast<UBasisTexture>(AssetData.GetAsset());
        if (!Texture)
        {
            ++InvalidCount;
            UE_LOG(LogTemp, Error,
                TEXT("BasisTextureValidation: failed to load %s"),
                *AssetData.GetObjectPathString());
            continue;
        }

        bool bModified = false;
        if (bExternalizePayloads)
        {
            if (Texture->BasisData.Num() > 0)
            {
                const FString FullPayloadPath =
                    BuildExternalPayloadPath(ResolvedExternalPayloadRoot, AssetData, Texture);
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPayloadPath), true);

                if (FFileHelper::SaveArrayToFile(Texture->BasisData, *FullPayloadPath))
                {
                    Texture->Modify();
                    Texture->SourcePayloadCrc =
                        static_cast<int64>(FCrc::MemCrc32(Texture->BasisData.GetData(), Texture->BasisData.Num()));
                    Texture->CompressedSize = Texture->BasisData.Num();
                    Texture->ExternalBasisPayloadPath = ToProjectRelativePayloadPath(FullPayloadPath);
                    Texture->BasisData.Reset();
                    Texture->MarkPackageDirty();
                    bModified = true;
                    ++ExternalizedCount;
                    UE_LOG(LogTemp, Display,
                        TEXT("%s: externalized Basis payload to %s"),
                        *Texture->GetPathName(),
                        *Texture->ExternalBasisPayloadPath);
                }
                else
                {
                    ++ExternalizeFailedCount;
                    UE_LOG(LogTemp, Error,
                        TEXT("%s: failed to externalize Basis payload to %s"),
                        *Texture->GetPathName(),
                        *FullPayloadPath);
                }
            }
            else if (!Texture->ExternalBasisPayloadPath.IsEmpty())
            {
                ++ExternalizedCount;
            }
            else
            {
                ++ExternalizeFailedCount;
                UE_LOG(LogTemp, Error,
                    TEXT("%s: no embedded BasisData available to externalize"),
                    *Texture->GetPathName());
            }
        }

        if (bSetInstallTimeNativeOnly
            && Texture->RuntimeStorageMode != EBasisRuntimeStorageMode::InstallTimeNativeOnly)
        {
            Texture->Modify();
            Texture->RuntimeStorageMode = EBasisRuntimeStorageMode::InstallTimeNativeOnly;
            Texture->MarkPackageDirty();
            bModified = true;
        }

        if (bWarmCache)
        {
            if (Texture->WarmNativeCache())
            {
                ++WarmedCount;
                CacheSizeBytes += Texture->GetNativeCacheSizeBytes();
            }
            else
            {
                ++WarmFailedCount;
                UE_LOG(LogTemp, Error,
                    TEXT("%s: failed to warm native cache"),
                    *Texture->GetPathName());
            }
        }

        if (bDiscardExternalPayloads)
        {
            if (Texture->DiscardExternalSourcePayload())
            {
                ++DiscardedCount;
            }
            else
            {
                ++DiscardFailedCount;
                UE_LOG(LogTemp, Error,
                    TEXT("%s: failed to discard external source payload"),
                    *Texture->GetPathName());
            }
        }

        TArray<FString> Errors;
        TArray<FString> Warnings;
        const bool bValid = Texture->ValidateRuntimeConfiguration(Errors, Warnings);
        if (bValid)
        {
            ++ValidCount;
        }
        else
        {
            ++InvalidCount;
        }

        for (const FString& Error : Errors)
        {
            UE_LOG(LogTemp, Error, TEXT("%s: %s"), *Texture->GetPathName(), *Error);
        }
        for (const FString& Warning : Warnings)
        {
            ++WarningCount;
            UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *Texture->GetPathName(), *Warning);
        }

        if (bModified)
        {
            PackagesToSave.AddUnique(Texture->GetOutermost());
        }
    }

    bool bSavedPackages = true;
    if (PackagesToSave.Num() > 0)
    {
        bSavedPackages = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
    }

    UE_LOG(LogTemp, Display,
        TEXT("BasisTextureValidation: valid=%d invalid=%d warnings=%d externalized=%d externalize_failed=%d warmed=%d warm_failed=%d discarded=%d discard_failed=%d saved_packages=%d cache=%.2f MB"),
        ValidCount,
        InvalidCount,
        WarningCount,
        ExternalizedCount,
        ExternalizeFailedCount,
        WarmedCount,
        WarmFailedCount,
        DiscardedCount,
        DiscardFailedCount,
        bSavedPackages ? PackagesToSave.Num() : -1,
        static_cast<double>(CacheSizeBytes) / (1024.0 * 1024.0));

    if (InvalidCount > 0
        || ExternalizeFailedCount > 0
        || WarmFailedCount > 0
        || DiscardFailedCount > 0
        || !bSavedPackages
        || (bFailOnWarnings && WarningCount > 0))
    {
        return 1;
    }
    return 0;
}
