#include "BasisTexture.h"
#include "BasisTextureLoader.h"
#include "HAL/FileManager.h"

UTexture2D* UBasisTexture::Transcode()
{
    if (BasisData.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UBasisTexture::Transcode: no data"));
        return nullptr;
    }

    FBasisTranscodeInfo Info;
    // Write to a temp file so we can reuse the existing file-based loader.
    // Include the asset name so LoadBasisTexture's normal-map filename check still works.
    const FString TempName = FString::Printf(TEXT("BasisTemp_%s.basis"), *GetName());
    const FString TempPath = FPaths::ProjectSavedDir() / TempName;
    FFileHelper::SaveArrayToFile(BasisData, *TempPath);

    UTexture2D* Tex = UBasisTextureLoader::LoadBasisTexture(TempPath, Info);
    IFileManager::Get().Delete(*TempPath);
    return Tex;
}
