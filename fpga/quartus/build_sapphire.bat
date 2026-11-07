@echo off
rem build_sapphire.bat - Sapphire CD32X docs/10: the EverDrive PRO core (krikzz's SSF mapper + the Arcade Card engine).
rem Double-click it in the Sapphire folder's fpga\quartus. It copies the project into krikzz's tree in fpga_work
rem (the one Metal Slug's builds use), runs Quartus, and copies the reports and mega-pro.rbf back into fpga\quartus\out.
setlocal
set HERE=%~dp0
set QSH=S:\KoboPort\sega-toolchain-12.1\sega\altera_lite\quartus\bin64\quartus_sh.exe
set TREE=S:\KoboPort\sega-toolchain-12.1\sega\fpga_work\mega-ed-pub\fpga
if not exist "%TREE%\mapper\lib_base" set TREE=S:\KoboPort\sega-toolchain-12.1\sega\fpga_work\fpga
if not exist "%TREE%\mapper\lib_base" (echo krikzz's fpga tree not found in fpga_work & pause & exit /b 1)
mkdir "%TREE%\mapper\map_sapphire" 2>nul
mkdir "%TREE%\fpga_pro\map_sapphire" 2>nul
copy /y "%HERE%..\map_sapphire\hub.sv"          "%TREE%\mapper\map_sapphire\" >nul
copy /y "%HERE%..\map_sapphire\map_sapphire.sv" "%TREE%\mapper\map_sapphire\" >nul
copy /y "%HERE%..\ac_engine.sv"                 "%TREE%\mapper\map_sapphire\" >nul
copy /y "%HERE%fpga_pro\map_sapphire\*.*"       "%TREE%\fpga_pro\map_sapphire\" >nul
mkdir "%HERE%out" 2>nul
echo Building in %TREE%\fpga_pro (a few minutes) ...
pushd "%TREE%\fpga_pro"
"%QSH%" --flow compile map_sapphire/mega-pro > "%HERE%out\build.log" 2>&1
popd
copy /y "%TREE%\fpga_pro\map_sapphire\output_files\mega-pro.fit.summary" "%HERE%out\" >nul
copy /y "%TREE%\fpga_pro\map_sapphire\output_files\mega-pro.sta.summary" "%HERE%out\" >nul
copy /y "%TREE%\fpga_pro\map_sapphire\output_files\mega-pro.rbf"         "%HERE%out\" >nul
echo done > "%HERE%out\DONE.txt"
echo Done: see fpga\quartus\out
pause
