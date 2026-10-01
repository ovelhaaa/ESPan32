# Physical M7.7.1 fixtures. Retain binaries/logs; always restore production.
param([string]$Port='COM10',[switch]$IncludeFixedControl,[switch]$Resume,[string]$EvidenceDirectory='docs/qualification/m771')
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. 'C:\Users\devx\esp\esp-idf\export.ps1'
function Invoke-Idf([string[]]$IdfArguments,[string]$Log) {
    & idf.py @IdfArguments *> $Log
    if($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $Log -Tail 40; throw "idf.py failed: $Log" }
}
try {
    foreach($fixed in $(if($IncludeFixedControl){@(0,1)}else{@(0)})) {
        $directory=if($fixed){"$EvidenceDirectory/fixed_control"}else{$EvidenceDirectory}
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        foreach($batch in @(0,1)) {
            $capture="$directory/batch${batch}_raw.log"
            if(Test-Path -LiteralPath $capture) {
                if($Resume -and (Test-Path -LiteralPath "$directory/batch${batch}_firmware.bin")) { continue }
                throw "Existing evidence: $capture; use a fresh evidence directory or explicitly resume."
            }
            Invoke-Idf @('-B','build-m77-qual',"-DPOCKETPAN_FORENSICS_M77_BATCH=$batch", "-DPOCKETPAN_UDU_FIXED_REFERENCE=$fixed",'build') "$directory/batch${batch}_build.log"
            Invoke-Idf @('-B','build-m77-qual','-p',$Port,'flash') "$directory/batch${batch}_flash.log"
            Copy-Item -LiteralPath build-m77-qual/pocket_pan.bin -Destination "$directory/batch${batch}_firmware.bin"
            & python tests/capture_forensics.py $capture --port $Port --rows 12 --seconds 140 --reset
            if($LASTEXITCODE -ne 0) { throw "Incomplete capture: $capture" }
        }
        & python tests/report_qualification.py $directory --fixtures 6
        if($LASTEXITCODE -ne 0) { throw "Qualification failed: $directory" }
    }
} finally {
    Invoke-Idf @('-B','build-ci-prod','-DPOCKETPAN_UDU_FIXED_REFERENCE=0','build') 'build-ci-prod/m771_build.log'
    Invoke-Idf @('-B','build-ci-prod','-p',$Port,'flash') 'build-ci-prod/m771_flash.log'
    Copy-Item -LiteralPath build-ci-prod/pocket_pan.bin -Destination "$EvidenceDirectory/production_firmware.bin"
    & python tests/capture_qualification.py "$EvidenceDirectory/production_raw.log" --port $Port --seconds 22
}
