# griddynMEX

## Prerequisites

- Install [SWIG with MATLAB](https://github.com/jaeandersson/swig/)
- `./configure --prefix=/Users/$USER/local/swig_install; make; make install;`
- Ensure that SWIG and MATLAB are in the PATH

## Building GRIDDYN with matlab extension

GRIDDYN can be build with the matlab extension by enabling the BUILD_MATLAB_EXTENSION option in cmake
GRIDDYN will also need to know the location of swig with MATLAB that was built.

It can also be built without that version of swig using existing files in the repo, but this will not work if there are any library changes
after installing the mex file will be placed in the matlab folder of the install directory
