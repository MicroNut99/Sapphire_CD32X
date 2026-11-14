@echo off
rem build_sap_core4.bat - Sapphire CD32X docs/65: the fourth fit of Sapphire's cart core - Gate of Thunder's console-proven
rem core (step 20) with the Arcade Card, the sector port, the backup RAM on the battery RAM chip and the save port (ID 5A01).
rem Fit 3 fitted (15,314 logic elements) but missed timing at 50 MHz; this kit takes the Arcade Card's index and register
rem value a clock later, off the paths into the CPU's tick and the PSRAM's address (the bench: the same frames).
rem Double-click it in the Sapphire folder's fpga\gotcore_fit4. It needs Gate of Thunder's project in fpga_work
rem (fpga_pro\map_got_cart) and leaves it as it is: it copies it into fpga_pro\map_sap_a and map_sap_b, GoT's mapper folder
rem into mapper\map_sap, and this kit's files over that copy.
rem   variant g: Gate of Thunder's Quartus settings (high-performance effort, physical synthesis); seeds until timing is met
rem Seeds 11 3 5 7 13 17 19 23 until timing is met (about 10 minutes each); if the first does not fit, it stops there.
rem Everything comes back into out\ here (RESULT.txt says what happened). Nothing is copied to the SD card.
setlocal
set HERE=%~dp0
set Q=S:\KoboPort\sega-toolchain-12.1\sega\altera_lite\quartus\bin64
set TREE=S:\KoboPort\sega-toolchain-12.1\sega\fpga_work\mega-ed-pub\fpga
if not exist "%TREE%\fpga_pro\map_got_cart\mega-pro.qsf" (echo Gate of Thunder's project fpga_pro\map_got_cart is not in %TREE% - tell Claude. & pause & exit /b 1)
if not exist "%TREE%\mapper\map_got\map_ssf_got.sv" (echo %TREE%\mapper\map_got is not there - tell Claude. & pause & exit /b 1)
set OUT=%HERE%out
mkdir "%OUT%" 2>nul
del /q "%OUT%\*.*" 2>nul
echo Sapphire core fit 4 > "%OUT%\RESULT.txt"
xcopy /e /i /y /q "%TREE%\mapper\map_got" "%TREE%\mapper\map_sap" >nul
copy /y "%HERE%mapper\*.*" "%TREE%\mapper\map_sap\" >nul
call :variant g
echo.
type "%OUT%\RESULT.txt"
echo.
echo Done. Tell Claude "fit done": it reads the out folder from here.
echo done > "%OUT%\DONE.txt"
pause
exit /b 0

:variant
set V=%1
set P=%TREE%\fpga_pro\map_sap_%V%
mkdir "%P%" 2>nul
copy /y "%TREE%\fpga_pro\map_got_cart\mega-pro.qpf" "%P%\" >nul
copy /y "%TREE%\fpga_pro\map_got_cart\topcfg.sv" "%P%\" >nul
copy /y "%TREE%\fpga_pro\map_got_cart\got_cart.sdc" "%P%\" >nul
copy /y "%TREE%\fpga_pro\map_got_cart\got_paths.tcl" "%P%\" >nul
copy /y "%HERE%mapper\dx_cdfw*.hex" "%P%\" >nul
for %%s in (11 3 5 7 13 17 19 23) do (
  if not exist "%OUT%\done_%V%.txt" if not exist "%OUT%\stop_%V%.txt" (
    echo ---- variant %V%, seed %%s: building, about 10 minutes ----
    powershell -NoProfile -Command "(Get-Content -Raw '%TREE%\fpga_pro\map_got_cart\mega-pro.qsf') -replace '(?m)mapper/map_got(/|\r?$)','mapper/map_sap$1' | Set-Content -NoNewline '%P%\mega-pro.qsf'"
    echo.>> "%P%\mega-pro.qsf"
    echo set_global_assignment -name SYSTEMVERILOG_FILE ../../mapper/map_sap/sap_ac.sv>> "%P%\mega-pro.qsf"
    echo set_global_assignment -name SYSTEMVERILOG_FILE ../../mapper/map_ssf/map_ssf.sv>> "%P%\mega-pro.qsf"
    if "%V%"=="g" echo set_global_assignment -name OPTIMIZATION_MODE "HIGH PERFORMANCE EFFORT">> "%P%\mega-pro.qsf"
    if "%V%"=="g" echo set_global_assignment -name PHYSICAL_SYNTHESIS_COMBO_LOGIC ON>> "%P%\mega-pro.qsf"
    if "%V%"=="g" echo set_global_assignment -name PHYSICAL_SYNTHESIS_REGISTER_DUPLICATION ON>> "%P%\mega-pro.qsf"
    if "%V%"=="g" echo set_global_assignment -name PHYSICAL_SYNTHESIS_REGISTER_RETIMING ON>> "%P%\mega-pro.qsf"
    if "%V%"=="g" echo set_global_assignment -name PHYSICAL_SYNTHESIS_EFFORT EXTRA>> "%P%\mega-pro.qsf"
    echo set_global_assignment -name FITTER_EFFORT "STANDARD FIT">> "%P%\mega-pro.qsf"
    echo set_global_assignment -name VERILOG_MACRO "SAP_NOREGRD=1">> "%P%\mega-pro.qsf"
    echo set_global_assignment -name SEED %%s>> "%P%\mega-pro.qsf"
    del /q "%P%\output_files\*.*" 2>nul
    pushd "%TREE%\fpga_pro"
    "%Q%\quartus_sh.exe" --flow compile map_sap_%V%/mega-pro > "%OUT%\build_%V%_%%s.log" 2>&1
    popd
    copy /y "%P%\mega-pro.qsf" "%OUT%\qsf_%V%.txt" >nul
    copy /y "%P%\output_files\mega-pro.map.summary" "%OUT%\map_%V%_%%s.summary.txt" >nul 2>nul
    copy /y "%P%\output_files\mega-pro.map.rpt" "%OUT%\map_%V%.rpt.txt" >nul 2>nul
    copy /y "%P%\output_files\mega-pro.fit.summary" "%OUT%\fit_%V%_%%s.summary.txt" >nul 2>nul
    copy /y "%P%\output_files\mega-pro.fit.rpt" "%OUT%\fit_%V%.rpt.txt" >nul 2>nul
    copy /y "%P%\output_files\mega-pro.sta.summary" "%OUT%\sta_%V%_%%s.summary.txt" >nul 2>nul
    if exist "%OUT%\sta_%V%_%%s.summary.txt" (
      findstr /c:"Slack : -" "%OUT%\sta_%V%_%%s.summary.txt" >nul && (echo variant %V% seed %%s: built, timing failed>> "%OUT%\RESULT.txt") || (echo variant %V% seed %%s: TIMING OK>> "%OUT%\RESULT.txt" & echo ok> "%OUT%\done_%V%.txt" & copy /y "%P%\output_files\mega-pro.rbf" "%OUT%\mega-pro_%V%.rbf" >nul)
    ) else (
      echo variant %V% seed %%s: NOT BUILT - no timing report: did not fit, or an error - the log says>> "%OUT%\RESULT.txt"
      echo stop> "%OUT%\stop_%V%.txt"
    )
  )
)
if not exist "%OUT%\done_%V%.txt" if not exist "%OUT%\stop_%V%.txt" (
  pushd "%P%"
  "%Q%\quartus_sta.exe" -t got_paths.tcl "%OUT%\paths_%V%.txt" > "%OUT%\paths_%V%.log" 2>&1
  popd
)
exit /b 0
