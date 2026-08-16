set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
# Workaround for broken vswhere registration on this machine:
# vcpkg discovers the instance via user-level env VS170COMNTOOLS pointing at Build Tools 2022.
set(VCPKG_PLATFORM_TOOLSET v143)