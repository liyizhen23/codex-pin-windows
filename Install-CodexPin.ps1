$ErrorActionPreference = 'Stop'

$sourceExe = Join-Path $PSScriptRoot 'CodexPin.exe'
if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
    throw "未找到安装文件：$sourceExe。请先解压完整 Release 安装包。"
}

$installDir = Join-Path $env:LOCALAPPDATA 'CodexPin'
$installedExe = Join-Path $installDir 'CodexPin.exe'
$startupDir = [Environment]::GetFolderPath('Startup')
$shortcutPath = Join-Path $startupDir 'Codex Pin.lnk'

$existing = Get-Process -Name 'CodexPin' -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -eq [IO.Path]::GetFullPath($installedExe)) }
if ($existing) {
    $existing | Stop-Process
    $existing | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Path $installDir -Force | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination $installedExe -Force

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $installedExe
$shortcut.WorkingDirectory = $installDir
$shortcut.WindowStyle = 7
$shortcut.Description = 'Codex 窗口置顶按钮'
$shortcut.Save()

Start-Process -FilePath $installedExe -WindowStyle Hidden
Write-Host "Codex Pin 已安装并启动：$installedExe"
Write-Host "开机启动项：$shortcutPath"
