@echo off
set NAME=SkeletonCpuAvx2
if exist %NAME%.exe del %NAME%.exe
cl /Zi /O2 /EHsc- /W4 /wd4324 /wd4204 /fp:fast /arch:AVX2 %NAME%.c /link kernel32.lib user32.lib gdi32.lib /incremental:no /opt:ref
if exist %NAME%.obj del %NAME%.obj
if "%1" == "run" if exist %NAME%.exe (.\%NAME%.exe)
