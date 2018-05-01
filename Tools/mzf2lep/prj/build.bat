@echo off
copy Makefile.Windows ..\src\Makefile
cd ..\src
md ..\bin
nmake release
nmake cleanall
del Makefile
cd ..\prj
