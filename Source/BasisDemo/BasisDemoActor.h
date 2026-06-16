#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BasisDemoActor.generated.h"

UCLASS()
class BASISDEMO_API ABasisDemoActor : public AActor
{
    GENERATED_BODY()

public:
    ABasisDemoActor();

protected:
    virtual void BeginPlay() override;

public:
    UPROPERTY(VisibleAnywhere)
    class UStaticMeshComponent* Mesh;

    /** Absolute path to the .basis file to load */
    UPROPERTY(EditAnywhere, Category = "Basis Demo")
    FString BasisFilePath;
};
