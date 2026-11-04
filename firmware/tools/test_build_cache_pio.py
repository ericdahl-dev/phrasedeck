"""Two checkouts sharing one build cache each build their own sources (#175).

Builds a two-line native project in two directories with PlatformIO, so it needs `pio` on PATH
and takes a few seconds: python3 -m unittest discover -s firmware/tools
"""
import os
import shutil
import subprocess
import tempfile
import unittest

BUILD_CACHE_PY = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build_cache.py")


@unittest.skipUnless(shutil.which("pio"), "needs PlatformIO (pio) on PATH")
class TwoCheckoutsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        self.cache = os.path.join(self.tmp, "cache")

    def checkout(self, name):
        project = os.path.join(self.tmp, name)
        os.makedirs(os.path.join(project, "src"))
        with open(os.path.join(project, "platformio.ini"), "w") as f:
            f.write("[platformio]\nbuild_cache_dir = %s\n\n[env:native]\nplatform = native\n"
                    "extra_scripts = pre:%s\n" % (self.cache, BUILD_CACHE_PY))
        return project

    def build_and_run(self, project, text):
        with open(os.path.join(project, "src", "main.cpp"), "w") as f:
            f.write('#include <cstdio>\nint main() { std::puts("%s"); }\n' % text)
        env = dict(os.environ, PLATFORMIO_BUILD_CACHE_DIR=self.cache)
        build = subprocess.run(["pio", "run", "-v", "-e", "native", "-d", project], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(build.returncode, 0, build.stdout)
        self.last_build = build.stdout
        program = os.path.join(project, ".pio", "build", "native", "program")
        return subprocess.run([program], stdout=subprocess.PIPE, text=True, check=True).stdout.strip()

    def test_a_checkout_builds_its_own_sources_after_another_built_them_first(self):
        a, b = self.checkout("a"), self.checkout("b")
        self.assertEqual(self.build_and_run(b, "v1"), "v1")
        self.assertEqual(self.build_and_run(a, "v2"), "v2")
        # B now changes to what A already built, then to something new.
        self.assertEqual(self.build_and_run(b, "v2"), "v2")
        self.assertIn("src/main.o' from cache", self.last_build, "B didn't reuse A's object")
        self.assertEqual(self.build_and_run(b, "v3"), "v3")
        self.assertEqual(self.build_and_run(a, "v1"), "v1")

    def test_each_checkout_keeps_its_own_build_state(self):
        a = self.checkout("a")
        self.build_and_run(a, "v1")
        self.assertFalse([n for n in os.listdir(self.cache) if n.startswith(".sconsign")],
                         "the SCons database is shared through the cache dir")


if __name__ == "__main__":
    unittest.main()
