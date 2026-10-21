@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Kasumi Adaptive for Metasequoia IME

rem This convenience launcher supports only a normal install or -WhatIf.
rem Use Install.ps1 directly for -ServerDirectory / -DataDirectory overrides.
if not "%~1"=="" if /I not "%~1"=="-WhatIf" goto :bad_args
if /I "%~1"=="-WhatIf" if not "%~2"=="" goto :bad_args

set "SCRIPT=%~dp0Install.ps1"
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "KASUMI_INSTALL_PS1=%SCRIPT%"
set "KASUMI_POWERSHELL=%POWERSHELL%"

if not exist "%SCRIPT%" goto :missing_script
if not exist "%POWERSHELL%" goto :missing_powershell

if /I "%~1"=="-WhatIf" goto :whatif

rem Detect the current token. A normal (non-elevated) launch is elevated below.
"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -Command ^
 "try { $id = [Security.Principal.WindowsIdentity]::GetCurrent(); $p = New-Object Security.Principal.WindowsPrincipal($id); if ($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { exit 0 } else { exit 1 } } catch { exit 1 }"
if errorlevel 1 goto :elevate
goto :run_direct

:whatif
rem -WhatIf is deliberately read-only and does not need a UAC prompt.
"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -WhatIf
set "RC=%ERRORLEVEL%"
goto :finish

:run_direct
rem Already elevated: keep this console visible so the real status is readable.
"%POWERSHELL%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -Confirm:$false
set "RC=%ERRORLEVEL%"
goto :finish

:elevate
rem One normal UAC prompt. The elevated helper is hidden and writes a UTF-8 log.
rem This parent waits only for that helper process (Start-Process -Wait); it never
rem waits for a resident Server, Watchdog or companion helper.
set "KASUMI_INSTALL_LOG=%TEMP%\kasumi-install-%RANDOM%-%RANDOM%.log"
if exist "%KASUMI_INSTALL_LOG%" del /q "%KASUMI_INSTALL_LOG%" >nul 2>&1
rem The -EncodedCommand below is the readable wrapper maintained here:
rem   $ErrorActionPreference = 'Stop'
rem   $log = $env:KASUMI_INSTALL_LOG
rem   $text = ''
rem   try { $output = & $env:KASUMI_INSTALL_PS1 -Confirm:$false *>&1
rem         $text = ($output | Out-String)
rem         [IO.File]::WriteAllText($log, $text, UTF8NoBom); exit 0 }
rem   catch { $text = $text + ($_ | Out-String)
rem           [IO.File]::WriteAllText($log, $text, UTF8NoBom); exit 1 }
echo [kasumi] Waiting for the elevated installer to finish...
echo [kasumi] This console waits only for that installer process; no resident helper is waited on.
"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -Command ^
 "$ErrorActionPreference = 'Stop'; $encoded = 'JABFAHIAcgBvAHIAQQBjAHQAaQBvAG4AUAByAGUAZgBlAHIAZQBuAGMAZQAgAD0AIAAnAFMAdABvAHAAJwAKACQAbABvAGcAIAA9ACAAJABlAG4AdgA6AEsAQQBTAFUATQBJAF8ASQBOAFMAVABBAEwATABfAEwATwBHAAoAJAB0AGUAeAB0ACAAPQAgACcAJwAKAHQAcgB5ACAAewAKACAAIAAgACAAJABvAHUAdABwAHUAdAAgAD0AIAAmACAAJABlAG4AdgA6AEsAQQBTAFUATQBJAF8ASQBOAFMAVABBAEwATABfAFAAUwAxACAALQBDAG8AbgBmAGkAcgBtADoAJABmAGEAbABzAGUAIAAqAD4AJgAxAAoAIAAgACAAIAAkAHQAZQB4AHQAIAA9ACAAKAAkAG8AdQB0AHAAdQB0ACAAfAAgAE8AdQB0AC0AUwB0AHIAaQBuAGcAKQAKACAAIAAgACAAWwBJAE8ALgBGAGkAbABlAF0AOgA6AFcAcgBpAHQAZQBBAGwAbABUAGUAeAB0ACgAJABsAG8AZwAsACAAJAB0AGUAeAB0ACwAIAAoAE4AZQB3AC0ATwBiAGoAZQBjAHQAIABTAHkAcwB0AGUAbQAuAFQAZQB4AHQALgBVAFQARgA4AEUAbgBjAG8AZABpAG4AZwAoACQAZgBhAGwAcwBlACkAKQApAAoAIAAgACAAIABlAHgAaQB0ACAAMAAKAH0ACgBjAGEAdABjAGgAIAB7AAoAIAAgACAAIAAkAHQAZQB4AHQAIAA9ACAAJAB0AGUAeAB0ACAAKwAgACgAJABfACAAfAAgAE8AdQB0AC0AUwB0AHIAaQBuAGcAKQAKACAAIAAgACAAWwBJAE8ALgBGAGkAbABlAF0AOgA6AFcAcgBpAHQAZQBBAGwAbABUAGUAeAB0ACgAJABsAG8AZwAsACAAJAB0AGUAeAB0ACwAIAAoAE4AZQB3AC0ATwBiAGoAZQBjAHQAIABTAHkAcwB0AGUAbQAuAFQAZQB4AHQALgBVAFQARgA4AEUAbgBjAG8AZABpAG4AZwAoACQAZgBhAGwAcwBlACkAKQApAAoAIAAgACAAIABlAHgAaQB0ACAAMQAKAH0A'; try { $p = Start-Process -FilePath $env:KASUMI_POWERSHELL -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -Verb RunAs -WindowStyle Hidden -PassThru -Wait; exit $p.ExitCode } catch { [Console]::Error.WriteLine('[kasumi] UAC/elevation failed: ' + $_.Exception.Message); exit 1223 }"
set "RC=%ERRORLEVEL%"
if exist "%KASUMI_INSTALL_LOG%" (
  "%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -Command "Get-Content -LiteralPath $env:KASUMI_INSTALL_LOG -Raw -Encoding UTF8"
)
if "%RC%"=="0" goto :elevated_success
echo.
echo [kasumi] Installation failed. Exit code: %RC%.
echo [kasumi] Full log: %KASUMI_INSTALL_LOG%
goto :finish

:elevated_success
echo.
echo [kasumi] Installation completed successfully.
echo [kasumi] Full log: %KASUMI_INSTALL_LOG%
goto :finish

:bad_args
echo [kasumi] This launcher accepts no arguments or only -WhatIf.
echo [kasumi] For -ServerDirectory / -DataDirectory, run Install.ps1 from PowerShell.
exit /b 2

:missing_script
echo [kasumi] Install.ps1 is missing next to Install.cmd.
exit /b 2

:missing_powershell
echo [kasumi] Windows PowerShell 5.1 was not found at:
echo   %POWERSHELL%
exit /b 2

:finish
if /I "%~1"=="" (
  echo.
  pause
)
endlocal & exit /b %RC%
