#include "BasisDemoActor.h"
#include "BasisDemoHUD.h"
#include "BasisTextureLoader.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

ABasisDemoActor::ABasisDemoActor()
{
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = Mesh;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
        TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (CubeMesh.Succeeded())
        Mesh->SetStaticMesh(CubeMesh.Object);

    BasisFilePath = TEXT("");  // Set via editor or override in subclass
}

void ABasisDemoActor::BeginPlay()
{
    Super::BeginPlay();

    FBasisTranscodeInfo Info;
    UTexture2D* Tex = UBasisTextureLoader::LoadBasisTexture(BasisFilePath, Info);

    if (!Tex)
    {
        UE_LOG(LogTemp, Error, TEXT("[BasisDemo] Failed to load: %s"), *BasisFilePath);
        return;
    }

    // Set HUD class programmatically and pass texture + info
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC)
    {
        // Replace HUD with BasisDemoHUD regardless of GameMode setting
        PC->ClientSetHUD(ABasisDemoHUD::StaticClass());

        ABasisDemoHUD* HUD = Cast<ABasisDemoHUD>(PC->GetHUD());
        if (HUD)
        {
            HUD->BasisTex = Tex;
            HUD->TexInfo  = Info;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[BasisDemo] %s | %s | %dx%d | disk=%.1fKB | BC7=%.1fKB | %.1fx"),
        *FPaths::GetCleanFilename(BasisFilePath), *Info.SourceFormat,
        Info.Width, Info.Height,
        Info.CompressedFileSize / 1024.f,
        Info.TranscodedSize / 1024.f,
        Info.CompressionRatio);
}
