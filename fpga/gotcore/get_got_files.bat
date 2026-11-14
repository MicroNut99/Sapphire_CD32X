@echo off
rem get_got_files.bat - Sapphire CD32X docs/65: no compile. Copies Gate of Thunder's fitted core files (fpga_work's
rem mapper\map_got) and its step-20 build reports into out\got\ here, so Claude can see what that fit really used (the
rem first Sapphire fit came out 3,300 logic elements larger than step 20, far more than the card). Takes a few seconds.
setlocal
set HERE=%~dp0
set TREE=S:\KoboPort\sega-toolchain-12.1\sega\fpga_work\mega-ed-pub\fpga
set W=S:\KoboPort\sega-toolchain-12.1\sega\fpga_work
mkdir "%HERE%out\got" 2>nul
mkdir "%HERE%out\got\map_got" 2>nul
mkdir "%HERE%out\got\map_dx" 2>nul
copy /y "%TREE%\mapper\map_got\*.*" "%HERE%out\got\map_got\" >nul
copy /y "%TREE%\mapper\map_dx\dx_*.*" "%HERE%out\got\map_dx\" >nul 2>nul
copy /y "%TREE%\fpga_pro\map_got_cart\mega-pro.qsf" "%HERE%out\got\map_got_cart.qsf.txt" >nul 2>nul
copy /y "%TREE%\fpga_pro\map_got_cart_p20\mega-pro.qsf" "%HERE%out\got\map_got_cart_p20.qsf.txt" >nul 2>nul
copy /y "%TREE%\fpga_pro\map_got_cart_p20\output_files\mega-pro.map.rpt" "%HERE%out\got\p20.map.rpt.txt" >nul 2>nul
copy /y "%TREE%\fpga_pro\map_got_cart_p20\output_files\mega-pro.fit.summary" "%HERE%out\got\p20.fit.summary.txt" >nul 2>nul
copy /y "%TREE%\fpga_pro\map_got_cart_p20\output_files\mega-pro.map.summary" "%HERE%out\got\p20.map.summary.txt" >nul 2>nul
copy /y "%W%\got_map_got_cart_p20.fit.summary.txt" "%HERE%out\got\" >nul 2>nul
dir /b /s "%HERE%out\got" > "%HERE%out\got\LIST.txt"
echo Copied. Tell Claude "got files copied".
pause
