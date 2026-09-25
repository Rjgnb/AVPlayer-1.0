param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Stamp
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Stamp)) {
    Write-Output 'clean'
    exit 0
}

$stampTime = (Get-Item -LiteralPath $Stamp).LastWriteTimeUtc
$searchRoots = @(
    (Join-Path $Root 'src'),
    (Join-Path $Root 'tests'),
    (Join-Path $Root 'cmake')
)

$changed = Get-ChildItem -LiteralPath $searchRoots -Recurse -File -Include '*.h', '*.hpp', '*.inl', '*.ixx' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTimeUtc -gt $stampTime } |
    Select-Object -First 1

if ($null -ne $changed) {
    Write-Output 'clean'
} else {
    Write-Output 'incremental'
}
