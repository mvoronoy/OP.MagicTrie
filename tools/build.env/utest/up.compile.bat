set trpath=%~dp0%
echo %%~dp0 is %trpath%
pushd .
cd ..\..\..
set tr_root=%CD%
echo tr_root %tr_root%
popd

rem docker run -it -v %tr_root%:/src/project --rm utest /bin/bash
docker run -it -v %tr_root%:/src/project --rm op_devenv /bin/bash