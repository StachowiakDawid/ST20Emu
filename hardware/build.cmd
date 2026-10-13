@echo off
if not defined ST20ROOT (
  echo Set ST20ROOT to the ST20R2.3.1 installation directory.
  exit /b 1
)

call "%ST20ROOT%\bin\ST20.bat"
if errorlevel 1 exit /b 1

cd /d "%~dp0"
set "SDK_ASC=%ST20ROOT%\examples\uartio\asc"
set "COMMON=-g -O0 -p STi5512MB282B_sdram -runtime os20 -D STi5512MB282B_sdram"

"%ST20ROOT%\bin\st20cc.exe" -c %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -o interruptible_probe.tco interruptible_probe.c
if errorlevel 1 goto failed
"%ST20ROOT%\bin\st20cc.exe" -c %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -o interruptible_ops.tco interruptible_ops.S
if errorlevel 1 goto failed
"%ST20ROOT%\bin\st20cc.exe" -c %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -o shadow_capture.tco shadow_capture.S
if errorlevel 1 goto failed
"%ST20ROOT%\bin\st20cc.exe" -c %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -o asc.tco "%SDK_ASC%\asc.c"
if errorlevel 1 goto failed
"%ST20ROOT%\bin\st20cc.exe" -c %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -o pio.tco "%SDK_ASC%\pio.c"
if errorlevel 1 goto failed
"%ST20ROOT%\bin\st20cc.exe" -c %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -o hw.tco "%SDK_ASC%\hw.c"
if errorlevel 1 goto failed
"%ST20ROOT%\bin\st20cc.exe" %COMMON% -I"%SDK_ASC%" -I"%ST20ROOT%\examples\lib" -M interruptible_probe.map interruptible_probe.tco interruptible_ops.tco shadow_capture.tco asc.tco pio.tco hw.tco
if errorlevel 1 goto failed
exit /b 0

:failed
exit /b %ERRORLEVEL%