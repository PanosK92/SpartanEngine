# start blender with mcp socket server already connected
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$script = Join-Path $PSScriptRoot 'start_mcp_server.py'

# stop leftover blender so port 9876 is free
Get-Process blender -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

Start-Process -FilePath $blender -ArgumentList '--python', $script
Write-Output 'blender mcp launching, socket on localhost:9876'
