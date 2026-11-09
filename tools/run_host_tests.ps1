# Run in a Visual Studio x64 developer terminal with Node and Python available.
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$out=Join-Path $project 'build-host-tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$ErrorActionPreference='Continue'
& cl /nologo /std:c++17 /EHsc /W4 /WX (Join-Path $project 'tests\car_core_test.cpp') "/Fe:$out\car_core_test.exe" "/Fo:$out\car_core_test.obj"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& (Join-Path $out 'car_core_test.exe')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if($env:IDF_PATH){
    $json=Join-Path $env:IDF_PATH 'components\json\cJSON'
    & cl /nologo /TC /W3 /D_CRT_SECURE_NO_WARNINGS /c (Join-Path $json 'cJSON.c') "/Fo:$out\cjson.obj"
    if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
    & cl /nologo /std:c++17 /EHsc /W4 /WX "/I$json" (Join-Path $project 'tests\web_safety_test.cpp') (Join-Path $out 'cjson.obj') "/Fe:$out\web_safety_test.exe" "/Fo:$out\web_safety_test.obj"
    if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
    & (Join-Path $out 'web_safety_test.exe')
    if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}else{throw 'IDF_PATH is required for the cJSON HTTP safety test.'}
& node (Join-Path $project 'tests\phone_test.cjs')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& cl /nologo /std:c++17 /EHsc /W4 /WX (Join-Path $project 'tests\range_test.cpp') "/Fe:$out\range_test.exe" "/Fo:$out\range_test.obj"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& (Join-Path $out 'range_test.exe')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& cl /nologo /std:c++17 /EHsc /W4 /WX (Join-Path $project 'tests\firmware_simulator.cpp') "/Fe:$out\firmware_simulator.exe" "/Fo:$out\firmware_simulator.obj"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$env:CAR_FIRMWARE_SIMULATOR=Join-Path $out 'firmware_simulator.exe'
& python -m unittest discover -s (Join-Path $project 'tests') -p 'test_*.py' -v
exit $LASTEXITCODE
