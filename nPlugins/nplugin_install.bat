@echo off
setlocal enabledelayedexpansion
REM ============================================================================
REM  nplugin_install.bat — Setup nplugins cho Manios (Windows)
REM  Tu dong cai nplugin.c tu GitHub
REM ============================================================================

echo.
echo   === nplugin installer (Windows) ===
echo.

REM ── 1. Kiem tra manios ──
set MANIOS_BIN=
for %%p in (manios.exe) do (
    where %%p >nul 2>&1 && set MANIOS_BIN=%%~$PATH:p
)
if "%MANIOS_BIN%"=="" (
    if exist "C:\Program Files\Manios\manios.exe" set MANIOS_BIN=C:\Program Files\Manios\manios.exe
)
if "%MANIOS_BIN%"=="" (
    if exist "%USERPROFILE%\.manios\bin\manios.exe" set MANIOS_BIN=%USERPROFILE%\.manios\bin\manios.exe
)
if "%MANIOS_BIN%"=="" (
    echo   x Manios chua duoc cai dat.
    echo   Cai Manios truoc: https://github.com/anhnoine/n-manios
    pause
    exit /b 1
)
echo   + Manios: %MANIOS_BIN%

REM ── 2. Tim system dir ──
set SHAREDIR=
if exist "%ProgramFiles%\Manios\share" (
    set SHAREDIR=%ProgramFiles%\Manios\share
) else (
    set SHAREDIR=%USERPROFILE%\.manios
)
echo   + System: %SHAREDIR%

REM ── 3. Tao thu muc nplugin ──
if not exist "%USERPROFILE%\.manios\nplugin" mkdir "%USERPROFILE%\.manios\nplugin"
if not exist "%SHAREDIR%\nplugin" mkdir "%SHAREDIR%\nplugin" 2>nul
echo   + Tao: %USERPROFILE%\.manios\nplugin\

REM ── 4. Cai mnos_ext.h ──
if not exist "%SHAREDIR%\include" mkdir "%SHAREDIR%\include"

(
echo #ifndef MNOS_EXT_H
echo #define MNOS_EXT_H
echo #include "mnos.h"
echo typedef struct Val ^(*MnosExtFn^)(struct Val *args, int nargs^);
echo typedef struct { const char *name; const char *doc; MnosExtFn func; } MnosExtFunc;
echo #define MNOS_EXT_EXPORT __attribute__^(^(visibility^("default"^)^)^)
echo #define MNOS_EXT_BEGIN^(extname^) \
echo     MNOS_EXT_EXPORT MnosExtFunc* mnos_ext_init^(int *nfuncs^) { \
echo         static MnosExtFunc funcs[] = {
echo #define MNOS_EXT_FUNC^(fname,cfunc,desc^) { ^(fname^),^(desc^),^(cfunc^) },
echo #define MNOS_EXT_END \
echo             {NULL,NULL,NULL} }; \
echo         *nfuncs=^(int^)^(sizeof^(funcs^)/sizeof^(MnosExtFunc^)^)-1; return funcs; }
echo #endif
) > "%SHAREDIR%\include\mnos_ext.h"

echo   + Cai: %SHAREDIR%\include\mnos_ext.h

REM ── 5. Tao nplugin.bat CLI ──
if not exist "%USERPROFILE%\.manios\bin" mkdir "%USERPROFILE%\.manios\bin"

