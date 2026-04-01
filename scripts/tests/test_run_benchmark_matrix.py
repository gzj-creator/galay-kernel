import os
import stat
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "run_benchmark_matrix.sh"

STANDALONE_BENCHMARKS = [
    "B1-ComputeScheduler",
    "B6-Udp",
    "B7-FileIo",
    "B8-MpscChannel",
    "B9-UnsafeChannel",
    "B10-Ringbuffer",
    "B13-Sendfile",
    "B14-SchedulerInjectedWakeup",
]

PAIRED_BENCHMARKS = [
    "B2-TcpServer",
    "B3-TcpClient",
    "B4-UdpServer",
    "B5-UdpClient",
    "B11-TcpIovServer",
    "B12-TcpIovClient",
]


class RunBenchmarkMatrixTest(unittest.TestCase):
    def make_fake_benchmark(self, path: Path) -> None:
        if path.name.endswith("Server"):
            body = """
            #!/usr/bin/env bash
            set -euo pipefail
            exec python3 -c 'import signal, sys, time; print(sys.argv[1], " ".join(sys.argv[2:])); sys.stdout.flush(); signal.signal(signal.SIGINT, lambda *_: sys.exit(0)); signal.signal(signal.SIGTERM, lambda *_: sys.exit(0)); time.sleep(3600)' "$0" "$@"
            """
        else:
            body = """
            #!/usr/bin/env bash
            set -euo pipefail
            echo "$0 $*"
            """

        path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def make_sleeping_client(self, path: Path) -> None:
        body = """
        #!/usr/bin/env bash
        set -euo pipefail
        trap '' TERM
        sleep 3600
        """
        path.write_text(textwrap.dedent(body).lstrip(), encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def test_allow_missing_runs_without_skip_arguments(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            log_root = tmp_path / "logs"
            (build_dir / "bin").mkdir(parents=True)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--build-dir",
                    str(build_dir),
                    "--log-root",
                    str(log_root),
                    "--allow-missing",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=8,
                env=os.environ.copy(),
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            self.assertTrue((log_root / "B1-ComputeScheduler.log").is_file())

    def test_repeat_runs_write_logs_under_labeled_directories(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_root = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            for name in STANDALONE_BENCHMARKS + PAIRED_BENCHMARKS:
                self.make_fake_benchmark(bin_dir / name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--build-dir",
                    str(build_dir),
                    "--log-root",
                    str(log_root),
                    "--repeat",
                    "2",
                    "--label",
                    "refactored",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
                env={**os.environ, "BENCH_TIMEOUT_SECONDS": "5"},
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            self.assertTrue(
                (log_root / "refactored" / "run-1" / "B1-ComputeScheduler.log").is_file()
            )
            self.assertTrue(
                (log_root / "refactored" / "run-2" / "B2-TcpServer.log").is_file()
            )
            self.assertTrue(
                (log_root / "refactored" / "run-2" / "B12-TcpIovClient.log").is_file()
            )

    def test_explicit_run_index_writes_logs_under_requested_directory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_root = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            for name in STANDALONE_BENCHMARKS + PAIRED_BENCHMARKS:
                self.make_fake_benchmark(bin_dir / name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--build-dir",
                    str(build_dir),
                    "--log-root",
                    str(log_root),
                    "--repeat",
                    "1",
                    "--run-index",
                    "3",
                    "--label",
                    "baseline",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
                env={**os.environ, "BENCH_TIMEOUT_SECONDS": "5"},
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            self.assertTrue((log_root / "baseline" / "run-3" / "B1-ComputeScheduler.log").is_file())
            self.assertFalse((log_root / "baseline" / "run-1" / "B1-ComputeScheduler.log").exists())

    def test_paired_client_timeout_does_not_hang_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_root = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            for name in STANDALONE_BENCHMARKS + PAIRED_BENCHMARKS:
                self.make_fake_benchmark(bin_dir / name)

            self.make_sleeping_client(bin_dir / "B5-UdpClient")

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--build-dir",
                    str(build_dir),
                    "--log-root",
                    str(log_root),
                    "--repeat",
                    "1",
                    "--label",
                    "baseline",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=8,
                env={
                    **os.environ,
                    "BENCH_TIMEOUT_SECONDS": "1",
                    "BENCH_TIMEOUT_KILL_AFTER_SECONDS": "1",
                    "BENCH_SERVER_STARTUP_SECONDS": "0.1",
                },
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            timeout_log = log_root / "baseline" / "run-1" / "B5-UdpClient.log"
            self.assertTrue(timeout_log.is_file())
            self.assertIn("[benchmark-timeout]", timeout_log.read_text(encoding="utf-8"))

    def test_skip_benchmark_skips_entire_paired_udp_group(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_root = tmp_path / "logs"
            bin_dir.mkdir(parents=True)

            for name in STANDALONE_BENCHMARKS + PAIRED_BENCHMARKS:
                self.make_fake_benchmark(bin_dir / name)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--build-dir",
                    str(build_dir),
                    "--log-root",
                    str(log_root),
                    "--repeat",
                    "1",
                    "--label",
                    "baseline",
                    "--skip-benchmark",
                    "B5-UdpClient",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
                env={**os.environ, "BENCH_TIMEOUT_SECONDS": "5"},
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            server_log = log_root / "baseline" / "run-1" / "B4-UdpServer.log"
            client_log = log_root / "baseline" / "run-1" / "B5-UdpClient.log"
            self.assertTrue(server_log.is_file())
            self.assertTrue(client_log.is_file())
            self.assertIn("[benchmark-skipped]", server_log.read_text(encoding="utf-8"))
            self.assertIn("[benchmark-skipped]", client_log.read_text(encoding="utf-8"))

    def test_timeout_command_uses_kill_after_grace_period(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            build_dir = tmp_path / "build"
            bin_dir = build_dir / "bin"
            log_root = tmp_path / "logs"
            fake_bin = tmp_path / "fake-bin"
            timeout_args = tmp_path / "timeout-args.txt"
            bin_dir.mkdir(parents=True)
            fake_bin.mkdir()

            for name in STANDALONE_BENCHMARKS:
                self.make_fake_benchmark(bin_dir / name)

            fake_timeout = fake_bin / "timeout"
            fake_timeout.write_text(
                textwrap.dedent(
                    f"""\
                    #!/usr/bin/env bash
                    set -euo pipefail
                    printf '%s\n' "$*" > "{timeout_args}"
                    exit 124
                    """
                ),
                encoding="utf-8",
            )
            fake_timeout.chmod(fake_timeout.stat().st_mode | stat.S_IXUSR)

            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--build-dir",
                    str(build_dir),
                    "--log-root",
                    str(log_root),
                    "--allow-missing",
                    "--skip-benchmark",
                    "B14-SchedulerInjectedWakeup",
                    "--skip-benchmark",
                    "B8-MpscChannel",
                    "--skip-benchmark",
                    "B6-Udp",
                    "--skip-benchmark",
                    "B10-Ringbuffer",
                    "--skip-benchmark",
                    "B13-Sendfile",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=8,
                env={
                    **os.environ,
                    "PATH": f"{fake_bin}:{os.environ['PATH']}",
                    "BENCH_TIMEOUT_SECONDS": "3",
                    "BENCH_TIMEOUT_KILL_AFTER_SECONDS": "2",
                },
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            self.assertTrue(timeout_args.is_file())
            args = timeout_args.read_text(encoding="utf-8").strip().split()
            self.assertGreaterEqual(len(args), 3)
            self.assertEqual(args[0], "-k")
            self.assertEqual(args[1], "2s")
            self.assertEqual(args[2], "3s")


if __name__ == "__main__":
    unittest.main()
