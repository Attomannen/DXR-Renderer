[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Retail')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string]$Solution = 'Build/Solutions/Game.sln',

    [string]$Target = 'Build'
)

$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $workspace $Solution
if (-not (Test-Path -LiteralPath $solutionPath)) {
    throw "Solution was not found at '$solutionPath'."
}
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild was not found at '$msbuild'."
}

# Codex's runtime can contribute an uppercase PATH while Windows contributes
# Path.  MSBuild's VC task converts its process environment to a case-sensitive
# dictionary and then fails before cl.exe starts.  Give the child one canonical
# Path entry without changing the user or machine environment.
$start = [System.Diagnostics.ProcessStartInfo]::new()
$start.FileName = $msbuild
$start.WorkingDirectory = $workspace
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @(
    $solutionPath,
    "/t:$Target",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/nologo'
)) {
    [void]$start.ArgumentList.Add($argument)
}

$start.Environment.Clear()
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
    $name = [string]$entry.Key
    if ($seen.Add($name)) {
        $canonicalName = if ($name -ieq 'Path') { 'Path' } else { $name }
        $start.Environment[$canonicalName] = [string]$entry.Value
    }
}

$process = [System.Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()
Write-Output $stdout
if ($stderr) { Write-Error $stderr }
exit $process.ExitCode