(
echo @echo off
echo setlocal
echo set NPLUGIN_DIR=%USERPROFILE%\.manios\nplugin
echo set GITHUB_RAW=https://raw.githubusercontent.com/anhnoine/n-manios/main/nPlugins/plugins
echo set INCDIR=%SHAREDIR%\include
echo if "%%1"=="" goto :help
echo if "%%1"=="install" goto :install
echo if "%%1"=="list" goto :list
echo if "%%1"=="uninstall" goto :uninstall
echo goto :help
echo.
echo :help
echo nplugin - Manios Plugin Manager
echo   nplugin install ^<name^>       Cai tu GitHub
echo   nplugin install -f ^<file^>    Cai tu file local
echo   nplugin install -g ^<url^>     Cai tu URL
echo   nplugin list                 Liet ke plugin
echo   nplugin uninstall ^<file^>    Go plugin
echo exit /b 0
echo.
echo :install
echo shift
echo if "%%1"=="-f" goto :install_file
echo if "%%1"=="-g" goto :install_url
echo REM install ^<name^>
echo set NAME=%%1
echo if "%%NAME%%"=="" goto :help
echo echo   + Downloading %%NAME%%.c...
echo curl -sL "%%GITHUB_RAW%%/%%NAME%%.c" -o "%%TEMP%%\nplugin_%%NAME%%.c"
echo if errorlevel 1 ^(echo   x Khong tim thay plugin '%%NAME%%' ^& exit /b 1^)
echo echo   + Compiling...
echo gcc -shared -I"%%INCDIR%%" -o "%%NPLUGIN_DIR%%\%%NAME%%.dll" "%%TEMP%%\nplugin_%%NAME%%.c"
echo if errorlevel 1 ^(echo   x Compile that bai ^& exit /b 1^)
echo del "%%TEMP%%\nplugin_%%NAME%%.c"
echo echo   =^> %%NPLUGIN_DIR%%\%%NAME%%.dll
echo echo   + Da cai!
echo exit /b 0
echo.
echo :install_file
echo set FILE=%%2
echo if "%%FILE%%"=="" ^(echo   x Thieu file ^& exit /b 1^)
echo gcc -shared -I"%%INCDIR%%" -o "%%NPLUGIN_DIR%%\%%~n2.dll" "%%FILE%%"
echo if errorlevel 1 ^(echo   x Compile that bai ^& exit /b 1^)
echo echo   =^> %%NPLUGIN_DIR%%\%%~n2.dll
echo echo   + Da cai!
echo exit /b 0
echo.
echo :install_url
echo set URL=%%2
echo if "%%URL%%"=="" ^(echo   x Thieu URL ^& exit /b 1^)
echo echo   + Downloading %%URL%%...
echo curl -sL "%%URL%%" -o "%%TEMP%%\nplugin_url.c"
echo if errorlevel 1 ^(echo   x Download that bai ^& exit /b 1^)
echo echo   + Compiling...
echo gcc -shared -I"%%INCDIR%%" -o "%%NPLUGIN_DIR%%\nplugin_url.dll" "%%TEMP%%\nplugin_url.c"
echo if errorlevel 1 ^(echo   x Compile that bai ^& exit /b 1^)
echo del "%%TEMP%%\nplugin_url.c"
echo echo   =^> %%NPLUGIN_DIR%%\nplugin_url.dll
echo echo   + Da cai!
echo exit /b 0
echo.
echo :list
echo Plugins da cai (%%NPLUGIN_DIR%%^):
echo dir /b "%%NPLUGIN_DIR%%\*.dll" 2^>nul
echo if errorlevel 1 echo   ^(chua co plugin nao^)
echo exit /b 0
echo.
echo :uninstall
echo set FILE=%%1
echo if "%%FILE%%"=="" ^(echo   x Thieu ten plugin ^& exit /b 1^)
echo if not "%%FILE:~-4%%"==".dll" set FILE=%%FILE%%.dll
echo if exist "%%NPLUGIN_DIR%%\%%FILE%%" ^(
echo     del "%%NPLUGIN_DIR%%\%%FILE%%" ^& echo   + Da go: %%FILE%%
echo ^) else ^(
echo     echo   x Khong tim thay: %%FILE%%
echo     exit /b 1
echo ^)
echo exit /b 0
) > "%USERPROFILE%\.manios\bin\nplugin.bat"

echo   + CLI: %USERPROFILE%\.manios\bin\nplugin.bat

REM ── 6. Auto-install nplugin.c tu GitHub ──
echo.
echo   --- Auto-install nplugin core ---
set NPLUGIN_C_URL=https://raw.githubusercontent.com/anhnoine/n-manios/main/nPlugins/nplugin.c
curl -sL "%NPLUGIN_C_URL%" -o "%TEMP%\nplugin_core.c" 2>nul
if errorlevel 1 (
    echo   ! Khong the tai nplugin.c ^(bo qua^)
) else (
    echo   + Downloaded: nplugin.c
    gcc -shared -I"%SHAREDIR%\include" -o "%USERPROFILE%\.manios\nplugin\nplugin_cmd.dll" "%TEMP%\nplugin_core.c" 2>nul
    if errorlevel 1 (
        echo   ! Compile that bai ^(da co gcc chua?^)
    ) else (
        echo   + Installed: nplugin_cmd.dll
    )
    del "%TEMP%\nplugin_core.c" 2>nul
)

REM ── 7. Xong ──
echo.
echo   =====================================
echo     nplugins: SAN SANG!
echo   =====================================
echo.
echo   Thu muc plugin:  %USERPROFILE%\.manios\nplugin\
echo   Include:         %SHAREDIR%\include\
echo   CLI:             %USERPROFILE%\.manios\bin\nplugin.bat
echo.
echo   Dung trong terminal:
echo     nplugin
echo.
echo   Dung trong Manios:
echo     load_ext("%USERPROFILE:\=/%/.manios/nplugin/my.dll")
echo     list_ext()
echo     nplugin_dir()
echo.

pause
