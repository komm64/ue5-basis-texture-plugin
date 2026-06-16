# encode_normals_xuastc8x8.ps1
# Encodes normal maps to XUASTC LDR 8x8 (2 bpp) KTX2 format.
#
# Edit the variables below to match your environment.
# $basisu : path to basisu.exe (https://github.com/BinomialLLC/basis_universal/releases)
# $srcDir : directory containing source PNG normal maps (*_nor_*.png)
# $outDir : output directory for KTX2 files

$basisu = "basisu.exe"                                    # set full path if not in PATH
$srcDir = Join-Path $PSScriptRoot "source_textures"
$outDir = Join-Path $PSScriptRoot "xuastc_8x8_textures"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$start = Get-Date
Write-Host "=== Encoding normal maps: XUASTC LDR 8x8 (2bpp) ==="
Write-Host "  Source : $srcDir"
Write-Host "  Output : $outDir"
Write-Host ""

$files = Get-ChildItem $srcDir -Filter "*_nor_*.png"
if ($files.Count -eq 0) {
    Write-Host "No normal map PNGs found in: $srcDir"
    exit 1
}

foreach ($f in $files) {
    $outFile = Join-Path $outDir ($f.BaseName + ".ktx2")
    & $basisu $f.FullName -ldr_8x8i -quality 128 -effort 6 -output_file $outFile 2>&1 | Out-Null
    if (Test-Path $outFile) {
        Write-Host ("  {0,-40} {1,6:N0} KB" -f $f.BaseName, ((Get-Item $outFile).Length / 1KB))
    } else {
        Write-Host "  FAILED: $($f.BaseName)"
    }
}

$elapsed = (Get-Date) - $start
$total   = (Get-ChildItem $outDir -Filter "*_nor_*.ktx2" | Measure-Object Length -Sum).Sum

Write-Host ""
Write-Host ("Done in {0:mm}m {0:ss}s" -f $elapsed)
Write-Host ("Total : {0:N2} MB ({1} files)" -f ($total / 1MB), $files.Count)
