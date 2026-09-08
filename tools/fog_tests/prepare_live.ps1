# Prepare disposable fixtures without saving changes to the source worlds.
$ErrorActionPreference = 'Stop'
$fogRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$fogOutput = Join-Path $fogRoot 'binaries/fog_tests'
New-Item -ItemType Directory -Force -Path $fogOutput | Out-Null
[xml]$fogWater = Get-Content -Raw -LiteralPath (Join-Path $fogRoot 'worlds/dreamcore.world')
$fogVariables = $fogWater.SelectSingleNode('/World/ConsoleVariables')
foreach ($fogSetting in @(@('r.fog','1'), @('r.ray_traced_shadows','1'), @('r.ray_traced_reflections','1'), @('r.entity_icons','0')))
{
    $fogVariable = $fogWater.CreateElement('Variable')
    $fogVariable.SetAttribute('name', $fogSetting[0])
    $fogVariable.SetAttribute('value', $fogSetting[1])
    $null = $fogVariables.AppendChild($fogVariable)
}
$fogWater.Save((Join-Path $fogOutput 'water.world'))
[xml]$fogRaster = $fogWater.OuterXml
foreach ($fogRt in $fogRaster.SelectNodes('/World/ConsoleVariables/Variable[@name="r.ray_traced_shadows" or @name="r.ray_traced_reflections"]'))
{
    $fogRt.SetAttribute('value', '0')
}
$fogRaster.Save((Join-Path $fogOutput 'water_raster.world'))
[xml]$fogLandscape = Get-Content -Raw -LiteralPath (Join-Path $fogRoot 'worlds/plan.world')
foreach ($fogEntity in @($fogLandscape.SelectNodes('/World/Entities/Entity')))
{
    if ($fogEntity.GetAttribute('name') -ne 'Landscape') { $null = $fogEntity.ParentNode.RemoveChild($fogEntity) }
}
foreach ($fogTerrain in $fogLandscape.SelectNodes('//terrain')) { $fogTerrain.SetAttribute('spawn_biome_props','false') }
foreach ($fogExtra in $fogWater.SelectNodes('/World/Entities/Entity[camera or light or Entity/camera]'))
{
    $null = $fogLandscape.SelectSingleNode('/World/Entities').AppendChild($fogLandscape.ImportNode($fogExtra,$true))
}
$null = $fogLandscape.SelectSingleNode('/World').ReplaceChild($fogLandscape.ImportNode($fogVariables,$true),$fogLandscape.SelectSingleNode('/World/ConsoleVariables'))
$fogLandscape.Save((Join-Path $fogOutput 'landscape.world'))
