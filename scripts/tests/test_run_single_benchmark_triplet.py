import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "run_single_benchmark_triplet.sh"


class RunSingleBenchmarkTripletTest(unittest.TestCase):
    def test_dry_run_forwards_timeout_seconds_to_each_backend(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "single-triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--benchmark",
                    "B8-MpscChannel",
                    "--repeat",
                    "2",
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
            triplet_lines = [
                line
                for line in result.stdout.splitlines()
                if "scripts/run_benchmark_triplet.sh --baseline-ref" in line
            ]
            self.assertEqual(3, len(triplet_lines))
            for line in triplet_lines:
                self.assertIn("--timeout-seconds 123", line)

    def test_dry_run_runs_backends_in_fixed_order(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "single-triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
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
            triplet_lines = [
                line
                for line in result.stdout.splitlines()
                if "scripts/run_benchmark_triplet.sh --baseline-ref" in line
            ]
            self.assertEqual(3, len(triplet_lines))
            self.assertIn("--backend kqueue", triplet_lines[0])
            self.assertIn("--backend epoll", triplet_lines[1])
            self.assertIn("--backend io_uring", triplet_lines[2])

    def test_dry_run_forwards_benchmark_and_backend_specific_output_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output_root = Path(tmp) / "single-triplet-output"
            result = subprocess.run(
                [
                    str(SCRIPT_PATH),
                    "--baseline-ref",
                    "cde3da1",
                    "--refactored-path",
                    ".worktrees/v3",
                    "--benchmark",
                    "B8-MpscChannel",
                    "--repeat",
                    "2",
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
            triplet_lines = [
                line
                for line in result.stdout.splitlines()
                if "scripts/run_benchmark_triplet.sh --baseline-ref" in line
            ]
            self.assertEqual(3, len(triplet_lines))
            for backend, line in zip(("kqueue", "epoll", "io_uring"), triplet_lines):
                self.assertIn("--benchmark B8-MpscChannel", line)
                self.assertIn("--repeat 2", line)
                self.assertIn(f"--output-root {output_root / backend}", line)


if __name__ == "__main__":
    unittest.main()
