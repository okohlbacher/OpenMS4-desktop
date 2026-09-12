import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from run import VIEWERS, WORKFLOWS, archive_install, check_install, core_prefix, installed_executable


class PackagingTest(unittest.TestCase):
    def test_archive_and_core_discovery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            core = root / "extracted" / "core"
            core.mkdir(parents=True)
            (core / "source-revision.txt").write_text("a" * 40 + "\n", encoding="utf-8")
            self.assertEqual(core_prefix(root / "extracted"), core)
            payload = root / "payload"
            payload.mkdir()
            (payload / "value").write_text("ok", encoding="utf-8")
            archive = archive_install(payload, root / "dist", "package")
            self.assertTrue(archive.is_file())
            self.assertEqual(len(archive.with_suffix(".gz.sha256").read_text().split()[0]), 64)

    def test_check_install_requires_every_entry_point(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            (prefix / "bin").mkdir()
            for tool in VIEWERS + WORKFLOWS:
                (prefix / "bin" / tool).write_text("", encoding="utf-8")
            with self.assertRaises(ValueError):  # GUI package configuration missing
                check_install(prefix)
            config = prefix / "lib/cmake/OpenMSGUI"
            config.mkdir(parents=True)
            (config / "OpenMSGUIConfig.cmake").write_text("", encoding="utf-8")
            check_install(prefix)
            # macOS ships the same entry point as an application bundle.
            (prefix / "bin" / "TOPPView").unlink()
            bundle = prefix / "bin/TOPPView.app/Contents/MacOS"
            bundle.mkdir(parents=True)
            (bundle / "TOPPView").write_text("", encoding="utf-8")
            self.assertEqual(installed_executable(prefix, "TOPPView"), bundle / "TOPPView")
            check_install(prefix)
            (bundle / "TOPPView").unlink()
            with self.assertRaises(ValueError):
                check_install(prefix)


if __name__ == "__main__":
    unittest.main()
