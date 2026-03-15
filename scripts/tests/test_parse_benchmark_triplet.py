import tempfile
import textwrap
import unittest
from pathlib import Path


from scripts.parse_benchmark_triplet import MetricSpec, collect_triplet_report, metric_status, render_markdown


def write_log(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content).strip() + "\n", encoding="utf-8")


class ParseBenchmarkTripletTest(unittest.TestCase):
    def test_collect_triplet_report_treats_timeout_logs_as_unsupported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            write_log(
                root / "baseline" / "run-1" / "B8-MpscChannel.log",
                """
                [benchmark-timeout] B8-MpscChannel exceeded 123s
                """,
            )
            write_log(
                root / "refactored" / "run-1" / "B8-MpscChannel.log",
                """
                --- Single Producer Throughput Test (1000000 messages) ---
                  sent=1000000, received=1000000, time=100ms, throughput=12000000 msg/s
                --- Latency Test (10000 messages) ---
                  messages=10000, avg_latency=800.00us
                --- Cross-Scheduler Test (1000000 messages) ---
                  sent=1000000, received=1000000, time=100ms, throughput=12000000 msg/s
                --- Sustained Load Test (5s) ---
                  total: sent=5000000, received=5000000, avg throughput: 11500000/s
                """,
            )

            report = collect_triplet_report(root)
            rows = {
                (row["benchmark"], row["metric"]): row
                for row in report["rows"]
            }

            single_tp = rows[("B8-MpscChannel", "single producer throughput")]
            self.assertIsNone(single_tp["baseline"]["value"])
            self.assertEqual(single_tp["baseline"]["formatted"], "unsupported")
            self.assertEqual(single_tp["status"], "unsupported")

    def test_metric_status_allows_five_percent_core_regression(self) -> None:
        throughput_spec = MetricSpec("throughput", "task/s", True)
        latency_spec = MetricSpec("latency", "us", False)
        sustained_spec = MetricSpec("throughput", "msg/s", True, tolerance_pct=10.0)
        recv_batched_spec = MetricSpec("throughput", "msg/s", True, tolerance_pct=15.0)

        self.assertEqual(
            metric_status("B1-ComputeScheduler", throughput_spec, 100.0, 95.0),
            "pass",
        )
        self.assertEqual(
            metric_status("B1-ComputeScheduler", throughput_spec, 100.0, 94.9),
            "fail",
        )
        self.assertEqual(
            metric_status("B8-MpscChannel", latency_spec, 100.0, 105.0),
            "pass",
        )
        self.assertEqual(
            metric_status("B8-MpscChannel", latency_spec, 100.0, 105.1),
            "fail",
        )
        self.assertEqual(
            metric_status("B9-UnsafeChannel", throughput_spec, 100.0, 90.0),
            "pass",
        )
        self.assertEqual(
            metric_status("B9-UnsafeChannel", throughput_spec, 100.0, 89.9),
            "fail",
        )
        self.assertEqual(
            metric_status("B8-MpscChannel", sustained_spec, 100.0, 90.0),
            "pass",
        )
        self.assertEqual(
            metric_status("B8-MpscChannel", sustained_spec, 100.0, 89.9),
            "fail",
        )
        self.assertEqual(
            metric_status("B9-UnsafeChannel", recv_batched_spec, 100.0, 85.0),
            "pass",
        )
        self.assertEqual(
            metric_status("B9-UnsafeChannel", recv_batched_spec, 100.0, 84.9),
            "fail",
        )

    def test_collect_triplet_report_uses_medians_and_delta_rules(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            b1_runs = {
                "baseline": [(1_200_000, 900_000, 60.0), (1_100_000, 920_000, 58.0), (1_300_000, 880_000, 62.0)],
                "refactored": [(1_500_000, 950_000, 40.0), (1_400_000, 960_000, 42.0), (1_600_000, 940_000, 38.0)],
            }
            for label, runs in b1_runs.items():
                for index, (empty_tp, light_tp, latency_us) in enumerate(runs, start=1):
                    write_log(
                        root / label / f"run-{index}" / "B1-ComputeScheduler.log",
                        f"""
                        [empty] schedulers=4, tasks=100000, time=100ms, throughput={empty_tp} tasks/sec
                        [light] schedulers=4, tasks=100000, time=100ms, throughput={light_tp} tasks/sec
                        [Latency] schedulers=4, tasks=10000, avg_latency={latency_us:.2f}us
                        """,
                    )

            b8_runs = {
                "baseline": [
                    (11_000_000, 18_000_000, 16_500_000, 1400.0, 13_000_000, 11_000_000),
                    (10_800_000, 18_200_000, 16_200_000, 1300.0, 13_200_000, 10_800_000),
                    (11_200_000, 17_800_000, 16_800_000, 1500.0, 12_800_000, 11_200_000),
                ],
                "refactored": [
                    (12_000_000, 18_500_000, 14_000_000, 800.0, 12_000_000, 11_500_000),
                    (12_200_000, 18_600_000, 14_500_000, 850.0, 12_100_000, 11_600_000),
                    (11_800_000, 18_400_000, 13_500_000, 900.0, 11_900_000, 11_400_000),
                ],
            }
            for label, runs in b8_runs.items():
                for index, values in enumerate(runs, start=1):
                    single_tp, multi_tp, batch_tp, latency_us, cross_tp, sustained_tp = values
                    write_log(
                        root / label / f"run-{index}" / "B8-MpscChannel.log",
                        f"""
                        --- Single Producer Throughput Test (1000000 messages) ---
                          sent=1000000, received=1000000, time=100ms, throughput={single_tp} msg/s
                        --- Multi Producer Throughput Test (4 producers, 1000000 messages) ---
                          sent=1000000, received=1000000, time=100ms, throughput={multi_tp} msg/s
                        --- Batch Receive Throughput Test (1000000 messages) ---
                          sent=1000000, received=1000000, time=100ms, throughput={batch_tp} msg/s
                        --- Latency Test (10000 messages) ---
                          messages=10000, avg_latency={latency_us:.2f}us
                        --- Cross-Scheduler Test (1000000 messages) ---
                          sent=1000000, received=1000000, time=100ms, throughput={cross_tp} msg/s
                        --- Sustained Load Test (5s) ---
                          total: sent=5000000, received=5000000, avg throughput: {sustained_tp}/s
                        """,
                    )

            b14_runs = {
                "baseline": [(4_500_000, 5.0), (4_700_000, 4.0), (4_600_000, 6.0)],
                "refactored": [(5_000_000, 3.0), (5_200_000, 3.5), (4_800_000, 2.5)],
            }
            for label, runs in b14_runs.items():
                for index, (throughput, latency_us) in enumerate(runs, start=1):
                    write_log(
                        root / label / f"run-{index}" / "B14-SchedulerInjectedWakeup.log",
                        f"""
                        [InjectedThroughput] producers=4, tasks_per_producer=500000, total=2000000, time=400ms, throughput={throughput} tasks/s
                        [InjectedLatency] samples=200000, time=100ms, avg_latency={latency_us:.2f}us
                        """,
                    )

            report = collect_triplet_report(root)
            rows = {
                (row["benchmark"], row["metric"]): row
                for row in report["rows"]
            }

            b1_empty = rows[("B1-ComputeScheduler", "empty throughput")]
            self.assertEqual(b1_empty["baseline"]["value"], 1_200_000.0)
            self.assertEqual(b1_empty["refactored"]["value"], 1_500_000.0)
            self.assertAlmostEqual(b1_empty["delta_vs_baseline_pct"], 25.0)
            self.assertEqual(b1_empty["status"], "pass")
            self.assertEqual(
                {
                    "benchmark",
                    "metric",
                    "kind",
                    "unit",
                    "is_core",
                    "is_primary",
                    "baseline",
                    "refactored",
                    "delta_vs_baseline_pct",
                    "status",
                },
                set(b1_empty),
            )

            b1_latency = rows[("B1-ComputeScheduler", "latency")]
            self.assertEqual(b1_latency["baseline"]["value"], 60.0)
            self.assertEqual(b1_latency["refactored"]["value"], 40.0)
            self.assertAlmostEqual(b1_latency["delta_vs_baseline_pct"], -33.3333333333, places=4)
            self.assertEqual(b1_latency["status"], "pass")

            b8_batch = rows[("B8-MpscChannel", "batch throughput")]
            self.assertEqual(b8_batch["baseline"]["value"], 16_500_000.0)
            self.assertEqual(b8_batch["refactored"]["value"], 14_000_000.0)
            self.assertAlmostEqual(b8_batch["delta_vs_baseline_pct"], -15.1515151515, places=4)
            self.assertEqual(b8_batch["status"], "fail")

            b8_latency = rows[("B8-MpscChannel", "latency")]
            self.assertEqual(b8_latency["status"], "pass")

            b14_throughput = rows[("B14-SchedulerInjectedWakeup", "injected throughput")]
            self.assertEqual(b14_throughput["status"], "pass")
            self.assertAlmostEqual(b14_throughput["delta_vs_baseline_pct"], 8.6956521739, places=4)

            self.assertFalse(report["overall_pass"])

    def test_collect_triplet_report_parses_placeholder_style_logs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for label, b1_throughput, b1_latency, b8_throughput, b8_latency, b14_throughput, b14_latency in [
                ("baseline", 900000, 20.0, 1.5e7, 100.0, 5.0e6, 50.0),
                ("refactored", 1.06383e6, 60.196, 1.26582e7, 16.9357, 3.77358e6, 13.9376),
            ]:
                write_log(
                    root / label / "run-1" / "B1-ComputeScheduler.log",
                    f"""
                    [{{}}] schedulers={{}}, tasks={{}}, time={{}}ms, throughput={{:.0f}} tasks/sec Empty 10 100000 118 {b1_throughput}
                    [Latency] schedulers={{}}, tasks={{}}, avg_latency={{:.2f}}us 10 10000 {b1_latency}
                    """,
                )
                write_log(
                    root / label / "run-1" / "B8-MpscChannel.log",
                    f"""
                    --- Single Producer Throughput Test ({{}} messages) --- 1000000
                      sent={{}}, received={{}}, time={{}}ms, throughput={{:.0f}} msg/s 1000000 1000000 73 {b8_throughput}
                    --- Latency Test ({{}} messages) --- 100000
                      messages={{}}, avg_latency={{:.2f}}us 100000 {b8_latency}
                    --- Sustained Load Test ({{}}s) --- 5
                      total: sent={{}}, received={{}}, avg throughput: {{:.0f}}/s 65240547 65240547 {b8_throughput}
                    """,
                )
                write_log(
                    root / label / "run-1" / "B14-SchedulerInjectedWakeup.log",
                    f"""
                    Scheduler injected wakeup benchmark, backend={{}} kqueue
                    [InjectedThroughput] producers={{}}, tasks_per_producer={{}}, total={{}}, time={{}}ms, throughput={{:.0f}} tasks/s 4 50000 200000 42 {b14_throughput}
                    [InjectedLatency] samples={{}}, time={{}}ms, avg_latency={{:.2f}}us 10000 25 {b14_latency}
                    """,
                )

            report = collect_triplet_report(root)
            rows = {
                (row["benchmark"], row["metric"]): row
                for row in report["rows"]
            }

            self.assertIn(("B1-ComputeScheduler", "empty throughput"), rows)
            self.assertEqual(rows[("B1-ComputeScheduler", "empty throughput")]["baseline"]["value"], 900000.0)
            self.assertEqual(rows[("B8-MpscChannel", "single producer throughput")]["refactored"]["value"], 1.26582e7)
            self.assertEqual(rows[("B14-SchedulerInjectedWakeup", "injected latency")]["baseline"]["value"], 50.0)

    def test_render_markdown_includes_triplet_table_and_status(self) -> None:
        report = {
            "overall_pass": True,
            "rows": [
                {
                    "benchmark": "B1-ComputeScheduler",
                    "metric": "empty throughput",
                    "kind": "throughput",
                    "unit": "task/s",
                    "baseline": {"value": 1_200_000.0, "formatted": "1.20M task/s"},
                    "refactored": {"value": 1_500_000.0, "formatted": "1.50M task/s"},
                    "delta_vs_baseline_pct": 25.0,
                    "status": "pass",
                }
            ],
        }

        markdown = render_markdown(report)
        self.assertIn("# Benchmark Triplet Summary", markdown)
        self.assertIn("| Benchmark | Metric | baseline | refactored | Delta vs baseline | Status |", markdown)
        self.assertIn("| `B1-ComputeScheduler` | empty throughput | 1.20M task/s | 1.50M task/s | +25.0% | PASS |", markdown)


if __name__ == "__main__":
    unittest.main()
