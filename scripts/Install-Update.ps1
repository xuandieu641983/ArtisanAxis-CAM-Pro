$ErrorActionPreference = 'Stop'
$stage = $PSScriptRoot
$tx = Get-Content -LiteralPath (Join-Path $stage 'transaction.json') -Raw | ConvertFrom-Json
$app = [IO.Path]::GetFullPath([string]$tx.app).TrimEnd('\')
$exe = Join-Path $app 'ArtisanAxis.exe'
$setup = Join-Path $stage 'Setup.exe'
$keepBackup = $tx.keepBackup -is [bool] -and $tx.keepBackup
$previousVersion = [version]$tx.previousVersion
$backupLabel = if ($keepBackup) { '.backup-v' + $previousVersion.ToString() + '-' } else { '.rollback-' }
$rollback = $app + $backupLabel + [Guid]::NewGuid().ToString('N')
$failed = $app + '.failed-' + [Guid]::NewGuid().ToString('N')
$meta = Join-Path $stage 'uninstall.reg'
$keyName = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\ArtisanAxis.XuanDieuCNC_is1'
$key = 'HKCU:\' + $keyName
$nativeKey = 'HKCU\' + $keyName
$logDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'XuanDieuCNC\ArtisanAxis\UpdateLogs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$log = Join-Path $logDirectory ('update-' + [Guid]::NewGuid().ToString('N') + '.log')
$installLog = [IO.Path]::ChangeExtension($log, '.setup.log')
$hash = [Security.Cryptography.SHA256]::Create()
try { $id = [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($app.ToLowerInvariant()))).Replace('-', '') }
finally { $hash.Dispose() }
$mutex = [Threading.Mutex]::new($false, ('Local\ArtisanAxis.Update.' + $id))
$lock = $false
$committed = $false
$restartOld = $false
$newApp = $null

function Write-Log([string]$Message) {
    Add-Content -LiteralPath $log -Value ((Get-Date -Format o) + ' ' + $Message) -Encoding UTF8
}
function RunningIn([string]$Path) {
    @(Get-Process -Name 'ArtisanAxis','CAMWorker' -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $Path
    })
}

