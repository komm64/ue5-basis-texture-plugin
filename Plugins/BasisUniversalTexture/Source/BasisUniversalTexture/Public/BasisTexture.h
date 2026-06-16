#pragma once

#include "CoreMinimal.h"
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

    /**
     * Transcode the stored Basis Universal data into a transient GPU-ready UTexture2D.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal")
    UTexture2D* Transcode();

    /** Compression ratio: TranscodedSize / CompressedSize */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Basis Universal")
    float GetCompressionRatio() const
    {
        return CompressedSize > 0
            ? static_cast<float>(TranscodedSize) / static_cast<float>(CompressedSize)
            : 0.f;
    }
};
