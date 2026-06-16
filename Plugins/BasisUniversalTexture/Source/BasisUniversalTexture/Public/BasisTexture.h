#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "BasisTexture.generated.h"

/**
 * Asset that stores a Basis Universal compressed texture (.basis format).
 *
 * At cook time: the raw .basis bytes are packed into the .uasset — much smaller
 * than a cooked BC7 texture.
 * At runtime: call Transcode() to decode into a GPU-ready UTexture2D.
 */
UCLASS(BlueprintType)
class BASISUNIVERSALTEXTURE_API UBasisTexture : public UObject
{
    GENERATED_BODY()

public:
    /** Raw .basis file bytes — this is what gets stored in the .uasset */
    UPROPERTY()
    TArray<uint8> BasisData;

    /** Width of the base mip */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int32 Width = 0;

    /** Height of the base mip */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int32 Height = 0;

    /** Source codec: "ETC1S" or "UASTC" */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    FString SourceFormat;

    /** Compressed size in bytes (= asset size on disk) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int64 CompressedSize = 0;

    /** Equivalent BC7 size in bytes (what a normal UE texture would cost) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Basis Info")
    int64 BC7EquivalentSize = 0;

    /**
     * Transcode the stored .basis data into a transient GPU-ready UTexture2D.
     */
    UFUNCTION(BlueprintCallable, Category = "Basis Universal")
    UTexture2D* Transcode();

    /** Compression ratio: BC7EquivalentSize / CompressedSize */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Basis Universal")
    float GetCompressionRatio() const
    {
        return CompressedSize > 0
            ? static_cast<float>(BC7EquivalentSize) / static_cast<float>(CompressedSize)
            : 0.f;
    }
};
