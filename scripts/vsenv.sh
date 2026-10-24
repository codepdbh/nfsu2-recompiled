# Source this from Git Bash: imports the MSVC x86 build environment (cl, link,
# INCLUDE, LIB) plus LLVM/clang, so cmake/ninja/clang work from bash.
# Usage: source scripts/vsenv.sh [x86|x64]
_arch="${1:-x86}"
_vcvars="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
_tmp="$(mktemp -d)"
printf '@echo off\r\ncall "%s" %s >nul 2>nul\r\nset\r\n' "$_vcvars" "$_arch" > "$_tmp/e.bat"
while IFS='=' read -r k v; do
  v="${v%$'\r'}"
  case "$k" in
    INCLUDE|LIB|LIBPATH|VCToolsInstallDir|WindowsSdkDir|WindowsSDKVersion|VSINSTALLDIR|VCINSTALLDIR|UCRTVersion|UniversalCRTSdkDir) export "$k=$v";;
    Path|PATH) _winpath="$v";;
  esac
done < <(cmd //c "$(cygpath -w "$_tmp/e.bat")")
# Prepend the MSVC tool dirs (converted) to the bash PATH.
_IFS="$IFS"; IFS=';'; _add=""
for p in $_winpath; do case "$p" in *"Microsoft Visual Studio"*|*"Windows Kits"*) _add="$_add:$(cygpath -u "$p")";; esac; done
IFS="$_IFS"
export PATH="/c/Program Files/LLVM/bin${_add}:$PATH"
_cmk="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake"
export PATH="$_cmk/CMake/bin:$_cmk/Ninja:$PATH"
rm -rf "$_tmp"; unset _tmp _vcvars _arch _winpath _add _IFS _cmk
