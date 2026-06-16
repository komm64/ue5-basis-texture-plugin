#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BasisTextureMaterialBinder.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UBasisTexture;
class UTexture2D;

USTRUCT(BlueprintType)
struct BASISUNIVERSALTEXTURE_API FBasisMaterialTextureParameter
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    FName ParameterName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    TObjectPtr<UBasisTexture> BasisTexture = nullptr;
};

USTRUCT(BlueprintType)
struct BASISUNIVERSALTEXTURE_API FBasisMaterialTextureBinding
{
    GENERATED_BODY()

    /** Material or material instance currently assigned to mesh components in the level/assets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;

    /** Texture parameters to populate from Basis assets at runtime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    TArray<FBasisMaterialTextureParameter> TextureParameters;
};

/**
 * Applies Basis textures to material parameters at runtime.
 *
 * This is intended for footprint-optimized builds where authored materials keep
 * tiny placeholder Texture2D references for cooking, then receive transient
 * GPU-ready textures produced from UBasisTexture assets at startup.
 */
UCLASS(BlueprintType, Blueprintable)
class BASISUNIVERSALTEXTURE_API ABasisTextureMaterialBinder : public AActor
{
    GENERATED_BODY()

public:
    ABasisTextureMaterialBinder();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    bool bApplyToWorldOnBeginPlay = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    bool bWarmNativeCachesBeforeBinding = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basis")
    TArray<FBasisMaterialTextureBinding> MaterialBindings;

    UFUNCTION(BlueprintCallable, Category = "Basis")
    int32 ApplyBindingsToWorld();

    UFUNCTION(BlueprintCallable, Category = "Basis")
    void ClearRuntimeBindings();

protected:
    virtual void BeginPlay() override;

private:
    UTexture2D* GetOrCreateRuntimeTexture(UBasisTexture* BasisTexture);
    int32 ApplyBindingToComponent(UPrimitiveComponent* Component, const TMap<UMaterialInterface*, const FBasisMaterialTextureBinding*>& BindingByMaterial);

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> RuntimeTextures;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> RuntimeMaterials;

    TMap<UBasisTexture*, UTexture2D*> TranscodedTextureCache;
};
