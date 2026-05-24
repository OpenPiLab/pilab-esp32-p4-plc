@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%tools\firmware_tests\build"
set "STUB_DIR=%ROOT%tools\firmware_tests\stubs"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cl /nologo /std:c++17 /EHsc ^
  /I "%ROOT%main" ^
  /I "%STUB_DIR%" ^
  "%ROOT%tools\firmware_tests\metadata_builder_host_test.cpp" ^
  "%ROOT%main\pilab_script_builder.cpp" ^
  /Fo"%BUILD_DIR%\\" ^
  /Fe"%BUILD_DIR%\metadata_builder_host_test.exe"

if errorlevel 1 (
  echo.
  echo Firmware metadata host test build failed.
  exit /b 1
)

"%BUILD_DIR%\metadata_builder_host_test.exe"

if errorlevel 1 (
  echo.
  echo Firmware metadata host tests failed.
  exit /b 1
)

echo.
echo PiLab firmware metadata tests completed successfully.

endlocal