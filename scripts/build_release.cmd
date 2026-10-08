@echo off
setlocal
cd /d "%~dp0\.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer / vswhere.exe was not found.
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  set "MSBUILD=%%i"
  goto :found
)

echo MSBuild was not found.
exit /b 1

:found
echo Using: %MSBUILD%
"%MSBUILD%" SSHLiteWin32.sln /m /p:Configuration=Release /p:Platform=x64
exit /b %errorlevel%
