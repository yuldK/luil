# 배포된 Skia 패키지를 받아 third_party/skia-prep에 설치한다.
#
# luil를 빌드하는 데 필요한 Skia는 이것으로 끝난다. Skia 소스도, external
# submodule도, gn·ninja·bazelisk도 필요 없다 — 패키지가 헤더와 정적 라이브러리와
# 고지를 이미 담고 있다 (skia-prep의 pack_skia.ps1이 만든다).
#
# 받을 것은 third_party/skia-prep.json이 정한다. 그 파일이 판번을 고정하는
# 자리이며, 자산마다 SHA-256을 함께 담는다 — 값이 다르면 설치하지 않는다.
#
# 자산이 놓인 자리는 핀 파일의 asset_base_url이 말한다. GitHub Releases든
# 사내 파일 서버든 이 스크립트는 구별하지 않는다.
#
# 이미 내려받아 둔 zip이 있으면 -ArchiveDirectory로 가리킨다. 그때는 네트워크를
# 전혀 쓰지 않는다. -Offline을 주면 zip이 없어도 받으러 가지 않고 실패한다 —
# 시도 자체가 허용되지 않는 환경을 위한 스위치다.

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string[]]$Configuration = @('Release'),
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
    $PinFile = Join-Path $repository_root 'third_party\skia-prep.json'
}
if (-not (Test-Path -LiteralPath $PinFile -PathType Leaf)) {
    throw "The Skia package pin file was not found: $PinFile"
}
if (-not $Destination) {
    $Destination = Join-Path $repository_root 'third_party\skia-prep'
}
if (-not $ArchiveDirectory) {
    $ArchiveDirectory = Join-Path $repository_root 'third_party\skia-prep-archives'
}

$pin = Get-Content -Raw -LiteralPath $PinFile | ConvertFrom-Json
$configurations = @($Configuration | Sort-Object -Unique)

foreach ($name in $configurations) {
    if (-not $pin.assets.PSObject.Properties.Name.Contains($name)) {
        throw @"
The pin file has no $name asset: $PinFile
Available: $($pin.assets.PSObject.Properties.Name -join ', ')
"@
    }
}

