param(
    [switch]$Apply,
    [int]$ParentPid = 0,
    [string]$ExpectedVersion,
    [string]$ExpectedSha256
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
# Windows PowerShell may still select TLS 1.0 by default.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

function Get-HttpsUri([string]$Text) {
    $uri = $null
    if (![Uri]::TryCreate($Text, [UriKind]::Absolute, [ref]$uri) -or
        $uri.Scheme -ne 'https' -or !$uri.Host -or $uri.UserInfo) {
        throw 'Nguon tai cap nhat can URL HTTPS khong chua thong tin dang nhap.'
    }
    $uri
}
function Get-Sha256Hex([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToUpperInvariant()
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}
$stage = $null
try {
    $config = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $PSScriptRoot 'update-feed.json') | ConvertFrom-Json
    $signer = ([string]$config.publisherSignerSha1).Trim()
    if ($signer -and $signer -notmatch '^[A-Fa-f0-9]{40}$') {
        throw 'Ma chung chi nha phat hanh trong cau hinh khong hop le.'
    }
    $localAppData = [Environment]::GetFolderPath('LocalApplicationData')
    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT -and $env:LOCALAPPDATA) {
        $localAppData = $env:LOCALAPPDATA
    }
    $userDirectory = Join-Path $localAppData 'XuanDieuCNC/ArtisanAxis'
    $userFile = Join-Path $userDirectory 'update_config.json'
    $source = 'default'
    $sourceValue = ''
    $keepBackup = $false
    if (Test-Path -LiteralPath $userFile) {
        $custom = Get-Content -Raw -Encoding UTF8 -LiteralPath $userFile | ConvertFrom-Json
        $source = [string]$custom.source
        $sourceValue = ([string]$custom.value).Trim()
        if ($custom.keepBackup -is [bool]) { $keepBackup = $custom.keepBackup }
    } else {
        $legacyFile = Join-Path $userDirectory 'updates.json'
        if (Test-Path -LiteralPath $legacyFile) {
            $old = Get-Content -Raw -Encoding UTF8 -LiteralPath $legacyFile | ConvertFrom-Json
            if ($old.googleSheetCsv) {
                $source = 'sheets'
                $sourceValue = ([string]$old.googleSheetCsv).Trim()
            } elseif ($old.PSObject.Properties.Name -contains 'googleSheetCsv' -and $config.githubRepository) {
                $source = 'github'
                $sourceValue = [string]$config.githubRepository
            }
        }
    }

    if ($source -eq 'default') {
        if ($config.googleSheetCsv) {
            $source = 'sheets'
            $sourceValue = [string]$config.googleSheetCsv
        } elseif ($config.githubRepository) {
            $source = 'github'
            $sourceValue = [string]$config.githubRepository
        } else { throw 'Chua cau hinh kenh cap nhat.' }
    }
    $notes = ''
    $localSetup = $null
    $download = $null
    switch ($source) {
        'sheets' {
            $sheet = Get-HttpsUri $sourceValue
            if ($sheet.Host -ne 'docs.google.com') {
                throw 'URL Google Sheet can la HTTPS tren docs.google.com.'
            }
            $rows = @(((Invoke-WebRequest -Uri $sheet -UseBasicParsing -TimeoutSec 30).Content | ConvertFrom-Csv) |
                Where-Object { $_.channel -ceq 'stable' })
            if ($rows.Count -ne 1) { throw 'CSV can co mot dong stable.' }
            $versionText = [string]$rows[0].version
            $download = [string]$rows[0].url
            $hash = [string]$rows[0].sha256
            $notes = [string]$rows[0].notes
        }
        'github' {
            if ($sourceValue -notmatch '^[\w.-]+/[\w.-]+$') { throw 'Ten kho GitHub khong hop le.' }
            $base = "https://api.github.com/repos/$sourceValue/releases/latest"
            $release = Invoke-RestMethod -Uri $base -Headers @{'Accept'='application/vnd.github+json'} -TimeoutSec 30
            $setupAssets = @($release.assets | Where-Object { $_.name -ceq 'Setup.exe' })
            $feedAssets = @($release.assets | Where-Object { $_.name -ceq 'update-feed.json' })
            if ($setupAssets.Count -ne 1 -or $feedAssets.Count -ne 1) {
                throw 'GitHub Release can co Setup.exe va update-feed.json.'
            }
            $versionText = ([string]$release.tag_name).TrimStart('v')
            $download = [string]$setupAssets[0].browser_download_url
            $feedUrl = Get-HttpsUri ([string]$feedAssets[0].browser_download_url)
            $setupUrl = Get-HttpsUri $download
            if ($feedUrl.Host -ne 'github.com' -or $setupUrl.Host -ne 'github.com') {
                throw 'Asset GitHub khong hop le.'
            }
            $feed = Invoke-RestMethod -Uri $feedUrl -TimeoutSec 30
            if ($feed.channel -cne 'stable' -or $feed.version -ne $versionText -or $feed.url -cne $download) {
                throw 'Thong tin manifest khong khop voi GitHub Release.'
            }
            $hash = [string]$feed.sha256
            $apiDigest = [string]$setupAssets[0].digest
            if ($apiDigest -and $apiDigest -ne "sha256:$hash") { throw 'SHA256 GitHub va manifest khong khop.' }
            $notes = [string]$feed.notes
            if (!$notes) { $notes = [string]$release.body }
        }
        'manifest' {
            $feed = Invoke-RestMethod -Uri (Get-HttpsUri $sourceValue) -TimeoutSec 30
            if ($feed.channel -cne 'stable') { throw 'Manifest can khai bao kenh stable.' }
            $versionText = [string]$feed.version
            $download = [string]$feed.url
            $hash = [string]$feed.sha256
            $notes = [string]$feed.notes
        }
        'local' {
            if (([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT -and
                    $sourceValue -notmatch '^([A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)') -or
                ![IO.Path]::IsPathRooted($sourceValue) -or !(Test-Path -LiteralPath $sourceValue -PathType Container)) {
                throw 'Can chon thu muc goi cap nhat ton tai va duong dan day du.'
            }
            $localSetup = Join-Path $sourceValue 'Setup.exe'
            $feedPath = Join-Path $sourceValue 'update-feed.json'
            if (!(Test-Path -LiteralPath $localSetup -PathType Leaf) -or !(Test-Path -LiteralPath $feedPath -PathType Leaf)) {
                throw 'Thu muc can chua Setup.exe va update-feed.json.'
            }
            $feed = Get-Content -Raw -Encoding UTF8 -LiteralPath $feedPath | ConvertFrom-Json
            if ($feed.channel -cne 'stable') { throw 'Manifest can khai bao kenh stable.' }
            $versionText = [string]$feed.version
            $hash = [string]$feed.sha256
            $notes = [string]$feed.notes
        }
        default { throw 'Loai nguon cap nhat khong hop le.' }
    }
    if ($versionText -notmatch '^\d+\.\d+\.\d+$' -or $hash -notmatch '^[A-Fa-f0-9]{64}$') {
        throw 'Phien ban hoac SHA256 khong hop le.'
    }
    if (!$localSetup) { $url = Get-HttpsUri $download }
   $installed = [version](Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.txt') -TotalCount 1)
    $version = [version]$versionText
    if ($version -le $installed) {
        @{ok=$true; available=$false} | ConvertTo-Json -Compress
        exit 0
    }
    $notes = $notes.Trim()
    if ($notes.Length -gt 12000) { $notes = $notes.Substring(0,12000) + ' ...' }
    if (!$Apply) {
   if ($localSetup -and (Get-Sha256Hex $localSetup) -ne $hash) {
            throw 'SHA256 bo cai trong thu muc khong khop.'
        }
        @{ok=$true; available=$true; version=$versionText; notes=$notes; sha256=$hash} | ConvertTo-Json -Compress
        exit 0
    }
    if (($ExpectedVersion -or $ExpectedSha256) -and ($ExpectedVersion -cne $versionText -or $ExpectedSha256 -ne $hash)) {
        throw 'Goi cap nhat da doi. Hay kiem tra lai va xem noi dung ban moi.'
    }
    if ($ParentPid -le 0) { throw 'Thieu tien trinh ArtisanAxis.' }
    $app = Split-Path -Parent $PSScriptRoot
    $exe = Join-Path $app 'ArtisanAxis.exe'
    $parent = Get-Process -Id $ParentPid -ErrorAction Stop
    if ($parent.Path -ne $exe) { throw 'Tien trinh goi cap nhat khong khop thu muc cai.' }

    $stage = Join-Path ([IO.Path]::GetTempPath()) ('ArtisanAxis-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    $setup = Join-Path $stage 'Setup.exe'
    if ($localSetup) {
        Copy-Item -LiteralPath $localSetup -Destination $setup
    } else {
        Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 300 -OutFile $setup
    }
 if ((Get-Sha256Hex $setup) -ne $hash) { throw 'SHA256 bo cai khong khop.' }
    if ($signer) {
        $sig = Get-AuthenticodeSignature -LiteralPath $setup
        if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne $signer) {
            throw 'Chu ky bo cai hoac chung chi nha phat hanh khong khop.'
        }
    }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($setup)
    if ($info.ProductName -ne 'ArtisanAxis CAM Pro' -or $info.FileMajorPart -ne $version.Major -or
        $info.FileMinorPart -ne $version.Minor -or $info.FileBuildPart -ne $version.Build) {
        throw 'San pham hoac phien ban bo cai khong khop.'
    }

    $helper = Join-Path $stage 'Install-Update.ps1'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-Update.ps1') -Destination $helper
    @{
        waitPid=$ParentPid; app=$app; sha256=$hash; signer=$signer; keepBackup=$keepBackup;
        targetVersion=$versionText; previousVersion=$installed.ToString()
    } | ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $stage 'transaction.json') -Encoding UTF8
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -WindowStyle Hidden -ArgumentList (
        '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + $helper + '"'
    )
    $stage = $null
    @{ok=$true; available=$true; ready=$true; version=$versionText} | ConvertTo-Json -Compress
} catch {
    @{ok=$false; error=$_.Exception.Message} | ConvertTo-Json -Compress
    exit 1
} finally {
    if ($stage) { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
}
