"""Tests for build_cache.py (#175): python3 -m unittest discover -s firmware/tools"""
import os
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import build_cache  # noqa: E402


class LibBuildDirTest(unittest.TestCase):
    def test_same_library_in_two_checkouts_gets_the_same_build_dir(self):
        a = build_cache.lib_build_dir("/home/x/wt-a/firmware/.pio/libdeps/crowpanel/lvgl", "/home/x/wt-a/firmware")
        b = build_cache.lib_build_dir("/tmp/wt-b/firmware/.pio/libdeps/crowpanel/lvgl", "/tmp/wt-b/firmware")
        self.assertEqual(a, b)
        self.assertTrue(a.startswith(os.path.join("$BUILD_DIR", "lib")))
        self.assertTrue(a.endswith(os.sep + "lvgl"))

    def test_different_libraries_with_the_same_name_get_different_dirs(self):
        project = "/home/x/wt-a/firmware"
        self.assertNotEqual(build_cache.lib_build_dir(project + "/lib/web", project),
                            build_cache.lib_build_dir(project + "/.pio/libdeps/crowpanel/web", project))

    def test_library_outside_the_project_keeps_its_absolute_path(self):
        # Framework libraries live in ~/.platformio, the same for every checkout.
        framework = "/home/x/.platformio/packages/framework-arduinoespressif32/libraries/WiFi"
        a = build_cache.lib_build_dir(framework, "/home/x/wt-a/firmware")
        b = build_cache.lib_build_dir(framework, "/tmp/wt-b/firmware")
        self.assertEqual(a, b)
        self.assertEqual(a, build_cache.pio_lib_build_dir(framework))

    def test_project_dir_that_is_only_a_prefix_is_not_the_project(self):
        self.assertEqual(build_cache.lib_build_dir("/home/x/firmware-old/lib/web", "/home/x/firmware"),
                         build_cache.pio_lib_build_dir("/home/x/firmware-old/lib/web"))


class CacheDirTest(unittest.TestCase):
    def test_cache_dir_is_private_because_objects_hold_the_wifi_password(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = os.path.join(tmp, "pio-build-cache")
            os.makedirs(cache, mode=0o755)
            os.chmod(cache, 0o755)
            build_cache.make_private(cache)
            self.assertEqual(stat.S_IMODE(os.stat(cache).st_mode), 0o700)

    def test_no_cache_dir_is_fine(self):
        build_cache.make_private("")


if __name__ == "__main__":
    unittest.main()
