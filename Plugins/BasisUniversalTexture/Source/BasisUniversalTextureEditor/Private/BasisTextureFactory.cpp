#include "BasisTextureFactory.h"
#include "BasisTexture.h"
#include "BasisTextureLoader.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

UBasisTextureFactory::UBasisTextureFactory()
{
    SupportedClass = UBasisTexture::StaticClass();
    bCreateNew     = false;
    bEditAfterNew  = false;
    bEditorImport  = true;
    Formats.Add(TEXT("basis;Basis Universal Texture (ETC1S/UASTC)"));
    Formats.Add(TEXT("ktx2;Basis Universal Texture KTX2 (ETC1S/UASTC/XUASTC)"));
}

bool UBasisTextureFactory::FactoryCanImport(const FString& Filename)
{
    FString Ext = FPaths::GetExtension(Filename).ToLower();
    return Ext == TEXT("basis") || Ext == TEXT("ktx2");
}

UObject* UBasisTextureFactory::FactoryCreateBinary(
    UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
    UObject* Context, const TCHAR* Type,
    const uint8*& Buffer, const uint8* BufferEnd,
    FFeedbackContext* Warn)
{
    UBasisTexture* Asset = NewObject<UBasisTexture>(InParent, InName, Flags);

    // Store the raw .basis bytes
    const int64 DataSize = BufferEnd - Buffer;
    Asset->BasisData.SetNumUninitialized(DataSize);
    FMemory::Memcpy(Asset->BasisData.GetData(), Buffer, DataSize);
    Asset->CompressedSize = DataSize;

    // Read metadata via Basis Universal transcoder to fill info fields
    // (reuse LoadBasisTexture's info extraction via a temp file)
    {
        FString TempExt = FString(Type).ToLower();
        if (TempExt.IsEmpty())
        {
            TempExt = TEXT("basis");
        }

        const FString TempPath = FPaths::ProjectSavedDir()
            / FString::Printf(TEXT("BasisImportTemp_%s.%s"), *InName.ToString(), *TempExt);
        FFileHelper::SaveArrayToFile(Asset->BasisData, *TempPath);

        FBasisTranscodeInfo Info;
        UBasisTextureLoader::LoadBasisTexture(TempPath, Info);
        IFileManager::Get().Delete(*TempPath);

        Asset->Width            = Info.Width;
        Asset->Height           = Info.Height;
        Asset->SourceFormat     = Info.SourceFormat;
        Asset->TranscodedFormat = Info.TranscodedFormat;
        Asset->TranscodedSize   = Info.TranscodedSize;
    }

    UE_LOG(LogTemp, Log,
        TEXT("BasisTextureFactory: imported %s [%dx%d] %s -> %s | %lld bytes (%.1f KB) | gpu %.1f KB | ratio %.1fx"),
        *InName.ToString(), Asset->Width, Asset->Height, *Asset->SourceFormat,
        *Asset->TranscodedFormat,
        Asset->CompressedSize, Asset->CompressedSize / 1024.f,
        Asset->TranscodedSize / 1024.f, Asset->GetCompressionRatio());

    return Asset;
}
