#pragma once

#include "CoreMinimal.h"
#include "BasisTextureTypes.h"
#include "Engine/Texture2D.h"
#include "BasisTexture.generated.h"

UENUM(BlueprintType)
enum class EBasisRuntimeStorageMode : uint8
{
    /** Keep the installed footprint small; transcode from Basis bytes when loading. */
    FootprintOptimized UMETA(DisplayName = "Footprint Optimized"),

    /** Keep the download small, then persist native GPU blocks after the first transcode. */
    DownloadOptimizedNativeCache UMETA(DisplayName = "Download Optimized Native Cache")
};

/**
 * Asset that stores a Basis Universal compressed texture (.basis or .ktx2 format).
 *
 * At cook time: the raw Basis Universal bytes are packed into the .uasset,
 * much smaller than a cooked GPU-native texture.
 * At runtime: call Transcode() to decode into a GPU-ready UTexture2D.
 */
UCLASS(BlueprintType)
class BASISUNIVERSALTEXTURE_API UBasisTexture : public UObject
{
    GENERATED_BODY()

public:
    /** Raw Basis Universal file bytes, stored directly in the .uasset */
    UPROPERTY()
    TArray<uint8> BasisData;

    /** Width of the base mip */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int32 Width = 0;

    /** Height of the base mip */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int32 Height = 0;

    /** Source codec/container: "ETC1S", "UASTC", "XUASTC", etc. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    FString SourceFormat;

    /** Runtime GPU target format: "BC1_RGB", "BC7_RGBA", etc. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    FString TranscodedFormat;

    /** Compressed size in bytes (= asset size on disk) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int64 CompressedSize = 0;

    /** Runtime GPU texture size in bytes after transcoding */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int64 TranscodedSize = 0;

    /** Runtime storage policy for this asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis Runtime")
    EBasisRuntimeStorageMode RuntimeStorageMode = EBasisRuntimeStorageMode::FootprintOptimized;

    /** Internal version for import/runtime metadata migrations. */
    UPROPERTY()
    int32 BasisMetadataVersion = 0;

    /** Texture usage. Imported assets are guessed from the filename, but production assets should verify this explicitly. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis Runtime")
    EBasisTextureSemantic TextureSemantic = EBasisTextureSemantic::Color;

    virtual void PostLoad() override;

    /**
     * Transcode the stored Basis Universal data into a transient GPU-ready UTexture2D.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal")
    UTexture2D* Transcode();

    /**
     * Generate or refresh this asset's native GPU block cache.
     * This keeps the shipping payload small while avoiding Basis transcode cost on later loads.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal|Native Cache")
    bool WarmNativeCache();

    /** Delete this asset's native GPU block cache, if present. */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal|Native Cache")
    bool ClearNativeCache();

    /** True when this asset already has a native GPU block cache on disk. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Basis Universal|Native Cache")
    bool HasNativeCache() const;

    /** Size of this asset's native GPU block cache on disk, or 0 when absent. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Basis Universal|Native Cache")
    int64 GetNativeCacheSizeBytes() const;

    /** Warm native caches for a batch of Basis textures, useful for first-launch or install-finalization flows. */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal|Native Cache")
    static void WarmNativeCacheForTextures(
        const TArray<UBasisTexture*>& Textures,
        int32& OutSucceeded,
        int32& OutFailed,
        int64& OutCacheSizeBytes);

    /**
     * Warm part of a texture list. Call repeatedly from a loading screen until it returns true.
     * MaxTextures is a per-call budget; values <= 0 perform no work.
     * @return True when all entries have been processed.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal|Native Cache")
    static bool WarmNativeCacheForTexturesBudgeted(
        const TArray<UBasisTexture*>& Textures,
        int32 StartIndex,
        int32 MaxTextures,
        int32& OutNextIndex,
        int32& OutSucceeded,
        int32& OutFailed,
        int64& OutCacheSizeBytes);

    /** Clear native caches for a batch of Basis textures. */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal|Native Cache")
    static void ClearNativeCacheForTextures(
        const TArray<UBasisTexture*>& Textures,
        int32& OutCleared,
        int32& OutFailed);

    /** Compression ratio: TranscodedSize / CompressedSize */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Basis Universal")
    float GetCompressionRatio() const
    {
        return CompressedSize > 0
            ? static_cast<float>(TranscodedSize) / static_cast<float>(CompressedSize)
            : 0.f;
    }
};
