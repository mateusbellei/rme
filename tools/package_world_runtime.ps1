[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$RuntimeDataDirectory,
    [Parameter(Mandatory)][string]$Destination,
    [Parameter(Mandatory)][string]$Revision
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeDataDirectory).Path
$target = [IO.Path]::GetFullPath($Destination)
foreach ($inputRoot in @($binary, $runtime)) {
    if ($target.Equals($inputRoot, [StringComparison]::OrdinalIgnoreCase) -or $target.StartsWith($inputRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Destination must be outside the input directories.' }
}
if (@(Get-ChildItem -LiteralPath $runtime -File -Recurse | Where-Object { $_.Extension -in '.dat', '.spr' }).Count) { throw 'Keep DAT/SPR external to the packaged runtime data.' }
if (Test-Path -LiteralPath $target) { throw 'Destination must be a new folder; existing runtimes are preserved.' }
if (!(Test-Path -LiteralPath (Join-Path $binary 'rme.exe') -PathType Leaf)) { throw 'Missing compiled rme.exe.' }
if (!(Test-Path -LiteralPath (Join-Path $runtime 'clients.xml') -PathType Leaf)) { throw 'Runtime data must include clients.xml.' }
$dlls = @(Get-ChildItem -LiteralPath $binary -Filter '*.dll' -File)
if (!$dlls.Count) { throw 'No adjacent DLLs found. Build the complete Release app first.' }
# No deletes/moves or asset discovery: copy only the explicitly provided data tree.
New-Item -ItemType Directory -Path $target | Out-Null
Copy-Item -LiteralPath (Join-Path $binary 'rme.exe') -Destination $target
foreach ($dll in $dlls) { Copy-Item -LiteralPath $dll.FullName -Destination $target }
Copy-Item -LiteralPath $runtime -Destination (Join-Path $target 'data') -Recurse
@{
    revision = $Revision
    packagedUtc = [DateTime]::UtcNow.ToString('o')
    executable = 'rme.exe'
    sha256 = (Get-FileHash -LiteralPath (Join-Path $target 'rme.exe') -Algorithm SHA256).Hash
    dllCount = $dlls.Count
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'build-info.json') -Encoding UTF8
Write-Output "Runtime ready: $target"
