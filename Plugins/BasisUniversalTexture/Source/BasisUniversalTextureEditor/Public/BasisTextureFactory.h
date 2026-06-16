#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "BasisTextureFactory.generated.h"

/**
 * Factory that imports .basis or .ktx2 files into UBasisTexture assets.
 * Drop a Basis Universal file into the Content Browser to create a UBasisTexture.
 */
UCLASS()
class UBasisTextureFactory : public UFactory
{
    GENERATED_BODY()

public:
    UBasisTextureFactory();

    virtual UObject* FactoryCreateBinary(
        UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
        UObject* Context, const TCHAR* Type,
        const uint8*& Buffer, const uint8* BufferEnd,
        FFeedbackContext* Warn) override;

    virtual bool FactoryCanImport(const FString& Filename) override;
};
