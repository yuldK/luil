# 배포된 Skia 패키지를 받아 third_party/skia-prep에 설치한다.
#
# luil를 빌드하는 데 필요한 Skia는 이것으로 끝난다. Skia 소스도, external
# submodule도, gn·ninja·bazelisk도 필요 없다 — 패키지가 헤더와 정적 라이브러리와
# 고지를 이미 담고 있다 (skia-prep의 pack_skia.ps1이 만든다).
#
# 받을 것은 third_party/skia-prep.json이 정한다. 그 파일이 판번을 고정하는
# 자리이며, 자산마다 SHA-256을 함께 담는다 — 값이 다르면 설치하지 않는다.
#
# 판번은 Skia commit 하나가 아니다. 같은 commit을 다른 도구사슬로 다시 패키징한
# 것이 따로 있으므로(clang-cl로 세운 r2가 그것이다), 핀은 패키지 판번과 도구사슬을
# 함께 적는다. 설치 생략 판정도 그것을 본다.
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

function Get-FileHashText {
    param([string]$path)

    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# manifest에 없는 것을 물어도 $null로 답한다.
# StrictMode에서 없는 속성을 읽으면 예외가 난다. 판번이 다른 설치본
# (도구사슬을 적지 않던 r1)을 "다르다"로 판정하려면 이 자리가 필요하다 —
# 예외로 죽는 것과 다시 설치하는 것은 다른 일이다.
function Get-ManifestValue {
    param($manifest, [string]$path)

    $current = $manifest
    foreach ($segment in $path.Split('.')) {
        if ($null -eq $current -or
            @($current.PSObject.Properties.Name) -notcontains $segment) {
            return $null
        }
        $current = $current.$segment
    }
    return $current
}

foreach ($field in @('skia_commit', 'png_codec', 'target', 'package_revision', 'toolchain')) {
    if ($null -eq (Get-ManifestValue -manifest $pin -path $field)) {
        throw @"
The pin file has no ${field}: $PinFile
A pin has to name the package revision and the toolchain, not just the Skia
commit - the same commit gets packaged more than once. See docs/skia-build.md.
"@
    }
}

foreach ($name in $configurations) {
    if (@($pin.assets.PSObject.Properties.Name) -notcontains $name) {
        throw @"
The pin file has no $name asset: $PinFile
Available: $($pin.assets.PSObject.Properties.Name -join ', ')
"@
    }
}

# 핀이 요구하는 것과 manifest가 말하는 것을 맞대는 목록이다.
# 설치 생략 판정과 아카이브 검사가 같은 목록을 본다 — 한쪽이 통과시킨 것을
# 다른 쪽이 막는 어긋남이 생기지 않는다.
#
# skia commit만으로는 갈리지 않는다. 같은 commit을 다른 도구사슬로 다시
# 패키징한 것이 따로 있고(clang-cl로 세운 r2), 그것은 성능이 다른 물건이다.
# 패키지 판번과 도구사슬이 그 둘을 가른다.
function Get-PackageIdentity {
    param($manifest)

    return @(
        @{ label = 'skia commit'; expected = $pin.skia_commit
            actual = (Get-ManifestValue -manifest $manifest -path 'skia.commit') },
        @{ label = 'package revision'; expected = $pin.package_revision
            actual = (Get-ManifestValue -manifest $manifest -path 'package_revision') },
        @{ label = 'png codec'; expected = $pin.png_codec
            actual = (Get-ManifestValue -manifest $manifest -path 'png_codec') },
        @{ label = 'target'; expected = $pin.target
            actual = (Get-ManifestValue -manifest $manifest -path 'target') },
        @{ label = 'toolchain'; expected = $pin.toolchain
            actual = (Get-ManifestValue -manifest $manifest -path 'toolchain.compiler') })
}

# 핀이 말하는 바로 그것이 이미 설치되어 있는지 본다.
# 판정의 근거는 설치본이 스스로 적어 둔 VERSION.json이다.
#
# skia commit과 구성 이름만 보던 자리다. 그것으로는 같은 commit을 다시
# 패키징한 것을 건너뛴다 — r1(MSVC)이 깔린 기계에서 r2(clang-cl)를 핀에
# 걸어도 "이미 설치됨"이 되고, 링크는 되는데 래스터가 59배 느린 물건이
# 그대로 남는다.
#
# 그래서 셋을 본다.
#  1. 패키지 식별자다 — commit·판번·코덱·대상·도구사슬 (Get-PackageIdentity).
#  2. 구성마다, 그 구성을 설치한 아카이브의 SHA-256이 핀의 것과 같은가.
#     Resolve-Asset이 아카이브를 그 해시로 검사하므로 이것이 곧 내용의 동일성이다.
#  3. 구성마다, 산출물이 그대로 있는가. args.gn은 구성 계약이라 체크섬까지 보고
#     라이브러리는 크기까지 본다 — 700 MB를 다시 해싱하지 않고도 잘려 나간
#     설치가 걸린다.
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
    foreach ($check in (Get-PackageIdentity -manifest $installed)) {
        if ($check.actual -ne $check.expected) {
            return $false
        }
    }
    foreach ($name in $configurations) {
        $record = Get-ManifestValue -manifest $installed -path "configurations.$name"
        if ($null -eq $record) {
            return $false
        }
        if ((Get-ManifestValue -manifest $record -path 'archive_sha256') -ne
            $pin.assets.$name.sha256.ToLowerInvariant()) {
            return $false
        }

        $build_directory = Join-Path $Destination ('out\skia-ui-{0}' -f $name.ToLowerInvariant())
        $arguments_file = Join-Path $build_directory 'args.gn'
        if (-not (Test-Path -LiteralPath $arguments_file -PathType Leaf)) {
            return $false
        }
        if ((Get-FileHashText -path $arguments_file) -ne
            (Get-ManifestValue -manifest $record -path 'args_sha256')) {
            return $false
        }

        $files = Get-ManifestValue -manifest $record -path 'files'
        if ($null -eq $files) {
            return $false
        }
        foreach ($entry in $files.PSObject.Properties) {
            $installed_file = Get-Item -ErrorAction Ignore `
                -LiteralPath (Join-Path $build_directory $entry.Name)
            if ($null -eq $installed_file -or $installed_file.Length -ne
                (Get-ManifestValue -manifest $entry.Value -path 'size')) {
                return $false
            }
        }
    }
    return $true
}

if ((Test-Installed) -and -not $Force) {
    Write-Output "Already installed: $Destination"
    Write-Output "  skia           : $($pin.skia_commit)"
    Write-Output "  package        : r$($pin.package_revision) ($($pin.toolchain))"
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
# 도구사슬이 갈리는 것도 같은 종류다 — 같은 commit이라도 무엇이 컴파일했는가에
# 따라 성능이 다른 물건이고, args.gn만으로는 그것이 드러나지 않는다.
function Test-Package {
    param([string]$staging, [string]$name)

    $version_file = Join-Path $staging 'VERSION.json'
    if (-not (Test-Path -LiteralPath $version_file -PathType Leaf)) {
        throw "The $name archive has no VERSION.json - it is not a skia-prep package."
    }
    $version = Get-Content -Raw -LiteralPath $version_file | ConvertFrom-Json
    foreach ($check in (Get-PackageIdentity -manifest $version)) {
        if ($check.actual -ne $check.expected) {
            $actual_text = if ($null -eq $check.actual) { '(absent)' } else { $check.actual }
            throw @"
The $name archive does not match the pin file ($($check.label)):
  expected: $($check.expected)
  actual  : $actual_text
"@
        }
    }
    if (@($version.configurations.PSObject.Properties.Name) -notcontains $name) {
        throw "The $name archive does not contain a $name build."
    }
    return $version
}

Write-Output "Pin file       : $PinFile"
Write-Output "skia           : $($pin.skia_commit)"
Write-Output "package        : r$($pin.package_revision) ($($pin.toolchain))"
Write-Output "png codec      : $($pin.png_codec)"
Write-Output "Configurations : $($configurations -join ', ')"
Write-Output "Destination    : $Destination"

# 아카이브를 먼저 모두 확보한다.
# 설치본을 지우는 것은 그 뒤다 — 받지 못할 것을 미리 알면 이미 서 있는
# 패키지를 잃지 않는다. 설치 생략 판정이 엄격해진 만큼 이 자리를 지나는 일이
# 잦아졌고, 실패가 "Skia가 아예 없는 트리"로 끝나서는 안 된다.
$archives = [ordered]@{}
foreach ($name in $configurations) {
    $archives[$name] = Resolve-Asset -name $name
}

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
$toolchain = $null
$include_vendor_headers = $false
foreach ($name in $configurations) {
    $staging = Join-Path $staging_root $name
    Expand-Archive -LiteralPath $archives[$name] -DestinationPath $staging -Force
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
        $toolchain = $version.toolchain
        $include_vendor_headers = $version.include_vendor_headers
    }

    $build_name = 'skia-ui-{0}' -f $name.ToLowerInvariant()
    New-Item -ItemType Directory -Force -Path (Join-Path $Destination 'out') | Out-Null
    Copy-Item -LiteralPath (Join-Path $staging "out\$build_name") `
        -Destination (Join-Path $Destination "out\$build_name") -Recurse -Force
    # 이 구성을 어느 아카이브가 설치했는지 함께 적는다.
    # 다음 실행의 설치 생략 판정이 그 값을 핀과 맞댄다. Resolve-Asset이 이미
    # 그 해시로 아카이브를 검사했으므로 다시 재지 않는다.
    $record = [ordered]@{
        archive        = $pin.assets.$name.file
        archive_sha256 = $pin.assets.$name.sha256.ToLowerInvariant()
    }
    foreach ($entry in $version.configurations.$name.PSObject.Properties) {
        $record[$entry.Name] = $entry.Value
    }
    $configuration_records[$name] = $record
    Write-Output "installed      : $name"
}

# 합쳐 놓은 것을 그대로 말하는 VERSION.json을 다시 쓴다.
# zip마다 들어 있던 것은 자기 구성 하나만 알고 있다.
$merged = [ordered]@{
    schema                 = 2
    package_revision       = $pin.package_revision
    packaged_at            = $packaged_at
    installed_at           = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    skia                   = [ordered]@{
        commit    = $pin.skia_commit
        milestone = $pin.skia_milestone
        patches   = $patches
    }
    png_codec              = $pin.png_codec
    toolchain              = $toolchain
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
