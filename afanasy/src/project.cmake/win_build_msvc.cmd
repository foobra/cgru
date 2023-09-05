rem @echo off
set CMAKE_GENERATOR=Visual Studio 17 2022

pushd ..\..\..
call setup.cmd
popd

setlocal EnableDelayedExpansion

rem Cmake finds 'Qt' by searching for 'qmake' in 'PATH'
@REM set "cgru_qt=C:\Qt"
@REM For /F "Tokens=*" %%I in ('dir /b "%cgru_qt%\5.*"') Do set "cgru_qt=%cgru_qt%\%%I"
@REM set "cgru_qt=%cgru_qt%\msvc2019_64"
@REM if exist !cgru_qt! (
@REM 	echo Adding "!cgru_qt!\bin" to PATH
@REM 	SET "PATH=!cgru_qt!\bin;%PATH%"
@REM )

set "AF_GUI=NO"
set "AF_QT_VER=5"


rem !!! You Should Install Pythonxxx.msi and make sure you enable the "download debug binary" checkbox
rem that will enable python's pythonxxx_d.lib, so vs can build and run debug configuration. !!!

rem Specify Python:
if exist %CGRU_LOCATION%\python-devel set CGRU_PYTHONDIR=%CGRU_LOCATION%\python-devel
if defined CGRU_PYTHONDIR (
   echo Building with CGRU Python: "!CGRU_PYTHONDIR!"
   SET "AF_PYTHON_INCLUDE_PATH=!CGRU_PYTHONDIR!\include"
   SET "AF_PYTHON_LIBRARIES=!CGRU_PYTHONDIR!\libs\python39.lib"
)

rem Boost:
@REM set "cgru_boost_install_dir=C:\local"
@REM For /F "Tokens=*" %%I in ('dir /b "%cgru_boost_install_dir%\boost*"') Do set "BOOST_ROOT=%%I"
@REM if defined BOOST_ROOT (
@REM 	set "BOOST_ROOT=!cgru_boost_install_dir!\!BOOST_ROOT!"
@REM 	echo BOOST_ROOT set to "!BOOST_ROOT!"
@REM )

rem Local overrides:
if exist override.cmd call override.cmd

rem Get Afanasy sources revision:
@REM pushd ..
@REM set folder=%CD%
@REM cd %CGRU_LOCATION%\utilities
@REM call getrevision.cmd %folder%
@REM popd

cmake -G "%CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE=Debug -BDebug

cmake Debug

cmake -G "%CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE=RelWithDebInfo -BRelWithDebInfo

cmake RelWithDebInfo

pause
