# GridDyn: CPack/NSIS Installer & Automated Testing

## Table of Contents

- [Included Files](https://github.com/jspero/LLNL#included-files)
- [Rebuilding the Installer](https://github.com/jspero/LLNL#rebuilding-the-installer)
- [Editing the Installer](https://github.com/jspero/LLNL#editing-the-installer)
  - [Expansion to component-based installation](https://github.com/jspero/LLNL#expansion-to-component-based-installation)
  - [Adding files](https://github.com/jspero/LLNL#adding-files)
  - [Start menu shortcuts](https://github.com/jspero/LLNL#start-menu-shortcuts)
  - [Modifying the user interface and graphics](https://github.com/jspero/LLNL/blob/master/README.md#modifying-the-user-interface-and-graphics)
- [CTest](https://github.com/jspero/LLNL#ctest)
- [Notes & Known Issues](https://github.com/jspero/LLNL#notes--known-issues)
- [Helpful Pages](https://github.com/jspero/LLNL#helpful-pages)

## Included Files

- **NSIS**: contains all the graphics used in the installer, along with alternates. Should be copied into NSIS root directory (subdirectory structures are the same).
  - **Contrib/Graphics**: Contains all of the graphics used in the installer, plus some alternates not currently in use.
  - **Docs/GridDyn**: Contains the licenses for GridDyn and KLU (Suitesparse).
- **CMakeLists.txt**: for main GridDyn directory.
- **GridDyn-0.6-win64.exe**: most recent version of the installer built from CPack (PACKAGE project in GridDyn Visual Studio solution).
- **GridDyn_NSIS_Format_Test.nsi**: allows GUI testing/previews without actually installing anything.
- **NSIS.template.in**: GridDyn-formatted NSIS template for use by CMake and CPack. Stored in _..\CMake\share\cmake-3.8\Modules_.
- **NSIS.template.nsi**: Syntax-aware copy of GridDyn-formatted NSIS template for editing purposes. Updates to this file should be followed by a copy and renaming of this file to **NSIS.template.in**.

## Rebuilding the Installer

### Prerequisites

1. CMake, NSIS, and Visual Studio should be installed and the experimental branch of the GridDyn repo should be cloned. CMake 3.8 and Visual Studio 2015 Community have been verified to this point.
2. The **NSIS.template.in** file included in this repo should be copied to _..\CMake\share\cmake-3.8\Modules_.
3. The **NSIS** folder included in this repo should be copied on top of the local NSIS root directory to add the necessary GridDyn graphics and documents for use in the installer.
4. The last three sections of the **CMakeLists.txt** file in this repo should be copied into the CMakeLists.txt file in the top-level of the GridDyn repo:

```CMake
# -------------------------------------------------------------
# Future Additions (by Jen)
# -------------------------------------------------------------

#adding dlls
# INSTALL(FILES ${LOCATION_OF_FILES} DESTINATION bin)
FILE(GLOB docs "docs/manuals/*")
INSTALL(FILES ${docs} DESTINATION docs)

# -------------------------------------------------------------
# CTest
# -------------------------------------------------------------

enable_testing()
add_test(NAME testComponents
         COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/testComponents.exe)
add_test(NAME testLibrary
         COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/testLibrary.exe)
add_test(NAME testSystem
         COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/testSystem.exe)

# -------------------------------------------------------------
# CPack for NSIS Installer
# -------------------------------------------------------------

set(CPACK_PACKAGE_NAME "GridDyn")
set(CPACK_PACKAGE_VENDOR "Lawrence Livermore National Security")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GridDyn Installer")
set(CPACK_PACKAGE_VERSION "${GridDyn_VERSION_MAJOR}.${GridDyn_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_MAJOR ${GridDyn_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${GridDyn_VERSION_MINOR})
#set(CPACK_PACKAGE_VERSION_PATCH ${GridDyn_VERSION_PATCH})

#THIS LINE MUST BE LAST
include(CPack)
```

### Main Process

1. Use CMake to create GridDyn Visual Studio solution.
2. Build Solution in VS.
3. Build PACKAGE in VS (This project is excluded from the solution configuration by default because repackaging the installer may not always be desired).
4. The new installer executable will be located in build directory.

## Editing the Installer

### Expansion to component-based installation

Currently, the GridDyn installer treats all files in the package as one unit, and it does not allow the user to install only certain parts of the package. As additional features and libraries are added to GridDyn, a component-based installation may be necessary. In order to add this functionality, both the **NSIS.template.in** file and the **CMakeLists.txt** file in the main GridDyn directory must be updated. For complete documentation on this process, view CMake's page on [Component Install with CPack](https://cmake.org/Wiki/CMake:Component_Install_With_CPack).

**NSIS.template.in**: For this file, only a quick modification is needed. Add this line to the _Pages_ section:

```nsis
!insertmacro MUI_PAGE_COMPONENTS
```

**CMakeLists.txt**: The CMake/CPack side of component installation requires a little more work. CMake's [Component Install with CPack](https://cmake.org/Wiki/CMake:Component_Install_With_CPack) covers these options well.

### Adding files

Adding files to be included in the installation process requires only a quick modification of the **CMakeLists.txt** file in the main GridDyn directory. See, for example, how the documentation was added:

```CMake
FILE(GLOB docs "docs/manuals/*")
INSTALL(FILES ${docs} DESTINATION docs)
```

### Start menu shortcuts

The installer is currently configured to create a start menu folder titled based on the application name and version number (i.e. GridDyn 0.6) in **NSIS.template.in**. A shortcut to the uninstaller is then added to this folder:

```nsis
  ;Create shortcuts
  CreateDirectory "$SMPROGRAMS\$STARTMENU_FOLDER"
  CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
```

Additional shortcuts may be added following the `CreateShortCut` format above. For more information, see the [CreateShortCut Reference Page](http://nsis.sourceforge.net/Reference/CreateShortCut) from NSIS.

### Modifying the user interface and graphics

The installer currently utilizes [NSIS Modern UI](http://nsis.sourceforge.net/Docs/Modern%20UI/Readme.html). Quick changes to installer pages may be made in **NSIS.template.in** under the section commented as `Interface Settings`. As seen below, these settings also allow changes to the graphics used throughout the installation process.

```nsis
;Interface Settings

  !define MUI_HEADERIMAGE
  !define MUI_HEADERIMAGE_RIGHT
  !define MUI_HEADERIMAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Header\GridDyn_header.bmp"
  !define MUI_ABORTWARNING
  !define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\GridDyn_inst_invert.ico"
  !define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\GridDyn_uninst_invert.ico"
  !define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\welcomefinish.bmp"

  !define MUI_FINISHPAGE_LINK "View the GridDyn User Manual"
  !define MUI_FINISHPAGE_LINK_LOCATION "file:///$INSTDIR/docs/GridDynUserManual.pdf"
```

Detailed information about page configuration may be found in the [NSIS Modern UI Readme](http://nsis.sourceforge.net/Docs/Modern%20UI/Readme.html) under Pages/Page settings.

## CTest

Tests may be easily combined into a package through a few lines of code at the end of the **CMakeLists.txt** file in the main GridDyn directory:

```CMake
enable_testing()
add_test(NAME testComponents
         COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/testComponents.exe)
add_test(NAME testLibrary
         COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/testLibrary.exe)
add_test(NAME testSystem
         COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/testSystem.exe)
```

This will create a package within the Visual Studio solution by the name of RUN_TESTS.

## Notes & Known Issues

- **Test executables** (_..\bin\test_) are currently not operational when GridDyn is installed through this installer. This is because the file locations are hardcoded during the build process of the original GridDyn solution. The main functionality of GridDyn itself, though, should be fully operational after installation is complete.

## Helpful Pages

- [NSIS variables in CPack](https://cmake.org/cmake/help/v3.0/module/CPackNSIS.html)
- [General CPack variables](https://cmake.org/cmake/help/v3.0/module/CPack.html)
- [NSIS Documentation](http://nsis.sourceforge.net/Docs/)
