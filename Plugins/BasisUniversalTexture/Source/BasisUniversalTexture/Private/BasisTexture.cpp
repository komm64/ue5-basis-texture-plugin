#include "BasisTexture.h"
#include "BasisTextureLoader.h"

UTexture2D* UBasisTexture::Transcode()
{
    if (BasisData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: no data"));
        return nullptr;
    }

    FBasisTranscodeInfo Info;
    return UBasisTextureLoader::LoadBasisTextureFromMemory(BasisData, GetName(), Info);
}
