#include "BasisDemoHUD.h"
#include "Engine/Canvas.h"

void ABasisDemoHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!BasisTex || !Canvas) return;

    const float W = Canvas->SizeX;
    const float H = Canvas->SizeY;

    // Draw the transcoded texture on the right half of the screen
    const float TexW = W * 0.45f;
    const float TexH = TexW; // square
    const float X    = W * 0.53f;
    const float Y    = (H - TexH) * 0.5f;

    Canvas->K2_DrawTexture(BasisTex, FVector2D(X, Y), FVector2D(TexW, TexH),
        FVector2D::ZeroVector, FVector2D::UnitVector, FLinearColor::White,
        EBlendMode::BLEND_Opaque);

    // Size comparison text on the left
    const FString Line1 = FString::Printf(TEXT("Format:      %s"), *TexInfo.SourceFormat);
    const FString Line2 = FString::Printf(TEXT("Resolution:  %d x %d"), TexInfo.Width, TexInfo.Height);
    const FString Line3 = FString::Printf(TEXT(".basis size: %.1f KB  (on disk)"), TexInfo.CompressedFileSize / 1024.f);
    const FString Line4 = FString::Printf(TEXT("BC7 equiv:   %.1f KB  (normal UE texture)"), TexInfo.TranscodedSize / 1024.f);
    const FString Line5 = FString::Printf(TEXT("Ratio:       %.1fx smaller"), TexInfo.CompressionRatio);

    float TY = H * 0.35f;
    const float LH = 32.f;
    DrawText(TEXT("[ Basis Universal Texture Demo ]"), FLinearColor::Yellow, 40, TY,       nullptr, 1.4f); TY += LH * 1.6f;
    DrawText(Line1, FLinearColor::White,  40, TY, nullptr, 1.2f); TY += LH;
    DrawText(Line2, FLinearColor::White,  40, TY, nullptr, 1.2f); TY += LH;
    DrawText(Line3, FLinearColor::Green,  40, TY, nullptr, 1.2f); TY += LH;
    DrawText(Line4, FLinearColor::Red,    40, TY, nullptr, 1.2f); TY += LH;
    DrawText(Line5, FLinearColor(0.f, 1.f, 1.f), 40, TY, nullptr, 1.4f);
}