function Get-FileHashText {
    param([string]$path)

    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# 이미 이 판번이 설치되어 있는지 본다.
# 요구한 구성이 모두 있고 skia commit이 같으면 아무것도 하지 않는다.
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
    if ($installed.skia.commit -ne $pin.skia_commit) {
        return $false
    }
    foreach ($name in $configurations) {
        if (-not $installed.configurations.PSObject.Properties.Name.Contains($name)) {
            return $false
        }
        $build_directory = Join-Path $Destination ('out\skia-ui-{0}' -f $name.ToLowerInvariant())
        if (-not (Test-Path -LiteralPath (Join-Path $build_directory 'args.gn') -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

if ((Test-Installed) -and -not $Force) {
    Write-Output "Already installed: $Destination"
    Write-Output "  skia           : $($pin.skia_commit)"
    Write-Output "  configurations : $($configurations -join ', ')"
    Write-Output 'Give -Force to install it again.'
    return
}

# 자산 하나를 확보한다.
# 이미 받아 둔 것이 해시까지 맞으면 다시 받지 않는다.
#  - 진행 메시지는 Write-Host로 낸다. Write-Output으로 내면 반환 스트림에 섞여
#    호출자가 경로 대신 메시지까지 받는다.
function Resolve-Asset {
    param([string]$name)

    $asset = $pin.assets.$name
    New-Item -ItemType Directory -Force -Path $ArchiveDirectory | Out-Null
    $path = Join-Path $ArchiveDirectory $asset.file

    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if ((Get-FileHashText -path $path) -eq $asset.sha256.ToLowerInvariant()) {
            Write-Host "cached         : $($asset.file)"
            return $path
        }
        if ($Offline) {
            throw @"
The $name archive does not match the pinned hash, and -Offline forbids fetching it:
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
The $name archive was not found, and -Offline forbids fetching it:
  $path
Put the archive there (or pass -ArchiveDirectory) and run again.
"@
    }
    if (-not $pin.asset_base_url) {
        throw @"
The $name archive was not found and the pin file has no asset_base_url:
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
Failed to download the $name archive: $url
$($_.Exception.Message)

Fetch it with a browser and put it in: $ArchiveDirectory
"@
    }

    $actual = Get-FileHashText -path $path
    if ($actual -ne $asset.sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $path -Force
        throw @"
The $name archive does not match the pinned hash, so it was discarded.
  expected: $($asset.sha256)
  actual  : $actual
  url     : $url
"@
    }
    return $path
}

# 패키지가 핀이 말하는 그것인지 확인한다.
# 구성 둘을 합칠 때 서로 다른 Skia에서 나온 것이 섞이면
# 헤더 하나에 라이브러리 둘이 어긋나는 조용한 고장이 된다.
function Test-Package {
    param([string]$staging, [string]$name)

    $version_file = Join-Path $staging 'VERSION.json'
    if (-not (Test-Path -LiteralPath $version_file -PathType Leaf)) {
        throw "The $name archive has no VERSION.json - it is not a skia-prep package."
    }
    $version = Get-Content -Raw -LiteralPath $version_file | ConvertFrom-Json
    foreach ($check in @(
            @{ label = 'skia commit'; actual = $version.skia.commit; expected = $pin.skia_commit },
            @{ label = 'png codec'; actual = $version.png_codec; expected = $pin.png_codec },
            @{ label = 'target'; actual = $version.target; expected = $pin.target })) {
        if ($check.expected -and $check.actual -ne $check.expected) {
            throw @"
The $name archive does not match the pin file ($($check.label)):
  expected: $($check.expected)
  actual  : $($check.actual)
"@
        }
    }
    if (-not $version.configurations.PSObject.Properties.Name.Contains($name)) {
        throw "The $name archive does not contain a $name build."
    }
    return $version
}

Write-Output "Pin file       : $PinFile"
Write-Output "skia           : $($pin.skia_commit)"
Write-Output "png codec      : $($pin.png_codec)"
Write-Output "Configurations : $($configurations -join ', ')"
Write-Output "Destination    : $Destination"

$staging_root = Join-Path $repository_root 'build\skia-prep-staging'
if (Test-Path -LiteralPath $staging_root) {
    Remove-Item -LiteralPath $staging_root -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $staging_root | Out-Null

if (Test-Path -LiteralPath $Destination) {
    Remove-Item -LiteralPath $Destination -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

# 구성마다 다른 zip이지만 헤더·고지는 같은 것이 들어 있다.
# 공통 부분은 처음 한 번만 옮기고, 그 뒤로는 out/ 만 더한다.
$shared_installed = $false
$configuration_records = [ordered]@{}
$packaged_at = ''
$patches = @()
$include_vendor_headers = $false
foreach ($name in $configurations) {
    $archive = Resolve-Asset -name $name
    $staging = Join-Path $staging_root $name
    Expand-Archive -LiteralPath $archive -DestinationPath $staging -Force
    $version = Test-Package -staging $staging -name $name

    if (-not $shared_installed) {
        foreach ($item in Get-ChildItem -LiteralPath $staging -Force) {
            if ($item.PSIsContainer -and $item.Name -eq 'out') {
                continue
            }
            Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
        }
        $shared_installed = $true
        $packaged_at = $version.packaged_at
        $patches = $version.skia.patches
        $include_vendor_headers = $version.include_vendor_headers
    }

    $build_name = 'skia-ui-{0}' -f $name.ToLowerInvariant()
    New-Item -ItemType Directory -Force -Path (Join-Path $Destination 'out') | Out-Null
    Copy-Item -LiteralPath (Join-Path $staging "out\$build_name") `
        -Destination (Join-Path $Destination "out\$build_name") -Recurse -Force
    $configuration_records[$name] = $version.configurations.$name
    Write-Output "installed      : $name"
}

# 합쳐 놓은 것을 그대로 말하는 VERSION.json을 다시 쓴다.
# zip마다 들어 있던 것은 자기 구성 하나만 알고 있다.
$merged = [ordered]@{
    schema                 = 1
    packaged_at            = $packaged_at
    installed_at           = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    skia                   = [ordered]@{
        commit    = $pin.skia_commit
        milestone = $pin.skia_milestone
        patches   = $patches
    }
    png_codec              = $pin.png_codec
    target                 = $pin.target
    include_vendor_headers = [bool]$include_vendor_headers
    configurations         = $configuration_records
}
Set-Content -LiteralPath (Join-Path $Destination 'VERSION.json') `
    -Value ($merged | ConvertTo-Json -Depth 8) -Encoding UTF8

Remove-Item -LiteralPath $staging_root -Recurse -Force

$size = (Get-ChildItem -LiteralPath $Destination -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Output ''
Write-Output ('Skia package ready: {0} ({1:N1} MB)' -f $Destination, ($size / 1MB))
Write-Output 'Configure luil next - it checks the package on its own:'
Write-Output '  cmake --preset vs2026-tests'
