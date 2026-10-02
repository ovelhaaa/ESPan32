# Paired physical legacy/packed caches; restore normal production even on failure.
param([string]$Port='COM10',[string]$EvidenceDirectory='docs/qualification/m8',[switch]$Resume,
    [int[]]$Models=@(0,1,2,3),[int[]]$Layouts=@(0,1),[int[]]$Batches=@(0,1))
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. 'C:\Users\devx\esp\esp-idf\export.ps1'
function Invoke-Idf([string[]]$Arguments,[string]$Log) {
    & idf.py @Arguments *> $Log
    if($LASTEXITCODE -ne 0) {Get-Content $Log -Tail 30;throw "idf.py failed: $Log"}
}
# Independent config: no dependence on a stale forensic build directory.
$config=Join-Path $PWD 'build-m8-qual/sdkconfig.m8'
New-Item -ItemType Directory -Force build-m8-qual | Out-Null
$text=[IO.File]::ReadAllText((Join-Path $PWD 'sdkconfig'))
$text=$text.Replace('# CONFIG_POCKETPAN_POLYPHONY_FORENSICS is not set','CONFIG_POCKETPAN_POLYPHONY_FORENSICS=y')
$text+="`nCONFIG_POCKETPAN_FORENSICS_BLOCKS=4096`n# CONFIG_POCKETPAN_DSP_PROFILE is not set`n"
[IO.File]::WriteAllText($config,$text)
try {
    foreach($packed in $Layouts) {
        $dir=Join-Path $EvidenceDirectory $(if($packed){'packed'}else{'legacy'})
        New-Item -ItemType Directory -Force $dir | Out-Null
        foreach($model in $Models) {
            foreach($batch in $Batches) {
                $prefix=Join-Path $dir "batch${model}_${batch}"
                $rows=if($batch){8}else{12}
                if(Test-Path "${prefix}_raw.log") {
                    $curves=([regex]::Matches([IO.File]::ReadAllText((Join-Path $PWD "${prefix}_raw.log")),'\[CURVE\]')).Count
                    if($Resume -and (Test-Path "${prefix}_firmware.bin") -and $curves -eq $rows) {continue}
                    throw "Existing evidence: $prefix"
                }
                Invoke-Idf @('-B','build-m8-qual',"-DSDKCONFIG=$config",'-DPOCKETPAN_FORENSICS_M77=0',
                    '-DPOCKETPAN_DSP_CANDIDATE=25','-DPOCKETPAN_PROCESS6_MICROKERNEL=1',
                    '-DPOCKETPAN_FORENSICS_M76=0','-DPOCKETPAN_FORENSICS_M75=0','-DPOCKETPAN_FORENSICS_M7=0',
                    '-DPOCKETPAN_FORENSICS_M74=0','-DPOCKETPAN_FORENSICS_FINE_BINS=5',
                    '-DPOCKETPAN_UDU_FIXED_REFERENCE=0',"-DPOCKETPAN_PACKED_NOTE_CACHE=$packed",
                    "-DPOCKETPAN_FORENSICS_M8_MODEL=$model","-DPOCKETPAN_FORENSICS_M8_BATCH=$batch",'build') "${prefix}_build.log"
                Invoke-Idf @('-B','build-m8-qual','-p',$Port,'flash') "${prefix}_flash.log"
                Copy-Item build-m8-qual/pocket_pan.bin "${prefix}_firmware.bin"
                & python tests/capture_forensics.py "${prefix}_raw.log" --port $Port --rows $rows --seconds 130 --reset
                if($LASTEXITCODE -ne 0) {throw "Incomplete hardware capture: $prefix"}
            }
        }
        if($Models.Count -ne 4 -or $Batches.Count -ne 2) {continue} # Subsets are diagnostic only.
        if($packed) { & python tests/report_qualification.py $dir --fixtures 20 }
        else { & python tests/report_qualification.py $dir --fixtures 20 --allow-deadline-misses }
        if($LASTEXITCODE -ne 0) {throw "Hardware qualification failed: $dir"}
    }
} finally {
    New-Item -ItemType Directory -Force "$EvidenceDirectory/after" | Out-Null
    Invoke-Idf @('-B','build-ci-prod','-DPOCKETPAN_PACKED_NOTE_CACHE=1','-DPOCKETPAN_FORENSICS_M8_MODEL=-1',
        '-DPOCKETPAN_DSP_CANDIDATE=25','-DPOCKETPAN_PROCESS6_MICROKERNEL=1',
        '-DPOCKETPAN_UDU_FIXED_REFERENCE=0','build') "$EvidenceDirectory/after/production_build.log"
    Invoke-Idf @('-B','build-ci-prod','-p',$Port,'flash') "$EvidenceDirectory/after/production_flash.log"
    Copy-Item build-ci-prod/pocket_pan.bin "$EvidenceDirectory/after/production_firmware.bin"
    Copy-Item build-ci-prod/pocket_pan.elf "$EvidenceDirectory/after/production.elf"
    Copy-Item build-ci-prod/pocket_pan.map "$EvidenceDirectory/after/production.map"
    & python tests/capture_qualification.py "$EvidenceDirectory/after/production_raw.log" --port $Port --seconds 30
    & python tests/report_cache_memory.py "$EvidenceDirectory/after/production.elf" "$EvidenceDirectory/after/memory.json" --verify-packed
    Get-ChildItem build-ci-prod/esp-idf/main/CMakeFiles/__idf_main.dir/dsp -Filter '*.su' |
        Copy-Item -Destination "$EvidenceDirectory/after"
    & python -m esp_idf_size --archives --format json --target esp32s3 "$EvidenceDirectory/after/production.map" -o "$EvidenceDirectory/after/archives.json"
}
