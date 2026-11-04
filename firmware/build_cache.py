# PlatformIO pre-build script (#175): lets git worktrees share the build cache for libraries too.
#
# SCons keys a cached object by its command line, the contents of its sources and headers, and the
# object's own path. PlatformIO builds each library in $BUILD_DIR/lib<hash>/<name>, where the hash
# is of the library's absolute path. For lib/ and .pio/libdeps (LVGL, LovyanGFX) that path is
# inside the checkout, so every worktree got other object paths and missed the cache. This hashes
# the path relative to the project instead. Libraries outside the project (the framework's, in
# ~/.platformio) keep PlatformIO's name. Only the directory name changes, never what is compiled.
import hashlib
import os
import sys


def pio_lib_build_dir(lib_path):
    """PlatformIO's own name (LibBuilderBase.build_dir in platformio/builder/tools/piolib.py)."""
    lib_hash = hashlib.sha1(lib_path.encode("utf-8")).hexdigest()[:3]
    return os.path.join("$BUILD_DIR", "lib%s" % lib_hash, os.path.basename(lib_path))


def lib_build_dir(lib_path, project_dir):
    lib_path = os.path.normpath(lib_path)
    project_dir = os.path.normpath(project_dir)
    if os.path.commonpath([lib_path, project_dir]) != project_dir:
        return pio_lib_build_dir(lib_path)
    return pio_lib_build_dir(os.path.relpath(lib_path, project_dir))


def make_private(cache_dir):
    """The cache keeps net.cpp.o and firmware.elf, which hold the Wi-Fi password (wifi_secrets.py),
    and PlatformIO makes the directory with the default umask, so only the owner may read it."""
    if cache_dir and os.path.isdir(cache_dir):
        os.chmod(cache_dir, 0o700)


def apply(env):
    make_private(env.subst("$BUILD_CACHE_DIR"))
    # SCons loads PlatformIO's piolib tool from its toolpath under its own module name, so find
    # the class wherever it was loaded rather than importing platformio.builder.tools.piolib.
    classes = {getattr(m, "LibBuilderBase", None) for m in list(sys.modules.values())}
    classes = [c for c in classes if isinstance(c, type) and isinstance(getattr(c, "build_dir", None), property)]
    if not classes:
        # A PlatformIO update changed this: builds stay correct, worktrees just share less.
        print("build_cache: no LibBuilderBase.build_dir in PlatformIO; libraries not shared")
        return
    project_dir = env.subst("$PROJECT_DIR")
    for c in classes:
        c.build_dir = property(lambda lib: lib_build_dir(lib.path, project_dir))


try:
    Import("env")  # noqa: F821 (PlatformIO's SCons global; absent when the tests import this)
except NameError:
    pass
else:
    apply(env)  # noqa: F821
