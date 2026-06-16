#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BasisTexture.h"
#include "Engine/DirectionalLight.h"
#include "GalleryActor.generated.h"

UCLASS()
class BASISDEMO_API AGalleryActor : public AActor
{
    GENERATED_BODY()

public:
    AGalleryActor();

    /** Standard albedo textures (BC7) */
    UPROPERTY(EditAnywhere, Category = "Gallery|Standard")
    TArray<UTexture2D*> StandardAlbedos;

    /** Standard normal map textures (BC7) */
    UPROPERTY(EditAnywhere, Category = "Gallery|Standard")
    TArray<UTexture2D*> StandardNormals;

    /** Basis Universal albedo assets */
    UPROPERTY(EditAnywhere, Category = "Gallery|Basis")
    TArray<UBasisTexture*> BasisAlbedos;

    /** Basis Universal normal map assets */
    UPROPERTY(EditAnywhere, Category = "Gallery|Basis")
    TArray<UBasisTexture*> BasisNormals;

    UPROPERTY(EditAnywhere, Category = "Gallery")
    bool bUseBasis = false;

    UPROPERTY(EditAnywhere, Category = "Gallery")
    int32 Columns = 5;

    UPROPERTY(EditAnywhere, Category = "Gallery")
    float PlaneSize = 200.f;

    UPROPERTY(EditAnywhere, Category = "Gallery")
    float Spacing = 260.f;

    /** Material with "Albedo" and "Normal" texture parameters */
    UPROPERTY(EditAnywhere, Category = "Gallery")
    UMaterialInterface* DisplayMaterial = nullptr;

    /** Fixed sun pitch angle (negative = above horizon). */
    UPROPERTY(EditAnywhere, Category = "Gallery|Sun")
    float SunPitch = -35.f;

    /** Fixed sun yaw angle. */
    UPROPERTY(EditAnywhere, Category = "Gallery|Sun")
    float SunYaw = 60.f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    void SpawnPlanes(const TArray<UTexture2D*>& Albedos, const TArray<UTexture2D*>& Normals);

    UPROPERTY()
    ADirectionalLight* SunLight = nullptr;
};
