# Repository Guidelines

## Build Notes

- This repository uses CMake, and the common local Windows build entry point is `cmake --build build --config Debug --parallel 4`.
- On some Windows environments, MSBuild can fail before compilation with `MSB6001` and a duplicate-environment-key error mentioning both `Path` and `PATH`.
- Only if that exact issue appears, rerun the build from `cmd` with the mixed-case `Path` entry cleared for the child process:
  `cmd /v:on /c "set Path=& cmake --build build --config Debug --parallel 4"`
- Use the same pattern for other CMake build targets when needed, for example:
  `cmd /v:on /c "set Path=& cmake --build build --config Debug --target NetworkComponentTests --clean-first --parallel 4"`
- If MSBuild still reports the `Path`/`PATH` collision, the existing `build-ninja` tree can compile with MSVC directly. From PowerShell on this machine, load the Visual Studio tools and build a target with:
  `cmd /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build build-ninja --target ModelComparisonTests --parallel 4'`
- Do not assume every build needs this workaround; use normal build commands unless the `Path`/`PATH` collision is actually present.