try {
    try { $lock = $mutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $lock = $true }
    if (!$lock) { throw 'Mot cap nhat khac dang chay.' }
    Wait-Process -Id ([int]$tx.waitPid) -Timeout 120 -ErrorAction SilentlyContinue
    if ((RunningIn $app).Count -gt 0) { throw 'Hay dong cac phien ArtisanAxis va CAMWorker truoc khi cap nhat.' }
    $restartOld = $true
    $installed = Get-ItemProperty -LiteralPath $key -Name InstallLocation -ErrorAction Stop
    if ([IO.Path]::GetFullPath($installed.InstallLocation).TrimEnd('\') -ne $app -or
        !(Test-Path -LiteralPath $exe) -or
        ((Get-Item -LiteralPath $app).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Cap nhat chi danh cho thu muc Inno Setup cua ArtisanAxis.'
    }
    $currentVersion = (Get-Content -LiteralPath (Join-Path $app 'scripts\version.txt') -TotalCount 1).Trim()
    if ([version]$currentVersion -ne [version]$tx.previousVersion -or [version]$tx.targetVersion -le [version]$currentVersion) {
        throw 'Phien ban da doi khi dang chuan bi cap nhat.'
    }
    if ([string]$tx.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or
        (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash -ne $tx.sha256) {
        throw 'SHA256 bo cai doi truoc khi cai dat.'
    }
    $config = Get-Content -Raw -LiteralPath (Join-Path $app 'scripts\update-feed.json') | ConvertFrom-Json
    $signer = ([string]$config.publisherSignerSha1).Trim()
    if ($signer -ne [string]$tx.signer -or ($signer -and $signer -notmatch '^[A-Fa-f0-9]{40}$')) {
        throw 'Cau hinh chung chi doi khi dang chuan bi cap nhat.'
    }
    if ($signer) {
        $sig = Get-AuthenticodeSignature -LiteralPath $setup
        if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne $signer) {
            throw 'Chu ky bo cai khong hop le truoc khi cai dat.'
        }
    }
    $version = [version]$tx.targetVersion
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($setup)
    if ($info.ProductName -ne 'ArtisanAxis CAM Pro' -or $info.FileMajorPart -ne $version.Major -or
        $info.FileMinorPart -ne $version.Minor -or $info.FileBuildPart -ne $version.Build) {
        throw 'San pham hoac phien ban bo cai khong khop truoc khi cai dat.'
    }
    & reg.exe export $nativeKey $meta /y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Khong sao luu duoc muc go cai dat.' }
    Move-Item -LiteralPath $app -Destination $rollback
    Write-Log "Backup: $rollback"
    if ($keepBackup) {
        Copy-Item -LiteralPath $meta -Destination (Join-Path $rollback 'ArtisanAxis-Uninstall-Backup.reg')
    }

    $setupArguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOCLOSEAPPLICATIONS','/NORESTARTAPPLICATIONS',
              ('/DIR="' + $app + '"'), ('/LOG="' + $installLog + '"'))
    $proc = Start-Process -FilePath $setup -ArgumentList $setupArguments -Wait -PassThru
    if ($proc.ExitCode -ne 0) { throw "Setup return code $($proc.ExitCode)" }
    $newVersion = (Get-Content -LiteralPath (Join-Path $app 'scripts\version.txt') -TotalCount 1).Trim()
    if ([version]$newVersion -ne [version]$tx.targetVersion) { throw 'Phien ban sau cai dat khong dung.' }

    if ($signer) {
        $installedConfig = Get-Content -Raw -LiteralPath (Join-Path $app 'scripts\update-feed.json') | ConvertFrom-Json
        if (([string]$installedConfig.publisherSignerSha1).Trim() -ne $signer) {
            throw 'Ban moi khong giu cau hinh chung chi da ghim.'
        }
    }

    $health = Join-Path $stage 'startup.ok'
    $token = [Guid]::NewGuid().ToString('N')
    $newApp = Start-Process -FilePath $exe -WorkingDirectory $app -PassThru -ArgumentList (
        '--update-health-file "' + $health + '" --update-health-token ' + $token
    )
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    $confirmed = $false
    do {
        $newApp.Refresh()
        if ($newApp.HasExited) { throw 'Phien ban moi thoat luc khoi dong.' }
        if (Test-Path -LiteralPath $health) {
            if ([IO.File]::ReadAllText($health, [Text.Encoding]::UTF8) -cne $token) {
                throw 'Xac nhan khoi dong khong hop le.'
            }
            $confirmed = $true
            break
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if (!$confirmed -or $newApp.WaitForExit(3000)) { throw 'Phien ban moi khong xac nhan khoi dong on dinh.' }
    $committed = $true
    Write-Log "Commit $newVersion"
    if (!$keepBackup) {
        Remove-Item -LiteralPath $rollback -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Write-Log "Backup retained: $rollback"
    }
} catch {
    $cause = $_.Exception.Message
    Write-Log $cause
    if (!$committed -and (Test-Path -LiteralPath $rollback)) {
        try {
            if ($newApp -and !$newApp.HasExited) {
                Stop-Process -Id $newApp.Id -Force
                $null = $newApp.WaitForExit(10000)
            }
            if (Test-Path -LiteralPath $app) { Move-Item -LiteralPath $app -Destination $failed }
            Move-Item -LiteralPath $rollback -Destination $app
            if (Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key -Recurse -Force }
            & reg.exe import $meta | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Khong khoi phuc duoc muc go cai dat.' }
            Write-Log "Rollback complete $($tx.previousVersion)"
            if (Test-Path -LiteralPath $failed) { Remove-Item -LiteralPath $failed -Recurse -Force -ErrorAction SilentlyContinue }
        } catch {
            $restartOld = $false
            Write-Log ('Rollback incomplete: ' + $_.Exception.Message)
            $cause += "`nRollback can kiem tra thu cong. Backup: $rollback"
        }
    }
    if ($restartOld -and (Test-Path -LiteralPath $exe) -and (RunningIn $app).Count -eq 0) {
        Start-Process -FilePath $exe -WorkingDirectory $app -ErrorAction SilentlyContinue
    }
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($cause + "`nNhat ky: " + $log, 'ArtisanAxis Update') | Out-Null
} finally {
    if ($lock) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
