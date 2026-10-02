# Fresh connected control/candidate or sparse diagnostic campaign. Always restore production.
param([string]$Port='COM10', [string]$Label='control', [switch]$Profile,
      [switch]$Release,
      [switch]$Resume,
      [int]$CommonNoise=1, [int]$ProductionCommonNoise=1,
      [string]$Build='build-m81-profile',
      [int[]]$Models=@(0,1,2,3), [int[]]$Batches=@(0,1))
$ErrorActionPreference='Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
. 'C:\Users\devx\esp\esp-idf\export.ps1' *> $null
$dir="docs/qualification/m81/$Label"
New-Item -ItemType Directory -Force $dir | Out-Null
$build=$Build
New-Item -ItemType Directory -Force $build | Out-Null
$config=Join-Path $PWD "$build/sdkconfig.m81"
$text=[IO.File]::ReadAllText((Join-Path $PWD 'sdkconfig'))
$text=$text.Replace('# CONFIG_POCKETPAN_POLYPHONY_FORENSICS is not set','CONFIG_POCKETPAN_POLYPHONY_FORENSICS=y')
$text+="`nCONFIG_POCKETPAN_FORENSICS_BLOCKS=4096`n"
[IO.File]::WriteAllText($config,$text)
function Invoke-Idf([string[]]$Arguments,[string]$Log) {
    & idf.py @Arguments *> $Log
    if($LASTEXITCODE -ne 0) {Get-Content $Log -Tail 25; throw "idf.py failed: $Log"}
}
try {
    foreach($model in $Models) {foreach($batch in $Batches) {
        $prefix="$dir/batch${model}_${batch}"
        if(Test-Path "${prefix}_raw.log") {
            $expected=if($Release){15}elseif($batch){8}else{12}
            $curves=([regex]::Matches([IO.File]::ReadAllText((Join-Path $PWD "${prefix}_raw.log")), '\[CURVE\]')).Count
            if($Resume -and $curves -eq $expected) {continue}
            throw "Evidence exists or incomplete: $prefix"
        }
        Invoke-Idf @('-B',$build,"-DSDKCONFIG=$config",'-DPOCKETPAN_PACKED_NOTE_CACHE=1',
            '-DPOCKETPAN_DSP_CANDIDATE=25',"-DPOCKETPAN_FORENSICS_M8_MODEL=$model",
            "-DPOCKETPAN_COMMON_NOISE=$CommonNoise",
            '-DPOCKETPAN_FORENSICS_OUTLIER_US=2200','-DPOCKETPAN_UI_AUDIO_CORRELATION=1',
            '-DPOCKETPAN_FORENSICS_FINE_BINS=5',
            "-DPOCKETPAN_FORENSICS_PHASE_PROBES=$(if($Profile){'ON'}else{'OFF'})",
            "-DPOCKETPAN_FORENSICS_RELEASE_PROBE=$(if($Release){1}else{0})",
            "-DPOCKETPAN_FORENSICS_M8_BATCH=$batch",'build') "${prefix}_build.log"
        Invoke-Idf @('-B',$build,'-p',$Port,'flash') "${prefix}_flash.log"
        Copy-Item "$build/pocket_pan.bin" "${prefix}_firmware.bin"
        Copy-Item "$build/CMakeCache.txt" "${prefix}_cmake.txt"
        Copy-Item $config "${prefix}_sdkconfig.txt"
        $rows=if($Release){15}elseif($batch){8}else{12}
        & python tests/capture_forensics.py "${prefix}_raw.log" --port $Port --rows $rows --seconds 90 --reset
        if($LASTEXITCODE -ne 0) {throw "Incomplete connected capture: $prefix"}
    }}
} finally {
    $restore="$dir/production"
    New-Item -ItemType Directory -Force $restore | Out-Null
    Invoke-Idf @('-B','build-ci-prod','-DPOCKETPAN_PACKED_NOTE_CACHE=1',
        '-DPOCKETPAN_FORENSICS_PHASE_PROBES=OFF','-DPOCKETPAN_UI_AUDIO_CORRELATION=0',
        '-DPOCKETPAN_FORENSICS_RELEASE_PROBE=0',
        "-DPOCKETPAN_COMMON_NOISE=$ProductionCommonNoise",
        '-DPOCKETPAN_FORENSICS_M8_MODEL=-1','-DPOCKETPAN_DSP_CANDIDATE=25','build') "$restore/build.log"
    Invoke-Idf @('-B','build-ci-prod','-p',$Port,'flash') "$restore/flash.log"
    Copy-Item build-ci-prod/pocket_pan.bin "$restore/firmware.bin"
    Copy-Item build-ci-prod/pocket_pan.elf "$restore/production.elf"
    Copy-Item build-ci-prod/pocket_pan.map "$restore/production.map"
    & python tests/capture_qualification.py "$restore/raw.log" --port $Port --seconds 20
}
