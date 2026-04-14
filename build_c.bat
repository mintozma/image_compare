@echo off
echo [Build] VideoColorDiff_C.exe 컴파일 중...

gcc -O2 -mwindows ^
    -o VideoColorDiff_C.exe ^
    videodiff.c ^
    -lgdi32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 ^
    -lwindowscodecs

if %ERRORLEVEL% == 0 (
    echo.
    echo [OK] 빌드 성공: VideoColorDiff_C.exe
) else (
    echo.
    echo [FAIL] 빌드 실패 - 오류를 확인하세요.
)
pause
