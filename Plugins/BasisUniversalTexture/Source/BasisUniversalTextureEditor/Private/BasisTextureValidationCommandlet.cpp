#include "BasisTextureValidationCommandlet.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BasisTexture.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

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
    const bool bFailOnWarnings = FParse::Param(*Params, TEXT("FailOnWarnings"));

    FString PackagePath = TEXT("/Game");
    FParse::Value(*Params, TEXT("Path="), PackagePath);

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
    int32 WarmedCount = 0;
    int32 WarmFailedCount = 0;
    int64 CacheSizeBytes = 0;

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
    }

    UE_LOG(LogTemp, Display,
        TEXT("BasisTextureValidation: valid=%d invalid=%d warnings=%d warmed=%d warm_failed=%d cache=%.2f MB"),
        ValidCount,
        InvalidCount,
        WarningCount,
        WarmedCount,
        WarmFailedCount,
        static_cast<double>(CacheSizeBytes) / (1024.0 * 1024.0));

    if (InvalidCount > 0 || WarmFailedCount > 0 || (bFailOnWarnings && WarningCount > 0))
    {
        return 1;
    }
    return 0;
}
