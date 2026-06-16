#include "BasisTextureFactory.h"
#include "BasisTexture.h"
#include "BasisTextureLoader.h"
#include "Misc/Paths.h"

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

    // Store the raw Basis Universal bytes
    const int64 DataSize = BufferEnd - Buffer;
    Asset->BasisData.SetNumUninitialized(DataSize);
    FMemory::Memcpy(Asset->BasisData.GetData(), Buffer, DataSize);
    Asset->CompressedSize = DataSize;

    // Read metadata through the same in-memory path used at runtime.
    {
        FBasisTranscodeInfo Info;
        TArray<uint8> NativeBlocks;
        if (!UBasisTextureLoader::TranscodeBasisTextureToNativeBlocks(
                Asset->BasisData, InName.ToString(), Info, NativeBlocks))
        {
            UE_LOG(LogTemp, Warning, TEXT("BasisTextureFactory: failed to import %s"), *InName.ToString());
            return nullptr;
        }

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
