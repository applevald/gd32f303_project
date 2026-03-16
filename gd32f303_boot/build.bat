@echo off
echo Building GD32F303 project...
set IAR_PATH="C:\Program Files (x86)\IAR Systems\Embedded Workbench 8.2\common\bin\IarBuild.exe"
if exist %IAR_PATH% (
    %IAR_PATH% gd32f303.ewp -build Debug
) else (
    echo IAR Build not found at %IAR_PATH%
    echo Searching for IAR Build...
    where iarbuild
)
pause