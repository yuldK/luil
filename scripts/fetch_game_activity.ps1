# GameActivity의 네이티브 절반을 third_party/game-activity-prep에 설치한다.
# Android 앱 host가 쓰는 헤더와 arm64-v8a 정적 라이브러리다 (docs/android-port-plan.md).
# Java 절반(GameActivity 클래스)은 같은 판번의 AAR을 Gradle이 받는다 — 두 절반의
# 판번은 이 핀 하나가 정한다 (examples/android가 핀의 version을 읽는다).
# third_party/game-activity-prep.json의 버전과 SHA-256을 검사한다.
# 캐시된 AAR은 -ArchiveDirectory로 지정한다.
# -Offline을 주면 AAR이 없어도 받으러 가지 않고 실패한다.

[CmdletBinding()]
param(
    [string]$Destination,
    [string]$PinFile,
    [string]$ArchiveDirectory,
    [switch]$Force,
    # 네트워크를 쓰지 않는다. 아카이브가 없거나 해시가 다르면 받지 않고
    # 어디에 두어야 하는지만 말하고 실패한다.
    [switch]$Offline
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# 진행 막대는 Invoke-WebRequest를 몇 배 느리게 만든다.
$ProgressPreference = 'SilentlyContinue'

$repository_root = Split-Path -Parent $PSScriptRoot
if (-not $PinFile) {
    $PinFile = Join-Path $repository_root 'third_party\game-activity-prep.json'
}
if (-not (Test-Path -LiteralPath $PinFile -PathType Leaf)) {
    throw "The GameActivity pin file was not found: $PinFile"
}
if (-not $Destination) {
    $Destination = Join-Path $repository_root 'third_party\game-activity-prep'
}
if (-not $ArchiveDirectory) {
    $ArchiveDirectory = Join-Path $repository_root 'third_party\game-activity-prep-archives'
}

$pin = Get-Content -Raw -LiteralPath $PinFile | ConvertFrom-Json

function Get-FileHashText {
    param([string]$path)

    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# 이미 이 판번이 설치되어 있는지 본다.
# 판번이 같고 설치 목록이 모두 있으면 아무것도 하지 않는다.
function Test-Installed {
    $version_file = Join-Path $Destination 'VERSION.json'
    if (-not (Test-Path -LiteralPath $version_file -PathType Leaf)) {
        return $false
    }
    try {
        $installed = Get-Content -Raw -LiteralPath $version_file | ConvertFrom-Json
    }
    catch {
        return $false
    }
    if ($installed.version -ne $pin.version) {
        return $false
    }
    foreach ($name in $pin.install.PSObject.Properties.Name) {
        $target = Join-Path $Destination ($pin.install.$name -replace '/', '\')
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

if ((Test-Installed) -and -not $Force) {
    Write-Output "Already installed: $Destination"
    Write-Output "  GameActivity   : $($pin.version)"
    Write-Output 'Give -Force to install it again.'
    return
}

# AAR을 확보한다.
# 이미 받아 둔 것이 해시까지 맞으면 다시 받지 않는다.
#  - 진행 메시지는 Write-Host로 낸다. Write-Output으로 내면 반환 스트림에 섞여
#    호출자가 경로 대신 메시지까지 받는다.
function Resolve-Asset {
    $asset = $pin.asset
    New-Item -ItemType Directory -Force -Path $ArchiveDirectory | Out-Null
    $path = Join-Path $ArchiveDirectory $asset.file

    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if ((Get-FileHashText -path $path) -eq $asset.sha256.ToLowerInvariant()) {
            Write-Host "cached         : $($asset.file)"
            return $path
        }
        if ($Offline) {
            throw @"
The GameActivity package does not match the pinned hash, and -Offline forbids fetching it:
  $path
  expected: $($asset.sha256)
  actual  : $((Get-FileHashText -path $path))
Replace it with the pinned archive and run again.
"@
        }
        Write-Host "stale, refetch : $($asset.file)"
        Remove-Item -LiteralPath $path -Force
    }

    if ($Offline) {
        throw @"
The GameActivity package was not found, and -Offline forbids fetching it:
  $path
Put the archive there (or pass -ArchiveDirectory) and run again.
"@
    }
    if (-not $pin.asset_base_url) {
        throw @"
The GameActivity package was not found and the pin file has no asset_base_url:
  $path
Download it by hand and put it there, or pass -ArchiveDirectory.
"@
    }
    $url = '{0}/{1}' -f $pin.asset_base_url.TrimEnd('/'), $asset.file
    Write-Host "downloading    : $url"
    try {
        Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
    }
    catch {
        throw @"
Failed to download the GameActivity package: $url
$($_.Exception.Message)

Fetch it with a browser and put it in: $ArchiveDirectory
"@
    }

    $actual = Get-FileHashText -path $path
    if ($actual -ne $asset.sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $path -Force
        throw @"
The GameActivity package does not match the pinned hash, so it was discarded.
  expected: $($asset.sha256)
  actual  : $actual
  url     : $url
"@
    }
    return $path
}

Write-Output "Pin file       : $PinFile"
Write-Output "GameActivity   : $($pin.version)"
Write-Output "Destination    : $Destination"

$archive = Resolve-Asset

$staging = Join-Path $repository_root 'build\game-activity-prep-staging'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# AAR은 zip이다. 확장자를 바꾸지 않아도 -LiteralPath로 그대로 풀린다.
Expand-Archive -LiteralPath $archive -DestinationPath $staging -Force

if (Test-Path -LiteralPath $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

# AAR 안에는 Java 클래스와 네 ABI의 라이브러리, prefab 메타데이터가 함께 들어 있다.
# luil이 쓰는 헤더와 arm64-v8a 정적 라이브러리만 옮긴다 — 무엇을 옮길지는 핀이 정한다.
foreach ($name in $pin.install.PSObject.Properties.Name) {
    $source = Join-Path $staging ($name -replace '/', '\')
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw @"
The GameActivity package is missing a file the pin file asks for:
  $name
The package may not be the one the pin describes: $($pin.asset.file)
"@
    }
    $target = Join-Path $Destination ($pin.install.$name -replace '/', '\')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}

Remove-Item -LiteralPath $staging -Recurse -Force

# 설치된 판번을 남긴다. 다음 실행이 이것을 보고 건너뛴다.
$version = [ordered]@{
    version = $pin.version
    target  = $pin.target
    source  = $pin.source
    sha256  = $pin.asset.sha256
}
$version_json = $version | ConvertTo-Json -Depth 4
Set-Content -LiteralPath (Join-Path $Destination 'VERSION.json') -Value $version_json -Encoding utf8NoBOM

$installed_bytes = (Get-ChildItem -LiteralPath $Destination -File -Recurse | Measure-Object -Property Length -Sum).Sum
Write-Output ('Installed      : {0:N1} MB' -f ($installed_bytes / 1MB))
