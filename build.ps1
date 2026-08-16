[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourcePath = Join-Path $repoRoot 'src\CodexPinNative.cpp'
$distDir = Join-Path $repoRoot 'dist'
$outputPath = Join-Path $distDir 'CodexPin.exe'
$objectPath = Join-Path $distDir 'CodexPinNative.obj'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
    throw '未找到 vswhere.exe。请安装 Visual Studio 2022 Build Tools。'
}

$vsInstallPath = & $vswherePath -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $vsInstallPath) {
    throw '未找到包含 MSVC x64 工具集的 Visual Studio 2022。'
}

$devShellModule = Join-Path $vsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

$compilerArgs = @(
    '/nologo'
    '/std:c++20'
    '/utf-8'
    '/EHsc'
    '/O2'
    '/MT'
    "/Fo$objectPath"
    $sourcePath
    '/link'
    '/SUBSYSTEM:WINDOWS'
    '/OPT:REF'
    '/OPT:ICF'
    'user32.lib'
    'gdi32.lib'
    "/OUT:$outputPath"
)

& cl.exe @compilerArgs
if ($LASTEXITCODE -ne 0) {
    throw "MSVC 构建失败，退出代码：$LASTEXITCODE"
}

Write-Host "构建完成：$outputPath"
