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
