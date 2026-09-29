@echo off
rem Builds the Timing DLL (with profiler) and copies it into ..\Assets.
rem Run build.bat afterwards to go back to the normal Release DLL.
call "%~dp0build.bat" Timing
