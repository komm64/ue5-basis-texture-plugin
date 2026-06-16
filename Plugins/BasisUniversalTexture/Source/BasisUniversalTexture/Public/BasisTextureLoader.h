#pragma once

#include "CoreMinimal.h"
#include "BasisTextureTypes.h"
#include "Engine/Texture2D.h"
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

    /** Guess texture usage from the source name. Importers use this only as an editable initial value. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Basis Universal")
    static EBasisTextureSemantic GuessTextureSemanticFromName(const FString& SourceName);

    /**
     * Transcode .basis or .ktx2 bytes already loaded in memory.
     * Uses GuessTextureSemanticFromName() for backwards compatibility.
     */
    static UTexture2D* LoadBasisTextureFromMemory(
        const TArray<uint8>& SourceData,
        const FString& SourceName,
        FBasisTranscodeInfo& OutInfo);

    /**
     * Transcode .basis or .ktx2 bytes already loaded in memory.
     * @param SourceData       Raw Basis Universal file bytes.
     * @param SourceName       Display/source name used for logging.
     * @param TextureSemantic  Explicit texture usage.
     * @param OutInfo          Size and format statistics for demo/comparison display.
     * @return                 Transient UTexture2D, or nullptr on failure.
     */
    static UTexture2D* LoadBasisTextureFromMemory(
        const TArray<uint8>& SourceData,
        const FString& SourceName,
        EBasisTextureSemantic TextureSemantic,
        FBasisTranscodeInfo& OutInfo);

    /**
     * Transcode Basis Universal bytes to platform-native compressed GPU blocks.
     * This does not create a UTexture2D and can be used to populate a native cache.
     * Uses GuessTextureSemanticFromName() for backwards compatibility.
     */
    static bool TranscodeBasisTextureToNativeBlocks(
        const TArray<uint8>& SourceData,
        const FString& SourceName,
        FBasisTranscodeInfo& OutInfo,
        TArray<uint8>& OutNativeBlocks);

    /**
     * Transcode Basis Universal bytes to platform-native compressed GPU blocks.
     * This does not create a UTexture2D and can be used to populate a native cache.
     */
    static bool TranscodeBasisTextureToNativeBlocks(
        const TArray<uint8>& SourceData,
        const FString& SourceName,
        EBasisTextureSemantic TextureSemantic,
        FBasisTranscodeInfo& OutInfo,
        TArray<uint8>& OutNativeBlocks);

    /**
     * Create a transient UTexture2D from native compressed GPU blocks.
     * Uses GuessTextureSemanticFromName() for backwards compatibility.
     */
    static UTexture2D* CreateTextureFromNativeBlocks(
        const TArray<uint8>& NativeBlocks,
        const FBasisTranscodeInfo& Info,
        const FString& SourceName);

    /**
     * Create a transient UTexture2D from native compressed GPU blocks.
     */
    static UTexture2D* CreateTextureFromNativeBlocks(
        const TArray<uint8>& NativeBlocks,
        const FBasisTranscodeInfo& Info,
        const FString& SourceName,
        EBasisTextureSemantic TextureSemantic);

    /**
     * Estimate how large an equivalent BC7 texture would be for a given resolution.
     * Kept for Blueprint compatibility with the original demo HUD.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal",
              meta = (DisplayName = "Estimate BC7 Size"))
    static int64 EstimateBC7Size(int32 Width, int32 Height, int32 MipLevels);
};
