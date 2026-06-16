#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BasisTextureLoader.h"
#include "BasisDemoHUD.generated.h"

UCLASS()
class BASISDEMO_API ABasisDemoHUD : public AHUD
{
    GENERATED_BODY()
public:
    UPROPERTY() UTexture2D* BasisTex = nullptr;
    FBasisTranscodeInfo TexInfo;

    virtual void DrawHUD() override;
};
