param([switch]$Apply, [int]$ParentPid = 0)
$ErrorActionPreference = 'Stop'
$stage = $null
try {
    $config = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'update-feed.json') | ConvertFrom-Json
    $signer = ([string]$config.publisherSignerSha1).Trim()
    if ($signer -and $signer -notmatch '^[A-Fa-f0-9]{40}$') {
        throw 'Ma chung chi nha phat hanh trong cau hinh khong hop le.'
    }
    $userDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'XuanDieuCNC\ArtisanAxis'
    $userFile = Join-Path $userDirectory 'updates.json'
    $sheetText = [string]$config.googleSheetCsv
    if (Test-Path -LiteralPath $userFile) {
        $custom = Get-Content -Raw -LiteralPath $userFile | ConvertFrom-Json
        if ($custom.PSObject.Properties.Name -contains 'googleSheetCsv') { $sheetText = [string]$custom.googleSheetCsv }
    }

    if ($sheetText) {
        $sheet = [Uri]$sheetText
        if ($sheet.Scheme -ne 'https' -or $sheet.Host -ne 'docs.google.com' -or $sheet.UserInfo) {
            throw 'URL Google Sheet can la HTTPS tren docs.google.com.'
        }
        $rows = @(((Invoke-WebRequest -Uri $sheet -UseBasicParsing -TimeoutSec 30).Content | ConvertFrom-Csv) |
            Where-Object { $_.channel -ceq 'stable' })
        if ($rows.Count -ne 1) { throw 'CSV can co mot dong stable.' }
        $versionText = [string]$rows[0].version
        $download = [string]$rows[0].url
        $hash = [string]$rows[0].sha256
    } elseif ($config.githubRepository) {
        if ($config.githubRepository -notmatch '^[\w.-]+/[\w.-]+$') { throw 'Ten kho GitHub khong hop le.' }
        $base = "https://api.github.com/repos/$($config.githubRepository)/releases/latest"
        $release = Invoke-RestMethod -Uri $base -Headers @{'Accept'='application/vnd.github+json'} -TimeoutSec 30
        $setupAssets = @($release.assets | Where-Object { $_.name -ceq 'Setup.exe' })
        $feedAssets = @($release.assets | Where-Object { $_.name -ceq 'update-feed.json' })
        if ($setupAssets.Count -ne 1 -or $feedAssets.Count -ne 1) {
            throw 'GitHub Release can co Setup.exe va update-feed.json.'
        }
        $versionText = ([string]$release.tag_name).TrimStart('v')
        $download = [string]$setupAssets[0].browser_download_url
        $feedUrl = [Uri]$feedAssets[0].browser_download_url
        $setupUrl = [Uri]$download
        if ($feedUrl.Scheme -ne 'https' -or $feedUrl.Host -ne 'github.com' -or $feedUrl.UserInfo -or
            $setupUrl.Scheme -ne 'https' -or $setupUrl.Host -ne 'github.com' -or $setupUrl.UserInfo) {
            throw 'Asset GitHub khong hop le.'
        }
        $feed = Invoke-RestMethod -Uri $feedUrl -TimeoutSec 30
        if ($feed.channel -cne 'stable' -or $feed.version -ne $versionText -or $feed.url -cne $download) {
            throw 'Thong tin manifest khong khop voi GitHub Release.'
        }
        $hash = [string]$feed.sha256
        $apiDigest = [string]$setupAssets[0].digest
        if ($apiDigest -and $apiDigest -ne "sha256:$hash") { throw 'SHA256 GitHub va manifest khong khop.' }
    } else { throw 'Chua cau hinh kenh cap nhat.' }

    if ($versionText -notmatch '^\d+\.\d+\.\d+$' -or $hash -notmatch '^[A-Fa-f0-9]{64}$') {
        throw 'Phien ban hoac SHA256 khong hop le.'
    }
    $url = [Uri]$download
    if ($url.Scheme -ne 'https' -or $url.UserInfo) { throw 'Tep cai dat can URL HTTPS.' }
    $installed = [version](Get-Content -LiteralPath (Join-Path $PSScriptRoot 'version.txt') -TotalCount 1)
    $version = [version]$versionText
    if ($version -le $installed) {
        @{ok=$true; available=$false} | ConvertTo-Json -Compress
        exit 0
    }
    if (!$Apply) {
        @{ok=$true; available=$true; version=$versionText} | ConvertTo-Json -Compress
        exit 0
    }
    if ($ParentPid -le 0) { throw 'Thieu tien trinh ArtisanAxis.' }
    $app = Split-Path -Parent $PSScriptRoot
    $exe = Join-Path $app 'ArtisanAxis.exe'
    $parent = Get-Process -Id $ParentPid -ErrorAction Stop
    if ($parent.Path -ne $exe) { throw 'Tien trinh goi cap nhat khong khop thu muc cai.' }

    $stage = Join-Path ([IO.Path]::GetTempPath()) ('ArtisanAxis-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    $setup = Join-Path $stage 'Setup.exe'
    Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 300 -OutFile $setup
    if ((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash -ne $hash) { throw 'SHA256 bo cai khong khop.' }
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
        waitPid=$ParentPid; app=$app; sha256=$hash; signer=$signer;
        targetVersion=$versionText; previousVersion=$installed.ToString()
    } | ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $stage 'transaction.json') -Encoding UTF8
    Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -WindowStyle Hidden -ArgumentList (
        '-NoProfile -NonInteractive -File "' + $helper + '"'
    )
    $stage = $null
    @{ok=$true; available=$true; ready=$true; version=$versionText} | ConvertTo-Json -Compress
} catch {
    @{ok=$false; error=$_.Exception.Message} | ConvertTo-Json -Compress
    exit 1
} finally {
    if ($stage) { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
}
