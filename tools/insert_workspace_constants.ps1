param(
    [Parameter(Mandatory=$true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

if (!(Test-Path -LiteralPath $Path)) {
    Write-Error "File not found: $Path"
    exit 1
}

$text = Get-Content -LiteralPath $Path -Raw

if ($text -match 'static\s+const\s+int32_t\s+kxpWorkspace\s*=\s*640\s*;' -and
    $text -match 'static\s+const\s+int32_t\s+kypWorkspace\s*=\s*480\s*;') {
    Write-Host "Workspace constants already present. No change needed."
    exit 0
}

$needle = "bool APP::_FSaveScreenshot(void)"
$insert = "static const int32_t kxpWorkspace = 640;`r`nstatic const int32_t kypWorkspace = 480;`r`n`r`n$needle"

if ($text.IndexOf($needle) -lt 0) {
    Write-Error "Could not find marker: $needle"
    exit 1
}

$text = $text.Replace($needle, $insert)
Set-Content -LiteralPath $Path -Value $text -NoNewline
Write-Host "Inserted kxpWorkspace/kypWorkspace constants before _FSaveScreenshot()."
