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
            exec python3 -c 'import signal, sys, time; print(sys.argv[1]); sys.stdout.flush(); signal.signal(signal.SIGINT, lambda *_: sys.exit(0)); signal.signal(signal.SIGTERM, lambda *_: sys.exit(0)); time.sleep(2)' "$0"
            """
        else:
            body = """
            #!/usr/bin/env bash
            set -euo pipefail
            echo "$0"
            """

        path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def make_sleeping_test(self, path: Path) -> None:
        body = """
        #!/usr/bin/env bash
        set -euo pipefail
        trap '' TERM
        sleep 3600
        """
        path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def make_quick_test(self, path: Path) -> None:
        body = """
        #!/usr/bin/env bash
        set -euo pipefail
        echo "$0"
        """
        path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def make_long_running_server(self, path: Path) -> None:
        body = """
        #!/usr/bin/env bash
        set -euo pipefail
        exec python3 -c 'import signal, sys, time; signal.signal(signal.SIGINT, lambda *_: sys.exit(0)); signal.signal(signal.SIGTERM, lambda *_: sys.exit(0)); time.sleep(3600)'
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
                timeout=45,
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

    def test_matrix_ignores_stale_binaries_not_backed_by_current_sources(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_dir = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            test_names = sorted(path.stem for path in TEST_SOURCE_DIR.glob("T*.cc"))
            for name in test_names:
                self.make_fake_test(bin_dir / name)

            stale_name = "T999-stale_artifact"
            self.make_fake_test(bin_dir / stale_name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    str(build_dir),
                    str(log_dir),
                ],
                cwd=PROJECT_ROOT,
                capture_output=True,
                text=True,
                timeout=45,
                env=os.environ.copy(),
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            self.assertFalse((log_dir / f"{stale_name}.log").exists())
            self.assertNotIn(stale_name, result.stdout)

    def test_standalone_timeout_does_not_hang_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_dir = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            self.make_sleeping_test(bin_dir / "T2-tcp_socket")
            for name in ("T3-tcp_server", "T4-tcp_client", "T6-udp_server", "T7-udp_client"):
                self.make_quick_test(bin_dir / name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    str(build_dir),
                    str(log_dir),
                ],
                cwd=PROJECT_ROOT,
                capture_output=True,
                text=True,
                timeout=8,
                env={
                    **os.environ,
                    "GALAY_TEST_TIMEOUT_SECONDS": "1",
                    "GALAY_TEST_TIMEOUT_KILL_AFTER_SECONDS": "1",
                },
            )

            self.assertNotEqual(result.returncode, 0)
            timeout_log = log_dir / "T2-tcp_socket.log"
            self.assertTrue(timeout_log.is_file())
            self.assertIn("[test-timeout]", timeout_log.read_text(encoding="utf-8"))

    def test_paired_client_timeout_does_not_hang_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_dir = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            self.make_long_running_server(bin_dir / "T3-tcp_server")
            self.make_sleeping_test(bin_dir / "T4-tcp_client")
            for name in ("T6-udp_server", "T7-udp_client"):
                self.make_quick_test(bin_dir / name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    str(build_dir),
                    str(log_dir),
                ],
                cwd=PROJECT_ROOT,
                capture_output=True,
                text=True,
                timeout=8,
                env={
                    **os.environ,
                    "GALAY_TEST_TIMEOUT_SECONDS": "1",
                    "GALAY_TEST_TIMEOUT_KILL_AFTER_SECONDS": "1",
                },
            )

            self.assertNotEqual(result.returncode, 0)
            timeout_log = log_dir / "T4-tcp_client.log"
            self.assertTrue(timeout_log.is_file())
            self.assertIn("[test-timeout]", timeout_log.read_text(encoding="utf-8"))

    def test_paired_server_timeout_does_not_hang_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_dir = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            self.make_long_running_server(bin_dir / "T3-tcp_server")
            self.make_quick_test(bin_dir / "T4-tcp_client")
            for name in ("T6-udp_server", "T7-udp_client"):
                self.make_quick_test(bin_dir / name)
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    str(build_dir),
                    str(log_dir),
                ],
                cwd=PROJECT_ROOT,
                capture_output=True,
                text=True,
                timeout=8,
                env={
                    **os.environ,
                    "GALAY_TEST_TIMEOUT_SECONDS": "1",
                    "GALAY_TEST_TIMEOUT_KILL_AFTER_SECONDS": "1",
                },
            )

            self.assertNotEqual(result.returncode, 0)
            timeout_log = log_dir / "T3-tcp_server.log"
            self.assertTrue(timeout_log.is_file())
            self.assertIn("[test-timeout]", timeout_log.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
