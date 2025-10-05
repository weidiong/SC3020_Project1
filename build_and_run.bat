@echo off
echo === Building with Visual Studio Compiler ===
echo.

echo Building storage system...
cl /O2 storage.c /Fe:storage.exe
echo Building B+ tree...
cl /O2 B+tree.c /Fe:build_bpt.exe
echo Building deletion program...
cl /O2 deletion.c /Fe:deletion.exe

echo.
echo === Running Workflow ===
echo Step 1: Loading data...
storage.exe
echo Step 2: Building index...
build_bpt.exe database.bin bpt.idx
echo Step 3: Running deletion...
deletion.exe database.bin bpt.idx

echo.
echo === All tasks completed! ===
pause
