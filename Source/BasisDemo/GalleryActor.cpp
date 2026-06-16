#include "GalleryActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"

AGalleryActor::AGalleryActor()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AGalleryActor::BeginPlay()
{
    Super::BeginPlay();

    // Find the DirectionalLight so both builds use the same comparison light.
    TArray<AActor*> Lights;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADirectionalLight::StaticClass(), Lights);
    if (Lights.Num() > 0)
        SunLight = Cast<ADirectionalLight>(Lights[0]);

    TArray<UTexture2D*> Albedos, Normals;

    if (bUseBasis)
    {
        for (UBasisTexture* BT : BasisAlbedos)
            Albedos.Add(BT ? BT->Transcode() : nullptr);
        for (UBasisTexture* BT : BasisNormals)
            Normals.Add(BT ? BT->Transcode() : nullptr);
    }
    else
    {
        Albedos = StandardAlbedos;
        Normals = StandardNormals;
    }

    SpawnPlanes(Albedos, Normals);
}

void AGalleryActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (SunLight)
    {
        SunLight->SetActorRotation(FRotator(SunPitch, SunYaw, 0.0f));
    }
}

void AGalleryActor::SpawnPlanes(const TArray<UTexture2D*>& Albedos, const TArray<UTexture2D*>& Normals)
{
    UMaterialInterface* BaseMat = DisplayMaterial;
    if (!BaseMat)
        BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_TextureDisplay.M_TextureDisplay"));
    if (!BaseMat)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Gallery] DisplayMaterial not set and M_TextureDisplay not found"));
        return;
    }

    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!PlaneMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[Gallery] Could not load Engine plane mesh"));
        return;
    }

    const float ScaleFactor = PlaneSize / 100.f;
    const int32 Count = Albedos.Num();

    for (int32 i = 0; i < Count; ++i)
    {
        UTexture2D* Albedo = Albedos[i];
        UTexture2D* Normal = Normals.IsValidIndex(i) ? Normals[i] : nullptr;

        const int32 Col = i % Columns;
        const int32 Row = i / Columns;
        FVector LocalPos(Col * Spacing, 0.f, Row * Spacing);

        UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
        Comp->SetStaticMesh(PlaneMesh);
        Comp->SetRelativeLocation(LocalPos);
        Comp->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
        Comp->SetRelativeScale3D(FVector(ScaleFactor));
        Comp->SetupAttachment(RootComponent);
        Comp->RegisterComponent();

        if (BaseMat && Albedo)
        {
            UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
            MID->SetTextureParameterValue(TEXT("Texture"), Albedo);
            if (Normal)
                MID->SetTextureParameterValue(TEXT("Normal"), Normal);
            Comp->SetMaterial(0, MID);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[Gallery] Spawned %d planes (%s mode)"),
        Count, bUseBasis ? TEXT("Basis") : TEXT("Standard"));
}
