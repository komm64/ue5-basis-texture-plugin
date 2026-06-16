#pragma once

#include "CoreMinimal.h"
#include "BasisTextureTypes.generated.h"

UENUM(BlueprintType)
enum class EBasisTextureSemantic : uint8
{
    /** Color/albedo-like data. Runtime textures are sampled as sRGB. */
    Color = 0 UMETA(DisplayName = "Color"),

    /** Normal-vector data. Runtime textures use normal-map compression settings and linear sampling. */
    NormalMap = 1 UMETA(DisplayName = "Normal Map"),

    /** Color/albedo-like data with full alpha. Runtime textures are sampled as sRGB. */
    ColorWithAlpha = 2 UMETA(DisplayName = "Color With Alpha"),

    /** Non-color data such as roughness, metallic, ambient occlusion, height, or packed masks. Runtime textures are sampled linearly. */
    Data = 3 UMETA(DisplayName = "Data / Linear")
};

UENUM(BlueprintType)
enum class EBasisNativeTargetProfile : uint8
{
    /** Use the plugin's default native format for the current platform. */
    DefaultForCurrentPlatform = 0 UMETA(DisplayName = "Default For Current Platform"),

    /** Desktop/console-friendly fast runtime BC blocks. Color uses BC1, alpha uses BC3, normal maps use BC5. */
    DesktopBC = 1 UMETA(DisplayName = "Desktop BC Fast Runtime"),

    /** ASTC 8x8 RGBA blocks for ASTC-capable mobile GPUs. */
    MobileASTC8x8 = 2 UMETA(DisplayName = "Mobile ASTC 8x8"),

    /** Desktop/console-friendly quality BC blocks. Color uses BC1, alpha and normal maps use BC7. */
    DesktopBCQuality = 3 UMETA(DisplayName = "Desktop BC Quality")
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
