@echo off
setlocal
set /p SONFORM_VERSION=<"%~dp0VERSION"
start "" "%~dp0build-plugin-current\HandpanStudio_artefacts\Release\Standalone\SONFORM Void %SONFORM_VERSION%.exe"
