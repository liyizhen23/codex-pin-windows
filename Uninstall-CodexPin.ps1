$ErrorActionPreference = 'Stop'

$installDir = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'CodexPin'))
$localRoot = [IO.Path]::GetFullPath($env:LOCALAPPDATA)
if (-not $installDir.StartsWith($localRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw '安全检查失败：安装目录不在 LOCALAPPDATA 中。'
}

$installedExe = Join-Path $installDir 'CodexPin.exe'
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'Codex Pin.lnk'

Write-Host '卸载将执行以下操作：'
Write-Host "1. 关闭由 $installedExe 启动的 CodexPin 进程（仅置顶按钮，不是 Codex）。"
Write-Host "2. 删除程序文件：$installedExe"
Write-Host "3. 删除开机启动快捷方式：$shortcutPath"
$answer = Read-Host '确认卸载请输入 Y'
if ($answer -notin @('Y', 'y')) {
    Write-Host '已取消，未删除任何文件。'
    exit 0
}

Get-Process -Name 'CodexPin' -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -eq [IO.Path]::GetFullPath($installedExe)) } |
    Stop-Process

if (Test-Path -LiteralPath $installedExe) {
    Remove-Item -LiteralPath $installedExe
}
if (Test-Path -LiteralPath $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath
}
if ((Test-Path -LiteralPath $installDir) -and -not (Get-ChildItem -LiteralPath $installDir -Force)) {
    Remove-Item -LiteralPath $installDir
}

Write-Host 'Codex Pin 已卸载。'
