param(
    [Parameter(Mandatory=$true)][ValidateSet('emission','reflections','gi','gi-black')][string]$Kind,
    [Parameter(Mandatory=$true)][string]$Path
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$lightingImage = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Path).Path)
try {
    if ($Kind -eq 'gi' -or $Kind -eq 'gi-black') {
        $lightingMaxima = @(0,0,0)
        for ($lightingY = 0.90; $lightingY -lt 0.99; $lightingY += 0.004) {
            for ($lightingX = 0.13; $lightingX -lt 0.91; $lightingX += 0.004) {
                $lightingPixel = $lightingImage.GetPixel([int]($lightingX*$lightingImage.Width),[int]($lightingY*$lightingImage.Height))
                $lightingMaxima[0] = [Math]::Max($lightingMaxima[0],$lightingPixel.R)
                $lightingMaxima[1] = [Math]::Max($lightingMaxima[1],$lightingPixel.G)
                $lightingMaxima[2] = [Math]::Max($lightingMaxima[2],$lightingPixel.B)
            }
        }
        # Allow one 8-bit code value through the full filtered capture path.
        $lightingTolerance = 1
        if ($lightingMaxima[0] -gt $lightingTolerance -or $lightingMaxima[2] -gt $lightingTolerance) { throw "GI has light outside the green emitter channel: $lightingMaxima" }
        if ($Kind -eq 'gi' -and $lightingMaxima[1] -le 0) { throw 'GI floor is unlit' }
        if ($Kind -eq 'gi-black' -and $lightingMaxima[1] -gt $lightingTolerance) { throw 'A black emission map injected light' }
        Write-Output "PASS $Kind floor maximum RGB: $lightingMaxima"
    } else {
        $lightingExpected = @(@(231,0,0),@(0,231,0),@(231,231,230),@(231,231,230))
        $lightingRows = if ($Kind -eq 'reflections') { @(0.65,0.94) } else { @(0.65) }
        foreach ($lightingY in $lightingRows) {
            for ($lightingPanel = 0; $lightingPanel -lt 4; $lightingPanel++) {
                $lightingPixel = $lightingImage.GetPixel([int]((0.125+0.25*$lightingPanel)*$lightingImage.Width),[int]($lightingY*$lightingImage.Height))
                $lightingActual = @($lightingPixel.R,$lightingPixel.G,$lightingPixel.B)
                for ($lightingChannel = 0; $lightingChannel -lt 3; $lightingChannel++) {
                    if ([Math]::Abs($lightingActual[$lightingChannel]-$lightingExpected[$lightingPanel][$lightingChannel]) -gt 3) { throw "Emission calibration mismatch: panel $lightingPanel at row $lightingY" }
                }
            }
        }
        Write-Output "PASS $Kind color and brightness"
    }
} finally {
    $lightingImage.Dispose()
}
