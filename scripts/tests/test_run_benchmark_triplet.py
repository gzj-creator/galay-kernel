import subprocess
import tempfile
import unittest
from pathlib import Path
import re


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "run_benchmark_triplet.sh"


class RunBenchmarkTripletTest(unittest.TestCase):
    def test_dry_run_forwards_timeout_seconds_to_matrix_commands(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--backend",
                    "kqueue",
                    "--benchmark",
                    "B5-UdpClient",
                    "--repeat",
                    "1",
                    "--timeout-seconds",
                    "123",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            matrix_lines = [
                line
                for line in result.stdout.splitlines()
                if "scripts/run_benchmark_matrix.sh --build-dir" in line
            ]
            self.assertEqual(2, len(matrix_lines))
            for line in matrix_lines:
                self.assertIn("--timeout-seconds 123", line)

    def test_dry_run_benchmark_filter_keeps_only_selected_paired_benchmark(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--backend",
                    "kqueue",
                    "--benchmark",
                    "B5-UdpClient",
                    "--repeat",
                    "1",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            stdout = result.stdout
            matrix_lines = [
                line
                for line in stdout.splitlines()
                if "scripts/run_benchmark_matrix.sh --build-dir" in line
            ]
            self.assertEqual(2, len(matrix_lines))
            self.assertIn("--label baseline", "\n".join(matrix_lines))
            self.assertIn("--label refactored", "\n".join(matrix_lines))
            for line in matrix_lines:
                self.assertIn("--skip-benchmark B1-ComputeScheduler", line)
                self.assertIn("--skip-benchmark B2-TcpServer", line)
                self.assertIn("--skip-benchmark B3-TcpClient", line)
                self.assertNotIn("--skip-benchmark B4-UdpServer", line)
                self.assertNotIn("--skip-benchmark B5-UdpClient", line)
                self.assertIn("--skip-benchmark B6-Udp", line)
                self.assertIn("--skip-benchmark B7-FileIo", line)
                self.assertIn("--skip-benchmark B8-MpscChannel", line)
                self.assertIn("--skip-benchmark B9-UnsafeChannel", line)
                self.assertIn("--skip-benchmark B10-Ringbuffer", line)
                self.assertIn("--skip-benchmark B11-TcpIovServer", line)
                self.assertIn("--skip-benchmark B12-TcpIovClient", line)
                self.assertIn("--skip-benchmark B13-Sendfile", line)
                self.assertIn("--skip-benchmark B14-SchedulerInjectedWakeup", line)

            self.assertNotIn("compat-b1/", stdout)
            self.assertNotIn("compat-b6/", stdout)
            self.assertNotIn("compat-b8/", stdout)
            self.assertNotIn("compat-b10/", stdout)
            self.assertNotIn("compat-b14/", stdout)

    def test_dry_run_benchmark_filter_keeps_only_selected_compat_benchmark(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--backend",
                    "epoll",
                    "--benchmark",
                    "B8-MpscChannel",
                    "--repeat",
                    "1",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            stdout = result.stdout
            self.assertNotIn("scripts/run_benchmark_matrix.sh --build-dir", stdout)
            self.assertIn("compat-b8/refactored", stdout)
            self.assertIn("compat-b8/baseline", stdout)
            self.assertNotIn("compat-b1/", stdout)
            self.assertNotIn("compat-b6/", stdout)
            self.assertNotIn("compat-b10/", stdout)
            self.assertNotIn("compat-b14/", stdout)

    def test_dry_run_honors_compat_backend_define_override(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--repeat",
                    "1",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                env={
                    **dict(__import__("os").environ),
                    "GALAY_TRIPLET_COMPAT_BACKEND_DEFINE": "USE_IOURING",
                },
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            compat_lines = "\n".join(
                line
                for line in result.stdout.splitlines()
                if "B1-ComputeScheduler-Compat" in line and " c++ " in f" {line} "
            )
            self.assertIn("-DUSE_IOURING", compat_lines)
            self.assertNotIn("-DUSE_EPOLL", compat_lines)

    def test_dry_run_can_request_io_uring_backend(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--backend",
                    "io_uring",
                    "--repeat",
                    "1",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertIn("-DDISABLE_IOURING=OFF", result.stdout)

            compat_lines = "\n".join(
                line
                for line in result.stdout.splitlines()
                if "B1-ComputeScheduler-Compat" in line and " c++ " in f" {line} "
            )
            self.assertIn("-DUSE_IOURING", compat_lines)

    def test_io_uring_dry_run_keeps_b6_udp_baseline_compat_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--backend",
                    "io_uring",
                    "--repeat",
                    "1",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            stdout = result.stdout
            matrix_lines = [
                line
                for line in stdout.splitlines()
                if "scripts/run_benchmark_matrix.sh --build-dir" in line
            ]
            joined_matrix = "\n".join(matrix_lines)

            self.assertRegex(
                joined_matrix,
                r"--label baseline .*--skip-benchmark B4-UdpServer .*--skip-benchmark B5-UdpClient",
            )

            refactored_line = next(line for line in matrix_lines if "--label refactored" in line)
            self.assertNotIn("--skip-benchmark B4-UdpServer", refactored_line)
            self.assertNotIn("--skip-benchmark B5-UdpClient", refactored_line)

            compat_run_lines = "\n".join(
                line
                for line in stdout.splitlines()
                if "env " in line and "B6-Udp-Compat" in line
            )
            self.assertIn("compat-b6/refactored/B6-Udp-Compat", compat_run_lines)
            self.assertIn("compat-b6/baseline/B6-Udp-Compat", compat_run_lines)

    def test_dry_run_prints_worktree_build_matrix_and_compat_steps(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--repeat",
                    "3",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            stdout = result.stdout

            self.assertRegex(stdout, r"baseline source: .*baseline-bench")
            self.assertIn("cmake -S", stdout)
            self.assertIn("cmake --build", stdout)
            self.assertIn("scripts/run_benchmark_matrix.sh --build-dir", stdout)
            self.assertIn("--repeat 1", stdout)
            self.assertIn("--run-index 1", stdout)
            self.assertIn("--run-index 2", stdout)
            self.assertIn("--run-index 3", stdout)
            self.assertIn("--label baseline", stdout)
            self.assertIn("--label refactored", stdout)
            self.assertIn("--skip-benchmark B1-ComputeScheduler", stdout)
            self.assertIn("--skip-benchmark B6-Udp", stdout)
            self.assertIn("--skip-benchmark B8-MpscChannel", stdout)
            self.assertIn("--skip-benchmark B10-Ringbuffer", stdout)
            self.assertIn("--skip-benchmark B14-SchedulerInjectedWakeup", stdout)
            self.assertIn("compat-b1/refactored", stdout)
            self.assertIn("compat-b1/baseline", stdout)
            self.assertIn("compat-b6/refactored", stdout)
            self.assertIn("compat-b6/baseline", stdout)
            self.assertIn("compat-b8/refactored", stdout)
            self.assertIn("compat-b8/baseline", stdout)
            self.assertIn("compat-b10/refactored", stdout)
            self.assertIn("compat-b10/baseline", stdout)
            self.assertIn("compat-b14/refactored", stdout)
            self.assertIn("compat-b14/baseline", stdout)
            self.assertIn("B1-ComputeScheduler-Compat", stdout)
            self.assertIn("B6-Udp-Compat", stdout)
            self.assertIn("B8-MpscChannel-Compat", stdout)
            self.assertIn("B10-Ringbuffer-Compat", stdout)
            self.assertIn("B14-SchedulerInjectedWakeup-Compat", stdout)

    def test_dry_run_rotates_compat_label_order_for_b1(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--repeat",
                    "3",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)

            label_order = re.findall(
                r"compat-b1/(baseline|refactored)/B1-ComputeScheduler-Compat",
                "\n".join(
                    line
                    for line in result.stdout.splitlines()
                    if "env " in line and "B1-ComputeScheduler-Compat" in line
                ),
            )

            self.assertEqual(
                [
                    "baseline",
                    "refactored",
                    "refactored",
                    "baseline",
                    "baseline",
                    "refactored",
                ],
                label_order,
            )

    def test_dry_run_rotates_main_matrix_label_order_per_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--repeat",
                    "3",
                    "--output-root",
                    str(output_root),
                    "--dry-run",
                ],
                cwd=SCRIPT_PATH.parents[2],
                capture_output=True,
                text=True,
                timeout=20,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)

            matrix_lines = [
                line
                for line in result.stdout.splitlines()
                if "scripts/run_benchmark_matrix.sh --build-dir" in line
            ]
            label_order = re.findall(r"--label (baseline|refactored)", "\n".join(matrix_lines))
            run_indices = re.findall(r"--run-index ([0-9]+)", "\n".join(matrix_lines))

            self.assertEqual(
                [
                    "baseline",
                    "refactored",
                    "refactored",
                    "baseline",
                    "baseline",
                    "refactored",
                ],
                label_order,
            )
            self.assertEqual(
                ["1", "1", "2", "2", "3", "3"],
                run_indices,
            )


if __name__ == "__main__":
    unittest.main()
