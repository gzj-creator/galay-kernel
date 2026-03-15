import os
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "run_test_matrix.sh"
PROJECT_ROOT = SCRIPT_PATH.parents[1]
TEST_SOURCE_DIR = PROJECT_ROOT / "test"

PAIRED_TESTS = {
    "T3-tcp_server",
    "T4-tcp_client",
    "T6-udp_server",
    "T7-udp_client",
}


class RunTestMatrixTest(unittest.TestCase):
    def make_fake_test(self, path: Path) -> None:
        if path.name in {"T3-tcp_server", "T6-udp_server"}:
            body = """
            #!/usr/bin/env bash
            set -euo pipefail
            exec python3 -c 'import signal, sys, time; print(sys.argv[1]); sys.stdout.flush(); signal.signal(signal.SIGINT, lambda *_: sys.exit(0)); signal.signal(signal.SIGTERM, lambda *_: sys.exit(0)); time.sleep(5)' "$0"
            """
        else:
            body = """
            #!/usr/bin/env bash
            set -euo pipefail
            echo "$0"
            """

        path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def test_matrix_runs_all_current_tests(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_dir = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            test_names = sorted(path.stem for path in TEST_SOURCE_DIR.glob("T*.cc"))
            for name in test_names:
                self.make_fake_test(bin_dir / name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    str(build_dir),
                    str(log_dir),
                ],
                cwd=PROJECT_ROOT,
                capture_output=True,
                text=True,
                timeout=20,
                env=os.environ.copy(),
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )

            missing_logs = [
                name for name in test_names
                if not (log_dir / f"{name}.log").is_file()
            ]
            self.assertEqual([], missing_logs)

            for name in PAIRED_TESTS:
                self.assertTrue((log_dir / f"{name}.log").is_file())


if __name__ == "__main__":
    unittest.main()
