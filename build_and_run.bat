@echo off
echo === Initializing Visual Studio Environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if %errorlevel% neq 0 (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
    if %errorlevel% neq 0 (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" 2>nul
        if %errorlevel% neq 0 (
            echo ERROR: Visual Studio environment not found!
            echo Please install Visual Studio 2022 or run from Developer Command Prompt
            pause
            exit /b 1
        )
    )
)

echo === Building with Visual Studio Compiler ===
echo.

echo Building storage system...
cl /O2 /std:c11 storage.c /Fe:storage.exe
if %errorlevel% neq 0 (
    echo ERROR: Failed to build storage system
    pause
    exit /b 1
)

echo Building B+ tree...
cl /O2 /std:c11 B+tree.c /Fe:build_bpt.exe
if %errorlevel% neq 0 (
    echo ERROR: Failed to build B+ tree
    pause
    exit /b 1
)

echo Building deletion program...
cl /O2 /std:c11 deletion.c /Fe:deletion.exe
if %errorlevel% neq 0 (
    echo ERROR: Failed to build deletion program
    pause
    exit /b 1
)

echo.
echo === Running Complete Database Task Workflow ===
echo.

echo Step 1: Loading data into database...
storage.exe
if %errorlevel% neq 0 (
    echo ERROR: Storage system failed
    pause
    exit /b 1
)

echo.
echo Step 2: Building B+ tree index...
build_bpt.exe database.bin bpt.idx
if %errorlevel% neq 0 (
    echo ERROR: B+ tree building failed
    pause
    exit /b 1
)

echo.
echo Step 3: Running deletion task...
deletion.exe database.bin bpt.idx
if %errorlevel% neq 0 (
    echo ERROR: Deletion task failed
    pause
    exit /b 1
)

echo.
echo === All tasks completed successfully! ===
pause
