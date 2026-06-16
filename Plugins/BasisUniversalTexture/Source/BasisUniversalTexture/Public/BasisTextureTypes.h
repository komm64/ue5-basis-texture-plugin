#pragma once

#include "CoreMinimal.h"
#include "BasisTextureTypes.generated.h"

UENUM(BlueprintType)
enum class EBasisTextureSemantic : uint8
{
    /** Color/albedo-like data. Runtime textures are sampled as sRGB. */
    Color UMETA(DisplayName = "Color"),

    /** Normal-vector data. Runtime textures use normal-map compression settings and linear sampling. */
    NormalMap UMETA(DisplayName = "Normal Map")
};

UENUM(BlueprintType)
enum class EBasisNativeTargetProfile : uint8
{
    /** Use the plugin's default native format for the current platform. */
    DefaultForCurrentPlatform UMETA(DisplayName = "Default For Current Platform"),

    /** Desktop/console-friendly BC blocks. Color uses BC1, normal maps use BC7. */
    DesktopBC UMETA(DisplayName = "Desktop BC"),

    /** ASTC 8x8 RGBA blocks for ASTC-capable mobile GPUs. */
    MobileASTC8x8 UMETA(DisplayName = "Mobile ASTC 8x8")
};

USTRUCT(BlueprintType)
struct BASISUNIVERSALTEXTURE_API FBasisNativeMipInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 Width = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 Height = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 OffsetBytes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Basis")
    int32 SizeBytes = 0;
};
