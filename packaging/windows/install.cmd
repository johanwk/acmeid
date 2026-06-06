@echo off
rem -- acmeid Windows installer --
rem Copies acmeid.exe + acmeid.dll into c:\opt\acmeid\, the convention
rem used by the project's SQLite recipes (PLAN.org section 5.2).
setlocal
set "DEST=c:\opt\acmeid"
if not exist "%DEST%" mkdir "%DEST%"
copy /Y "%~dp0acmeid.exe" "%DEST%\" >nul || goto :fail
copy /Y "%~dp0acmeid.dll" "%DEST%\" >nul || goto :fail
echo Installed acmeid to %DEST%
echo.
echo Load from sqlite3 with:
echo     .load c:/opt/acmeid/acmeid
endlocal
exit /b 0

:fail
echo Install FAILED.>&2
endlocal
exit /b 1
