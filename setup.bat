set TORCH_INSTALL_PREFIX=%CONDA_PREFIX%\Lib\site-packages\torch
set CMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH%;%cd%;%TORCH_INSTALL_PREFIX%
set PATH=%PATH%;%TORCH_INSTALL_PREFIX%\lib
set FLEXML_OP_DIR=%cd%\tests\flexml
set DD_ROOT=%cd%
set DEVICE=stx