#include "BasisTextureMaterialBinder.h"

#include "BasisTexture.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

ABasisTextureMaterialBinder::ABasisTextureMaterialBinder()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ABasisTextureMaterialBinder::BeginPlay()
{
    Super::BeginPlay();

    if (bApplyToWorldOnBeginPlay)
    {
        ApplyBindingsToWorld();
    }
}

void ABasisTextureMaterialBinder::ClearRuntimeBindings()
{
    RuntimeMaterials.Reset();
    RuntimeTextures.Reset();
    TranscodedTextureCache.Reset();
}

UTexture2D* ABasisTextureMaterialBinder::GetOrCreateRuntimeTexture(UBasisTexture* BasisTexture)
{
    if (!BasisTexture)
    {
        return nullptr;
    }

    if (UTexture2D** ExistingTexture = TranscodedTextureCache.Find(BasisTexture))
    {
        return *ExistingTexture;
    }

    if (bWarmNativeCachesBeforeBinding)
    {
        BasisTexture->WarmNativeCache();
    }

    UTexture2D* RuntimeTexture = BasisTexture->Transcode();
    if (!RuntimeTexture)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("BasisTextureMaterialBinder: failed to transcode %s"),
            *BasisTexture->GetPathName());
        return nullptr;
    }

    RuntimeTextures.Add(RuntimeTexture);
    TranscodedTextureCache.Add(BasisTexture, RuntimeTexture);
    return RuntimeTexture;
}

int32 ABasisTextureMaterialBinder::ApplyBindingToComponent(
    UPrimitiveComponent* Component,
    const TMap<UMaterialInterface*, const FBasisMaterialTextureBinding*>& BindingByMaterial)
{
    if (!Component)
    {
        return 0;
    }

    int32 AppliedSlots = 0;
    const int32 MaterialCount = Component->GetNumMaterials();
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        UMaterialInterface* Material = Component->GetMaterial(MaterialIndex);
        const FBasisMaterialTextureBinding* const* BindingPtr = BindingByMaterial.Find(Material);
        if (!BindingPtr || !*BindingPtr)
        {
            continue;
        }

        const FBasisMaterialTextureBinding& Binding = **BindingPtr;
        UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(Material, this);
        if (!DynamicMaterial)
        {
            continue;
        }

        int32 AppliedParameters = 0;
        for (const FBasisMaterialTextureParameter& Parameter : Binding.TextureParameters)
        {
            if (Parameter.ParameterName.IsNone() || !Parameter.BasisTexture)
            {
                continue;
            }

            UTexture2D* RuntimeTexture = GetOrCreateRuntimeTexture(Parameter.BasisTexture);
            if (!RuntimeTexture)
            {
                continue;
            }

            DynamicMaterial->SetTextureParameterValue(Parameter.ParameterName, RuntimeTexture);
            ++AppliedParameters;
        }

        if (AppliedParameters > 0)
        {
            Component->SetMaterial(MaterialIndex, DynamicMaterial);
            RuntimeMaterials.Add(DynamicMaterial);
            ++AppliedSlots;
        }
    }

    return AppliedSlots;
}

int32 ABasisTextureMaterialBinder::ApplyBindingsToWorld()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return 0;
    }

    TMap<UMaterialInterface*, const FBasisMaterialTextureBinding*> BindingByMaterial;
    for (const FBasisMaterialTextureBinding& Binding : MaterialBindings)
    {
        if (Binding.SourceMaterial && Binding.TextureParameters.Num() > 0)
        {
            BindingByMaterial.Add(Binding.SourceMaterial, &Binding);
        }
    }

    if (BindingByMaterial.Num() == 0)
    {
        return 0;
    }

    int32 AppliedSlots = 0;
    for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
    {
        AActor* Actor = *ActorIt;
        if (!Actor || Actor == this)
        {
            continue;
        }

        TArray<UPrimitiveComponent*> PrimitiveComponents;
        Actor->GetComponents(PrimitiveComponents);
        for (UPrimitiveComponent* Component : PrimitiveComponents)
        {
            AppliedSlots += ApplyBindingToComponent(Component, BindingByMaterial);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("BasisTextureMaterialBinder: applied %d material slots using %d material bindings and %d runtime textures"),
        AppliedSlots,
        BindingByMaterial.Num(),
        RuntimeTextures.Num());

    return AppliedSlots;
}
