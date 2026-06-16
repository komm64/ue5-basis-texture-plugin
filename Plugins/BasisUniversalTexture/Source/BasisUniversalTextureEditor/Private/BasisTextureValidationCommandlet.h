#pragma once

#include "Commandlets/Commandlet.h"
#include "BasisTextureValidationCommandlet.generated.h"

UCLASS()
class BASISUNIVERSALTEXTUREEDITOR_API UBasisTextureValidationCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UBasisTextureValidationCommandlet();

    virtual int32 Main(const FString& Params) override;
};
