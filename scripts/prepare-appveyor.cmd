::
:: HEIF codec.
:: Copyright (c) 2018 struktur AG, Joachim Bauch <bauch@struktur.de>
::
:: This file is part of libheif.
::
:: libheif is free software: you can redistribute it and/or modify
:: it under the terms of the GNU General Public License as published by
:: the Free Software Foundation, either version 3 of the License, or
:: (at your option) any later version.
::
:: libheif is distributed in the hope that it will be useful,
:: but WITHOUT ANY WARRANTY; without even the implied warranty of
:: MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
:: GNU General Public License for more details.
::
:: You should have received a copy of the GNU General Public License
:: along with libheif.  If not, see <http://www.gnu.org/licenses/>.
::
setlocal

set RESULT=0
set LIBDE265_VERSION=1.0.3
set LIBDE265_SOURCE=https://github.com/strukturag/libde265/archive/v%LIBDE265_VERSION%.zip
set LIBDE265_DESTINATION=libde265-%LIBDE265_VERSION%.zip

echo Downloading libde265 %LIBDE265_VERSION% from %LIBDE265_SOURCE% to %LIBDE265_DESTINATION% ...
curl -L -o "%LIBDE265_DESTINATION%" "%LIBDE265_SOURCE%"
if %ERRORLEVEL% neq 0 goto error
7z x "%LIBDE265_DESTINATION%"
if %ERRORLEVEL% neq 0 goto error

echo Building libde265 in libde265-%LIBDE265_VERSION% ...
cd "libde265-%LIBDE265_VERSION%"
set LIBDE265_BUILD=%APPVEYOR_BUILD_FOLDER%\build-libde265
cmake "-G%GENERATOR%%CMAKE_GEN_SUFFIX%" -H. "-B%LIBDE265_BUILD%"
if %ERRORLEVEL% neq 0 goto error
cmake --build "%LIBDE265_BUILD%" --config %CONFIGURATION%
if %ERRORLEVEL% neq 0 goto error
copy /y libde265\de265.h "%LIBDE265_BUILD%\libde265"
copy /y libde265\en265.h "%LIBDE265_BUILD%\libde265"
copy /y "%LIBDE265_BUILD%\libde265\%CONFIGURATION%\libde265.*" "%LIBDE265_BUILD%"

goto done


:error
echo error: %ERRORLEVEL%
set RESULT=%ERRORLEVEL%

:done
exit /b %EL%
