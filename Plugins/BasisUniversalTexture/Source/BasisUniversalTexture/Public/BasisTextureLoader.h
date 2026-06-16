#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "BasisTextureLoader.generated.h"

/**
 * Transcoding result info for debug/demo purposes.
 */
USTRUCT(BlueprintType)
struct BASISUNIVERSALTEXTURE_API FBasisTranscodeInfo
{
    GENERATED_BODY()

    /** Compressed file size on disk (bytes) */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int64 CompressedFileSize = 0;

    /** Transcoded GPU texture size in memory (bytes) */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int64 TranscodedSize = 0;

    /** Compression ratio: TranscodedSize / CompressedFileSize */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    float CompressionRatio = 0.f;

    /** Texture width */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 Width = 0;

    /** Texture height */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 Height = 0;

    /** Number of mip levels */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 MipLevels = 0;

    /** Source codec/container string (ETC1S, UASTC, XUASTC, etc.) */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    FString SourceFormat;

    /** Runtime GPU target format string (BC1_RGB, BC7_RGBA, etc.) */
    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    FString TranscodedFormat;
};

/**
 * Blueprint-accessible loader that transcodes .basis / .ktx2 files to UTexture2D at runtime.
 *
 * Usage in Blueprint:
 *   UBasisTextureLoader::LoadBasisTexture(FilePath, Info) -> UTexture2D*
 */
UCLASS(BlueprintType)
class BASISUNIVERSALTEXTURE_API UBasisTextureLoader : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Load a .basis or .ktx2 file from an absolute path and transcode it to a GPU-ready UTexture2D.
     * @param FilePath   Absolute path to the .basis or .ktx2 file.
     * @param OutInfo    Size and format statistics for demo/comparison display.
     * @return           Transient UTexture2D, or nullptr on failure.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal",
              meta = (DisplayName = "Load Basis Texture"))
    static UTexture2D* LoadBasisTexture(const FString& FilePath, FBasisTranscodeInfo& OutInfo);

    /**
     * Estimate how large an equivalent BC7 texture would be for a given resolution.
     * Kept for Blueprint compatibility with the original demo HUD.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal",
              meta = (DisplayName = "Estimate BC7 Size"))
    static int64 EstimateBC7Size(int32 Width, int32 Height, int32 MipLevels);
};
