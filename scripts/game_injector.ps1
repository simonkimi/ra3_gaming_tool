param(
    [Parameter(Position = 0)]
    [string]$Action,

    [Parameter(Mandatory = $true)]
    [string]$Exe,

    [string]$ProcessName = 'ra3_1.12.game'
)

function Fail([string]$Message) {
    [Console]::Error.WriteLine($Message)
    exit 1
}

if ($Action -notin @('inject', 'unload')) {
    Fail 'usage: task game_injector -- inject|unload'
}

if (-not (Test-Path -LiteralPath $Exe)) {
    Fail "game_injector not found: $Exe"
}

$procs = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) {
    Fail "$ProcessName is not running"
}

$code = 0
foreach ($proc in $procs) {
    Write-Host "$Action pid=$($proc.Id)"
    & $Exe $Action $proc.Id
    if ($LASTEXITCODE -ne 0) {
        $code = $LASTEXITCODE
    }
}
exit $code
