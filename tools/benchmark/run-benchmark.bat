@echo off
rem Pokemon Emerald native PC - benchmark suite (tools/benchmark/README.md).
rem
rem Measures the 3D view in eleven scenes at 1920x1080, 2560x1440 and 3840x2160 (VSync
rem off, no frame rate limit, native render scale), three of them again with World
rem details off, and - when the previous checkpoint is next to this one - four of
rem the scenes with the previous build, for a before/after comparison.
rem
rem   run-benchmark.bat          the full suite (about 19 minutes)
rem   run-benchmark.bat quick    four scenes at 1080p and 4K (about 4 minutes)
rem
rem Do not press keys while it runs: game windows open and close by themselves. Each
rem scene starts from its own save in scenes\ (copied first); your own saves and
rem settings are not used or changed. Results: results\run-N\ (results.csv, a .log
rem per scene). Claude reads that folder.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "GAMEDIR=%~dp0..\test-run"
set "EXE=%GAMEDIR%\pokeemerald-mp-diorama.exe"
set "OLDDIR=%~dp0..\..\native-wip-20261012-1\test-run"
set "OLDEXE=%OLDDIR%\pokeemerald-mp-diorama.exe"
if not exist "%EXE%" (
    echo The game is missing: %EXE%
    pause
    exit /b 1
)

set "SECS=10"
set "HEIGHTS=1080,1440,2160"
set "MODE=full"
if /i "%~1"=="quick" (
    set "SECS=6"
    set "HEIGHTS=1080,2160"
    set "MODE=quick"
)

set /a N=1
:findrun
if exist "results\run-!N!" (
    set /a N+=1
    goto findrun
)
set "OUT=%~dp0results\run-!N!"
mkdir "!OUT!"
echo Benchmark (%MODE%) - results in results\run-!N!
echo Do not press keys; game windows open and close by themselves.
echo.

if "%MODE%"=="quick" (
    call :scene route101 route101 1
    call :scene rustboro rustboro 1
    call :scene dense-objects route110 1
    call :scene heavy-rain route104 1 PKMN_DIORAMA_WEATHER 13
    goto done
)

call :scene route101 route101 1
call :scene petalburg-woods woods 1
call :scene rustboro rustboro 1
call :scene slateport-market slateport-market 1
call :scene mauville mauville 1
call :scene granite-cave granite 1
call :scene dewford-coast dewford 1
call :scene dense-objects route110 1
call :scene jagged-pass jagged 1
call :scene night-town mauville 1 PKMN_CLOCK_HOUR 22
call :scene heavy-rain route104 1 PKMN_DIORAMA_WEATHER 13
call :scene route101-details-off route101 0
call :scene rustboro-details-off rustboro 0
call :scene dense-objects-details-off route110 0
if exist "%OLDEXE%" (
    call :before route101 route101
    call :before petalburg-woods woods
    call :before rustboro rustboro
    call :before jagged-pass jagged
) else (
    echo The previous checkpoint's game was not found next to this one; no before/after run.
)

:done
echo.
echo Done. The results are in results\run-!N!  (Claude reads them from there.)
pause
exit /b 0

rem :scene NAME SAVE DETAILS [ENVNAME ENVVALUE]
:scene
set "W=!OUT!\work-%~1"
mkdir "!W!"
copy /y "scenes\%~2.sav" "!W!\test.sav" >nul
if errorlevel 1 (
    echo   %~1: scenes\%~2.sav is missing - skipped
    exit /b 0
)
call :config "!W!\pokeemerald.cfg" %~3
if not "%~4"=="" set "%~4=%~5"
echo   %~1 ...
pushd "%GAMEDIR%"
"%EXE%" --save "!W!\test.sav" --config "!W!\pokeemerald.cfg" --benchmark %SECS% --bench-name %~1 --bench-heights %HEIGHTS% --bench-log "!OUT!\%~1.log" --bench-csv "!OUT!\results.csv" > "!OUT!\%~1-game.txt" 2>&1
popd
if not "%~4"=="" set "%~4="
if not exist "!OUT!\%~1.log" echo   %~1: no result (see %~1-game.txt)
exit /b 0

rem :before NAME SAVE - the previous checkpoint's build, its own benchmark (720p to 4K)
:before
set "W=!OUT!\before-%~1"
mkdir "!W!"
copy /y "scenes\%~2.sav" "!W!\test.sav" >nul
if errorlevel 1 exit /b 0
call :config "!W!\pokeemerald.cfg" 1
echo   %~1 (previous build) ...
pushd "!W!"
"%OLDEXE%" --save test.sav --config pokeemerald.cfg --benchmark %SECS% > game.txt 2>&1
popd
if exist "!W!\benchmark.log" (
    copy /y "!W!\benchmark.log" "!OUT!\before-%~1.log" >nul
) else (
    echo   %~1 previous build: no result
)
exit /b 0

rem :config FILE DETAILS - the default graphics (high), World details on (1) or off (0)
:config
> "%~1" echo cameraMode=adventure
>> "%~1" echo resolution=1920x1080
>> "%~1" echo displayMode=windowed
if "%~2"=="0" (
    >> "%~1" echo quality=custom
    >> "%~1" echo shadows=medium
    >> "%~1" echo lighting=1
    >> "%~1" echo effects=1
    >> "%~1" echo details=0
    >> "%~1" echo antialiasing=0
) else (
    >> "%~1" echo quality=high
)
exit /b 0
