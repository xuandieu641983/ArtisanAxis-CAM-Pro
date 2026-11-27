param(
  [Parameter(Mandatory=$true)][string]$QtRoot,
  [string]$InnoCompiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
  [string]$CertificateThumbprint = '',
  [switch]$Rhino3dm,
  [string]$SignTool = 'signtool.exe',
  [string]$TimestampUrl = 'http://timestamp.digicert.com',
  [string]$UpdateRepository = '',
  [string]$GoogleSheetCsv = ''
)
$ErrorActionPreference = 'Stop'
$src = Split-Path -Parent $PSScriptRoot
function Run([string]$File, [string[]]$Arguments) {
  & $File @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$File exited with $LASTEXITCODE" }
}
function Sign([string]$File) {
  if ($CertificateThumbprint) {
    Run $SignTool @('sign','/sha1',$CertificateThumbprint,'/fd','SHA256','/tr',$TimestampUrl,'/td','SHA256',$File)
  }
}
$projectLine = Select-String -LiteralPath (Join-Path $src 'CMakeLists.txt') -Pattern '^project\(ArtisanAxis VERSION (\d+\.\d+\.\d+) '
if (!$projectLine) { throw 'Project version was not found.' }
$version = $projectLine.Matches[0].Groups[1].Value
if ($UpdateRepository -and $UpdateRepository -notmatch '^[\w.-]+/[\w.-]+$') { throw 'Invalid GitHub update repository.' }
if ($CertificateThumbprint -and $CertificateThumbprint -notmatch '^[A-Fa-f0-9]{40}$') { throw 'Invalid signing thumbprint.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio discovery tool vswhere.exe was not found.' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if (!$vs) { throw 'Visual Studio with MSVC x64 was not found.' }
$major = ([version]$vs).Major
$generator = switch ($major) {
  17 { 'Visual Studio 17 2022' }
  18 { 'Visual Studio 18 2026' }
  default { throw "Visual Studio major version $major is not supported by this build script." }
}
$build = Join-Path $src "build-win64-vs$major"
$stage = Join-Path $src ('stage-build-' + [Guid]::NewGuid().ToString('N'))
Run 'cmake' @('-S',$src,'-B',$build,'-G',$generator,'-A','x64',"-DCMAKE_PREFIX_PATH=$QtRoot",'-DCMAKE_POLICY_VERSION_MINIMUM=3.5',("-DARTISAN_RHINO="+$(if($Rhino3dm){"ON"}else{"OFF"})))
Run 'cmake' @('--build',$build,'--config','Release','--parallel')
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
Run 'ctest' @('--test-dir',$build,'-C','Release','--output-on-failure')
Run 'cmake' @('--install',$build,'--config','Release','--prefix',$stage)
Run (Join-Path $QtRoot 'bin\windeployqt.exe') @('--release','--no-translations','--no-compiler-runtime',$stage)
$runtimeDir = Join-Path $stage 'vcredist'
New-Item -ItemType Directory -Path $runtimeDir | Out-Null
$runtime = Join-Path $runtimeDir 'vc_redist.x64.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$runtimeSource = Get-ChildItem -Path (Join-Path $vsPath 'VC\Redist\MSVC') -Filter 'vc_redist.x64.exe' -File -Recurse -ErrorAction SilentlyContinue |
  Sort-Object { [Diagnostics.FileVersionInfo]::GetVersionInfo($_.FullName).FileVersionRaw } -Descending | Select-Object -First 1
if ($runtimeSource) { Copy-Item -LiteralPath $runtimeSource.FullName -Destination $runtime }
else {
  Invoke-WebRequest -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -UseBasicParsing -OutFile $runtime
}
$runtimeSig = Get-AuthenticodeSignature -LiteralPath $runtime
if ($runtimeSig.Status -ne 'Valid' -or $runtimeSig.SignerCertificate.Subject -notmatch 'CN=Microsoft Corporation') {
  throw 'Expected a signed Microsoft Visual C++ Redistributable.'
}
Set-Content -LiteralPath (Join-Path $stage 'scripts\version.txt') -Value $version -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Install-Update.ps1') -Destination (Join-Path $stage 'scripts')
[ordered]@{
  githubRepository = $UpdateRepository
  googleSheetCsv = $GoogleSheetCsv
  publisherSignerSha1 = $CertificateThumbprint
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'scripts\update-feed.json') -Encoding UTF8
Sign (Join-Path $stage 'ArtisanAxis.exe')
Sign (Join-Path $stage 'CAMWorker.exe')
# The installer receives the unique staging directory rather than reusing old DLLs.
Run $InnoCompiler @("/DStageDir=$stage","/DBuildVersion=$version",(Join-Path $src 'packaging\Setup.iss'))
$installer = Join-Path $src 'dist\Setup.exe'
Sign $installer
if ($CertificateThumbprint) {
  $signature = Get-AuthenticodeSignature -LiteralPath $installer
  if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint) {
    throw 'Expected a trusted Authenticode signature on Setup.exe.'
  }
}
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$releaseUrl = if ($UpdateRepository) { "https://github.com/$UpdateRepository/releases/download/v$version/Setup.exe" } else { '' }
[ordered]@{
  channel = 'stable'
  version = $version
  url = $releaseUrl
  sha256 = $hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $src 'dist\update-feed.json') -Encoding ASCII
"$hash *Setup.exe" | Set-Content -LiteralPath (Join-Path $src 'dist\Setup.exe.sha256') -Encoding ASCII
Write-Output "Release binaries: $stage"
Write-Output "Installer: $src\dist\Setup.exe"
