# WebView2 SDK를 third_party/webview2-prep에 설치한다.
# 패키지는 헤더, 정적 로더, 라이선스를 제공한다.
# third_party/webview2-prep.json의 버전과 SHA-256을 검사한다.
# 로더는 CRT 중립이므로 Debug·Release에서 같은 파일을 사용한다.
# 캐시된 nupkg는 -ArchiveDirectory로 지정한다.
# 페이지 실행에 필요한 Evergreen Runtime은 별도로 설치한다
# (docs/concepts/consumer-contract.md).

[CmdletBinding()]
param(
    [string]$Destination,
    [string]$PinFile,
    [string]$ArchiveDirectory,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# 진행 막대는 Invoke-WebRequest를 몇 배 느리게 만든다.
$ProgressPreference = 'SilentlyContinue'

$repository_root = Split-Path -Parent $PSScriptRoot
if (-not $PinFile) {
    $PinFile = Join-Path $repository_root 'third_party\webview2-prep.json'
}
if (-not (Test-Path -LiteralPath $PinFile -PathType Leaf)) {
    throw "The WebView2 SDK pin file was not found: $PinFile"
}
if (-not $Destination) {
    $Destination = Join-Path $repository_root 'third_party\webview2-prep'
}
if (-not $ArchiveDirectory) {
    $ArchiveDirectory = Join-Path $repository_root 'third_party\webview2-prep-archives'
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
        $target = Join-Path $Destination $pin.install.$name
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

if ((Test-Installed) -and -not $Force) {
    Write-Output "Already installed: $Destination"
    Write-Output "  WebView2 SDK   : $($pin.version)"
    Write-Output 'Give -Force to install it again.'
    return
}

# nupkg를 확보한다.
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
        Write-Host "stale, refetch : $($asset.file)"
        Remove-Item -LiteralPath $path -Force
    }

    if (-not $pin.asset_base_url) {
        throw @"
The WebView2 SDK package was not found and the pin file has no asset_base_url:
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
Failed to download the WebView2 SDK package: $url
$($_.Exception.Message)

Fetch it with a browser and put it in: $ArchiveDirectory
"@
    }

    $actual = Get-FileHashText -path $path
    if ($actual -ne $asset.sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $path -Force
        throw @"
The WebView2 SDK package does not match the pinned hash, so it was discarded.
  expected: $($asset.sha256)
  actual  : $actual
  url     : $url
"@
    }
    return $path
}

Write-Output "Pin file       : $PinFile"
Write-Output "WebView2 SDK   : $($pin.version)"
Write-Output "Destination    : $Destination"

$archive = Resolve-Asset

$staging = Join-Path $repository_root 'build\webview2-prep-staging'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# nupkg는 zip이다. 확장자를 바꾸지 않아도 -LiteralPath로 그대로 풀린다.
Expand-Archive -LiteralPath $archive -DestinationPath $staging -Force

if (Test-Path -LiteralPath $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

# 패키지 안에는 .NET 어셈블리와 WinRT 투영 도구까지 들어 있다.
# 네이티브 C++ 소비자에게 필요한 것만 평평하게 옮긴다 — 무엇을 옮길지는 핀이 정한다.
foreach ($name in $pin.install.PSObject.Properties.Name) {
    $source = Join-Path $staging ($name -replace '/', '\')
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw @"
The WebView2 SDK package is missing a file the pin file asks for:
  $name
The package may not be the one the pin describes: $($pin.asset.file)
"@
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $pin.install.$name) -Force
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
Write-Output ''
Write-Output 'The SDK is only the build-time half. The Evergreen Runtime that actually'
Write-Output 'renders pages is installed separately, by Microsoft or by the consumer:'
Write-Output '  https://developer.microsoft.com/microsoft-edge/webview2/'
