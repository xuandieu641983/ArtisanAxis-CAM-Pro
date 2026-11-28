param([string]$Source = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('ArtisanAxis-UpdateQA-' + [Guid]::NewGuid().ToString('N'))
$oldLocal = $env:LOCALAPPDATA
$oldXdg = $env:XDG_DATA_HOME
$utf8 = [Text.UTF8Encoding]::new($false)
$passed = 0

function Save-Json([string]$Path, [hashtable]$Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 5), $utf8)
}
function Run-Update {
    $stdout = & $driver @args
    $result = $stdout | ConvertFrom-Json
    if ($null -eq $result.ok) { throw "Invalid protocol: $stdout" }
    $result
}
function Test-Case([bool]$Condition, [string]$Name) {
    if (!$Condition) { throw "FAILED: $Name" }
    $script:passed++
    Write-Output "PASS $Name"
}
function Invoke-RestMethod {
    param($Uri, $Headers, $TimeoutSec)
    switch ([string]$Uri) {
        'https://api.github.com/repos/shop/artisanaxis/releases/latest' {
            return @{
                tag_name='v1.3.0'; body='Release notes from GitHub';
                assets=@(
                    @{name='Setup.exe'; digest="sha256:$fixtureHash"; browser_download_url=$fixtureDownload},
                    @{name='update-feed.json'; browser_download_url='https://github.com/shop/artisanaxis/releases/download/v1.3.0/update-feed.json'}
                )
            }
        }
        'https://github.com/shop/artisanaxis/releases/download/v1.3.0/update-feed.json' {
            return @{channel='stable';version='1.3.0';url=$fixtureDownload;sha256=$fixtureHash}
        }
        'https://updates.example.invalid/update-feed.json' {
            return @{channel='stable';version='1.3.0';url=$fixtureDownload;sha256=$fixtureHash;notes='Manifest release notes'}
        }
        default { throw "Unexpected HTTP request: $Uri" }
    }
}
function Invoke-WebRequest {
    param($Uri, [switch]$UseBasicParsing, $TimeoutSec, $OutFile)
    if ($OutFile -or [string]$Uri -ne 'https://docs.google.com/spreadsheets/d/demo') {
        throw "Unexpected request: $Uri"
    }
    @{Content="channel,version,url,sha256,notes`nstable,1.3.0,$fixtureDownload,$fixtureHash,CSV release notes"}
}

try {
    $env:LOCALAPPDATA = $root
    $env:XDG_DATA_HOME = $root
    $user = Join-Path $root 'XuanDieuCNC/ArtisanAxis'
    $scripts = Join-Path $root 'scripts'
    $packages = Join-Path $root ('packages-' + [char]0x0111)
    $null = New-Item -ItemType Directory -Path $user,$scripts,$packages -Force
    Copy-Item -LiteralPath (Join-Path $Source 'scripts/Update.ps1') -Destination $scripts
    $driver = Join-Path $scripts 'Update.ps1'
    foreach ($file in @($driver, (Join-Path $Source 'scripts/Install-Update.ps1'))) {
        $tokens = $null
        $errors = $null
        $null = [System.Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$errors)
        if ($errors) { throw ($errors | Out-String) }
    }
    $trusted = Join-Path $scripts 'update-feed.json'
    $settings = Join-Path $user 'update_config.json'
    $versionFile = Join-Path $scripts 'version.txt'
    $installer = Join-Path $packages 'Setup.exe'
    $feedPath = Join-Path $packages 'update-feed.json'
    [IO.File]::WriteAllText($versionFile, "1.2.0`n", $utf8)
    [IO.File]::WriteAllText($installer, "Fixture bytes", $utf8)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
$stream = [System.IO.File]::OpenRead($installer)
try {
    $fixtureHash = ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToUpperInvariant()
} finally {
    $stream.Dispose()
    $sha256.Dispose()
}
    $fixtureDownload = 'https://github.com/shop/artisanaxis/releases/download/v1.3.0/Setup.exe'

    Save-Json $trusted @{githubRepository='shop/artisanaxis';publisherSignerSha1=''}
    $r = Run-Update
    Test-Case ($r.ok -and $r.available -and $r.notes -ceq 'Release notes from GitHub') 'default GitHub feed with notes'

    Save-Json $settings @{source='sheets';value='https://docs.google.com/spreadsheets/d/demo'}
    $r = Run-Update
    Test-Case ($r.ok -and $r.available -and $r.notes -ceq 'CSV release notes') 'Sheets feed with notes'

    Save-Json $settings @{source='manifest';value='https://updates.example.invalid/update-feed.json'}
    $r = Run-Update
    Test-Case ($r.ok -and $r.available -and $r.notes -ceq 'Manifest release notes') 'HTTPS manifest with notes'

    Save-Json $settings @{source='manifest';value='http://updates.example.invalid/update-feed.json'}
    $r = Run-Update
    Test-Case (!$r.ok -and $r.error -match 'HTTPS') 'rejects cleartext source before network'

    Save-Json $settings @{source='local';value=$packages;keepBackup=$true;autoCheck=$false}
    Save-Json $feedPath @{channel='stable';version='1.3.0';sha256=$fixtureHash;notes='Local release notes'}
    $r = Run-Update
    Test-Case ($r.ok -and $r.available -and $r.notes -ceq 'Local release notes' -and $r.sha256 -eq $fixtureHash) 'local UTF-8 folder and verified SHA-256'

    $r = Run-Update -Apply -ExpectedVersion '1.2.9' -ExpectedSha256 $fixtureHash
    Test-Case (!$r.ok -and $r.error -match 'da doi') 'rejects changed release before install preparation'

    Save-Json $feedPath @{channel='stable';version='1.2.0';sha256=$fixtureHash}
    $r = Run-Update
    Test-Case ($r.ok -and !$r.available) 'equal or older version unavailable'

    Save-Json $feedPath @{channel='stable';version='1.3.0';sha256=('0' * 64)}
    $r = Run-Update
    Test-Case (!$r.ok -and $r.error -match 'SHA256') 'rejects mismatched local package hash'

    Save-Json $trusted @{publisherSignerSha1='invalid pin'}
    Save-Json $settings @{source='local';value=$packages;publisherSignerSha1=''}
    $r = Run-Update
    Test-Case (!$r.ok -and $r.error -match 'chung chi') 'user config cannot override installed signer pin'

    Write-Output "$passed updater cases passed"
} finally {
    $env:LOCALAPPDATA = $oldLocal
    $env:XDG_DATA_HOME = $oldXdg
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
