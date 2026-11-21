@echo off
rem REIM M3 playable entry. Starts the UE Host (Shipping RC by default).
rem For Development: play-m3.bat -Config Development
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\play-m3.ps1" %*
