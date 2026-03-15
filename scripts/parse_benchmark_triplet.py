#!/usr/bin/env python3

import argparse
import json
import re
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


TRIPLET_LABELS = ("baseline", "refactored")
CORE_BENCHMARKS = {
    "B1-ComputeScheduler",
    "B8-MpscChannel",
    "B14-SchedulerInjectedWakeup",
}
CORE_REGRESSION_TOLERANCE_PCT = 5.0
DEFAULT_REGRESSION_TOLERANCE_PCT = 10.0
FLOAT_RE = r"[0-9]+(?:\.[0-9]+)?(?:e[+-]?[0-9]+)?"
UNSUPPORTED_MARKERS = ("unsupported", "skip", "not supported", "benchmark-timeout")


@dataclass(frozen=True)
class MetricSpec:
    kind: str
    unit: str
    higher_is_better: bool
    primary: bool = True
    tolerance_pct: float | None = None


METRIC_SPECS: dict[str, dict[str, MetricSpec]] = {
    "B1-ComputeScheduler": {
        "empty throughput": MetricSpec("throughput", "task/s", True),
        "light throughput": MetricSpec("throughput", "task/s", True),
        "heavy throughput": MetricSpec("throughput", "task/s", True, primary=False),
        "latency": MetricSpec("latency", "us", False),
    },
    "B3-TcpClient": {
        "average qps": MetricSpec("throughput", "qps", True),
        "average throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B5-UdpClient": {
        "packet loss": MetricSpec("loss", "%", False),
        "send throughput": MetricSpec("throughput", "MB/s", True),
        "recv throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B6-Udp": {
        "packet loss": MetricSpec("loss", "%", False),
        "send throughput": MetricSpec("throughput", "MB/s", True),
        "recv throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B7-FileIo": {
        "read iops": MetricSpec("throughput", "iops", True),
        "write iops": MetricSpec("throughput", "iops", True),
        "read throughput": MetricSpec("throughput", "MB/s", True),
        "write throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B8-MpscChannel": {
        "single producer throughput": MetricSpec("throughput", "msg/s", True),
        "multi producer throughput": MetricSpec("throughput", "msg/s", True),
        "batch throughput": MetricSpec("throughput", "msg/s", True),
        "latency": MetricSpec("latency", "us", False),
        "cross-scheduler throughput": MetricSpec("throughput", "msg/s", True),
        "sustained throughput": MetricSpec("throughput", "msg/s", True, tolerance_pct=10.0),
    },
    "B9-UnsafeChannel": {
        "unsafe throughput": MetricSpec("throughput", "msg/s", True),
        "batch throughput": MetricSpec("throughput", "msg/s", True),
        "recvBatched throughput": MetricSpec("throughput", "msg/s", True, tolerance_pct=15.0),
        "latency": MetricSpec("latency", "us", False),
        "mpsc reference throughput": MetricSpec("throughput", "msg/s", True, primary=False),
    },
    "B10-Ringbuffer": {
        "write throughput": MetricSpec("throughput", "MB/s", True),
        "read/write throughput": MetricSpec("throughput", "MB/s", True),
        "wrap around throughput": MetricSpec("throughput", "MB/s", True),
        "write iovecs latency": MetricSpec("latency", "ns/call", False),
        "read iovecs latency": MetricSpec("latency", "ns/call", False),
        "produce+consume latency": MetricSpec("latency", "ns/pair", False),
        "network receive throughput": MetricSpec("throughput", "MB/s", True),
        "network send throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B12-TcpIovClient": {
        "average qps": MetricSpec("throughput", "qps", True),
        "average throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B13-Sendfile": {
        "sendfile 1MB throughput": MetricSpec("throughput", "MB/s", True),
        "sendfile 10MB throughput": MetricSpec("throughput", "MB/s", True),
        "sendfile 50MB throughput": MetricSpec("throughput", "MB/s", True),
        "sendfile 100MB throughput": MetricSpec("throughput", "MB/s", True),
    },
    "B14-SchedulerInjectedWakeup": {
        "injected throughput": MetricSpec("throughput", "task/s", True),
        "injected latency": MetricSpec("latency", "us", False),
    },
}


def _find_float(pattern: str, text: str) -> float | None:
    match = re.search(pattern, text, flags=re.MULTILINE | re.IGNORECASE)
    if not match:
        return None
    return float(match.group(1))


def _find_float_in_line(line: str, *patterns: str) -> float | None:
    for pattern in patterns:
        value = _find_float(pattern, line)
        if value is not None:
            return value
    return None


def _find_two_floats(pattern: str, text: str) -> tuple[float, float] | None:
    match = re.search(pattern, text, flags=re.MULTILINE | re.IGNORECASE)
    if not match:
        return None
    return float(match.group(1)), float(match.group(2))


def parse_b1(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    for label, value in re.findall(
        rf"\[([^\]]+)\].*?throughput=({FLOAT_RE})\s+tasks/sec",
        text,
        flags=re.MULTILINE | re.IGNORECASE,
    ):
        if label == "Latency":
            continue
        metrics[f"{label.strip()} throughput"] = float(value)

    for label, value in re.findall(
        rf"\[\{{\}}\]\s+schedulers=\{{\}},\s+tasks=\{{\}},\s+time=\{{\}}ms,\s+throughput=\{{:[^}}]+\}}\s+tasks/sec\s+([A-Za-z]+)\s+{FLOAT_RE}\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
        text,
        flags=re.MULTILINE | re.IGNORECASE,
    ):
        metrics[f"{label.strip().lower()} throughput"] = float(value)

    latency = _find_float(rf"\[Latency\].*?avg_latency=({FLOAT_RE})us", text)
    if latency is None:
        latency = _find_float(
            rf"\[Latency\].*?avg_latency=\{{:[^}}]+\}}us\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
            text,
        )
    if latency is not None:
        metrics["latency"] = latency
    return metrics


def parse_b3_or_b12(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    qps = _find_float(r"Average QPS:\s*([0-9.]+)", text)
    throughput = _find_float(r"Average Throughput:\s*([0-9.]+)\s+MB/s", text)
    if qps is not None:
        metrics["average qps"] = qps
    if throughput is not None:
        metrics["average throughput"] = throughput
    return metrics


def parse_udp_summary(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    packet_loss = _find_float(rf"Packet Loss Rate:\s*({FLOAT_RE})%", text)
    if packet_loss is None:
        packet_loss = _find_float(rf"Packet Loss Rate:\s+\{{:[^}}]+\}}%\s+({FLOAT_RE})", text)

    send_tp = _find_float(rf"Sent:\s*{FLOAT_RE}\s+pkt/s\s+\(({FLOAT_RE})\s+MB/s\)", text)
    if send_tp is None:
        send_tp = _find_float(
            rf"Sent:\s+\{{:[^}}]+\}}\s+pkt/s\s+\(\{{:[^}}]+\}}\s+MB/s\)\s+{FLOAT_RE}\s+({FLOAT_RE})",
            text,
        )

    recv_tp = _find_float(rf"Received:\s*{FLOAT_RE}\s+pkt/s\s+\(({FLOAT_RE})\s+MB/s\)", text)
    if recv_tp is None:
        recv_tp = _find_float(
            rf"Received:\s+\{{:[^}}]+\}}\s+pkt/s\s+\(\{{:[^}}]+\}}\s+MB/s\)\s+{FLOAT_RE}\s+({FLOAT_RE})",
            text,
        )
    if packet_loss is not None:
        metrics["packet loss"] = packet_loss
    if send_tp is not None:
        metrics["send throughput"] = send_tp
    if recv_tp is not None:
        metrics["recv throughput"] = recv_tp
    return metrics


def parse_b7(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    iops = _find_two_floats(
        rf"Read IOPS:\s*({FLOAT_RE}),\s*Write IOPS:\s*({FLOAT_RE})",
        text,
    )
    if iops is None:
        iops = _find_two_floats(
            rf"Read IOPS:\s+\{{:[^}}]+\}},\s+Write IOPS:\s+\{{:[^}}]+\}}\s+({FLOAT_RE})\s+({FLOAT_RE})",
            text,
        )
    if iops is not None:
        metrics["read iops"], metrics["write iops"] = iops

    throughputs = _find_two_floats(
        rf"Read Throughput:\s*({FLOAT_RE})\s+MB/s,\s*Write Throughput:\s*({FLOAT_RE})\s+MB/s",
        text,
    )
    if throughputs is None:
        throughputs = _find_two_floats(
            rf"Read Throughput:\s+\{{:[^}}]+\}}\s+MB/s,\s+Write Throughput:\s+\{{:[^}}]+\}}\s+MB/s\s+({FLOAT_RE})\s+({FLOAT_RE})",
            text,
        )
    if throughputs is not None:
        metrics["read throughput"], metrics["write throughput"] = throughputs
    return metrics


def parse_b8(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    current_metric: str | None = None
    section_map = {
        "Single Producer Throughput Test": "single producer throughput",
        "Multi Producer Throughput Test": "multi producer throughput",
        "Batch Receive Throughput Test": "batch throughput",
        "Cross-Scheduler Test": "cross-scheduler throughput",
        "Sustained Load Test": "sustained throughput",
    }

    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("--- "):
            header = stripped
            current_metric = None
            for marker, metric_name in section_map.items():
                if marker in header:
                    current_metric = metric_name
                    break
            continue

        if "avg_latency=" in stripped:
            latency = _find_float_in_line(
                stripped,
                rf"avg_latency=({FLOAT_RE})us",
                rf"avg_latency=\{{:[^}}]+\}}us\s+{FLOAT_RE}\s+({FLOAT_RE})",
            )
            if latency is not None:
                metrics["latency"] = latency
            continue

        if "avg throughput:" in stripped:
            sustained = _find_float_in_line(
                stripped,
                rf"avg throughput:\s*({FLOAT_RE})",
                rf"avg throughput:\s+\{{:[^}}]+\}}/s\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
            )
            if sustained is not None:
                metrics["sustained throughput"] = sustained
            continue

        if "throughput=" in stripped and current_metric:
            throughput = _find_float_in_line(
                stripped,
                rf"throughput=({FLOAT_RE})\s+msg/s",
                rf"throughput=\{{:[^}}]+\}}\s+msg/s\s+{FLOAT_RE}\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
            )
            if throughput is not None:
                metrics[current_metric] = throughput

    return metrics


def parse_b9(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    current_metric: str | None = None
    section_map = {
        "UnsafeChannel Throughput Test": "unsafe throughput",
        "UnsafeChannel Batch Receive Throughput Test": "batch throughput",
        "UnsafeChannel recvBatched Throughput Test": "recvBatched throughput",
        "MpscChannel Throughput Test": "mpsc reference throughput",
    }

    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("--- "):
            header = stripped
            current_metric = None
            for marker, metric_name in section_map.items():
                if marker in header:
                    current_metric = metric_name
                    break
            if "UnsafeChannel Latency Test" in header:
                current_metric = None
            continue

        if "avg_latency=" in stripped:
            latency = _find_float_in_line(
                stripped,
                rf"avg_latency=({FLOAT_RE})us",
                rf"avg_latency=\{{:[^}}]+\}}us\s+{FLOAT_RE}\s+({FLOAT_RE})",
            )
            if latency is not None:
                metrics["latency"] = latency
            continue

        if "throughput=" in stripped and current_metric:
            throughput = _find_float_in_line(
                stripped,
                rf"throughput=({FLOAT_RE})\s+msg/s",
                rf"throughput=\{{:[^}}]+\}}\s+msg/s\s+{FLOAT_RE}\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
            )
            if throughput is not None:
                metrics[current_metric] = throughput

    return metrics


def parse_b10(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    line_patterns = {
        "write throughput": [
            rf"Write Throughput \(chunk=[^)]+\):\s*({FLOAT_RE})\s+MB/s",
            rf"\{{\}}:\s+\{{:[^}}]+\}}\s+MB/s.*Write Throughput \(chunk=[^)]+\)\s+({FLOAT_RE})\s+{FLOAT_RE}\s+{FLOAT_RE}",
        ],
        "read/write throughput": [
            rf"Read/Write Throughput \(chunk=[^)]+\):\s*({FLOAT_RE})\s+MB/s",
            rf"\{{\}}:\s+\{{:[^}}]+\}}\s+MB/s.*Read/Write Throughput \(chunk=[^)]+\)\s+({FLOAT_RE})\s+{FLOAT_RE}\s+{FLOAT_RE}",
        ],
        "wrap around throughput": [
            rf"Wrap Around Performance:\s*({FLOAT_RE})\s+MB/s",
            rf"Wrap Around Performance:\s+\{{:[^}}]+\}}\s+MB/s.*\s+({FLOAT_RE})\s+{FLOAT_RE}\s+{FLOAT_RE}",
        ],
        "write iovecs latency": [
            rf"getWriteIovecs:\s*({FLOAT_RE})\s+ns/call",
            rf"getWriteIovecs:\s+\{{:[^}}]+\}}\s+ns/call.*\s+({FLOAT_RE})\s+{FLOAT_RE}",
        ],
        "read iovecs latency": [
            rf"getReadIovecs:\s*({FLOAT_RE})\s+ns/call",
            rf"getReadIovecs:\s+\{{:[^}}]+\}}\s+ns/call.*\s+({FLOAT_RE})\s+{FLOAT_RE}",
        ],
        "produce+consume latency": [
            rf"produce\+consume:\s*({FLOAT_RE})\s+ns/pair",
            rf"produce\+consume:\s+\{{:[^}}]+\}}\s+ns/pair.*\s+({FLOAT_RE})\s+{FLOAT_RE}",
        ],
        "network receive throughput": [
            rf"Network Receive Simulation:\s*({FLOAT_RE})\s+MB/s",
            rf"\{{\}}:\s+\{{:[^}}]+\}}\s+MB/s.*Network Receive Simulation\s+({FLOAT_RE})\s+{FLOAT_RE}\s+{FLOAT_RE}",
        ],
        "network send throughput": [
            rf"Network Send Simulation:\s*({FLOAT_RE})\s+MB/s",
            rf"\{{\}}:\s+\{{:[^}}]+\}}\s+MB/s.*Network Send Simulation\s+({FLOAT_RE})\s+{FLOAT_RE}\s+{FLOAT_RE}",
        ],
    }
    for metric_name, patterns in line_patterns.items():
        matches: list[float] = []
        for pattern in patterns:
            matches.extend(float(value) for value in re.findall(pattern, text, flags=re.IGNORECASE))
        if not matches:
            continue
        metrics[metric_name] = max(matches) if "throughput" in metric_name else min(matches)
    return metrics


def parse_b13(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    current_size_mb: int | None = None
    current_method: str | None = None
    for line in text.splitlines():
        stripped = line.strip()
        header = re.search(rf"=== Benchmark:\s+(.+?)\s+\(({FLOAT_RE})\s+MB\)\s+===", stripped)
        if header:
            current_method = header.group(1).strip()
            current_size_mb = int(float(header.group(2)))
            continue

        placeholder_header = re.search(
            rf"=== Benchmark:\s+\{{\}}\s+\(\{{\}}\s+MB\)\s+===\s+(.+?)\s+({FLOAT_RE})",
            stripped,
            flags=re.IGNORECASE,
        )
        if placeholder_header:
            current_method = placeholder_header.group(1).strip()
            current_size_mb = int(float(placeholder_header.group(2)))
            continue

        if current_method == "sendfile" and current_size_mb is not None and "Throughput:" in stripped:
            throughput = _find_float_in_line(
                stripped,
                rf"Throughput:\s*({FLOAT_RE})\s+MB/s",
                rf"Throughput:\s+\{{:[^}}]+\}}\s+MB/s\s+({FLOAT_RE})",
            )
            if throughput is not None:
                metrics[f"sendfile {current_size_mb}MB throughput"] = throughput

    return metrics


def parse_b14(text: str) -> dict[str, float]:
    metrics: dict[str, float] = {}
    throughput = _find_float(rf"\[InjectedThroughput\].*?throughput=({FLOAT_RE})\s+tasks/s", text)
    if throughput is None:
        throughput = _find_float(
            rf"\[InjectedThroughput\].*?throughput=\{{:[^}}]+\}}\s+tasks/s\s+{FLOAT_RE}\s+{FLOAT_RE}\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
            text,
        )
    latency = _find_float(rf"\[InjectedLatency\].*?avg_latency=({FLOAT_RE})us", text)
    if latency is None:
        latency = _find_float(
            rf"\[InjectedLatency\].*?avg_latency=\{{:[^}}]+\}}us\s+{FLOAT_RE}\s+{FLOAT_RE}\s+({FLOAT_RE})",
            text,
        )
    if throughput is not None:
        metrics["injected throughput"] = throughput
    if latency is not None:
        metrics["injected latency"] = latency
    return metrics


PARSERS: dict[str, Callable[[str], dict[str, float]]] = {
    "B1-ComputeScheduler": parse_b1,
    "B3-TcpClient": parse_b3_or_b12,
    "B5-UdpClient": parse_udp_summary,
    "B6-Udp": parse_udp_summary,
    "B7-FileIo": parse_b7,
    "B8-MpscChannel": parse_b8,
    "B9-UnsafeChannel": parse_b9,
    "B10-Ringbuffer": parse_b10,
    "B12-TcpIovClient": parse_b3_or_b12,
    "B13-Sendfile": parse_b13,
    "B14-SchedulerInjectedWakeup": parse_b14,
}


def detect_unsupported(text: str) -> bool:
    lowered = text.lower()
    return any(marker in lowered for marker in UNSUPPORTED_MARKERS)


def iter_label_runs(input_root: Path, label: str) -> list[Path]:
    label_root = input_root / label
    if not label_root.exists():
        return []

    run_dirs = sorted(
        path for path in label_root.iterdir() if path.is_dir() and path.name.startswith("run-")
    )
    if run_dirs:
        return run_dirs
    return [label_root]


def format_value(value: float | None, unit: str) -> str:
    if value is None:
        return "unsupported"

    if unit in {"task/s", "msg/s", "qps", "iops"}:
        if abs(value) >= 1_000_000:
            return f"{value / 1_000_000:.2f}M {unit}"
        if abs(value) >= 1_000:
            return f"{value / 1_000:.2f}K {unit}"
        return f"{value:.2f} {unit}"
    if unit == "%":
        return f"{value:.2f}%"
    return f"{value:.2f} {unit}"


def format_delta(delta: float | None) -> str:
    if delta is None:
        return "n/a"
    return f"{delta:+.1f}%"


def metric_status(
    benchmark: str,
    spec: MetricSpec,
    baseline_value: float | None,
    refactored_value: float | None,
) -> str:
    if baseline_value is None or refactored_value is None:
        return "unsupported"

    tolerance_pct = (
        spec.tolerance_pct
        if spec.tolerance_pct is not None
        else (
            CORE_REGRESSION_TOLERANCE_PCT
            if benchmark in CORE_BENCHMARKS
            else DEFAULT_REGRESSION_TOLERANCE_PCT
        )
    )
    tolerance_ratio = tolerance_pct / 100.0

    if spec.higher_is_better:
        return "fail" if refactored_value < baseline_value * (1.0 - tolerance_ratio) else "pass"
    return "fail" if refactored_value > baseline_value * (1.0 + tolerance_ratio) else "pass"


def collect_triplet_report(input_root: Path | str) -> dict:
    input_root = Path(input_root)

    sample_values: dict[str, dict[str, dict[str, list[float]]]] = {}
    label_support: dict[str, dict[str, bool]] = {}

    for label in TRIPLET_LABELS:
        for run_dir in iter_label_runs(input_root, label):
            for log_path in sorted(run_dir.glob("*.log")):
                benchmark = log_path.stem
                parser = PARSERS.get(benchmark)
                if parser is None:
                    continue

                text = log_path.read_text(encoding="utf-8")
                unsupported = detect_unsupported(text)
                label_support.setdefault(benchmark, {}).setdefault(label, False)
                if unsupported:
                    label_support[benchmark][label] = True
                    continue

                metrics = parser(text)
                if not metrics:
                    continue

                label_support[benchmark][label] = True
                metric_bucket = sample_values.setdefault(benchmark, {})
                for metric_name, value in metrics.items():
                    metric_bucket.setdefault(metric_name, {}).setdefault(label, []).append(value)

    rows: list[dict] = []

    for benchmark in sorted(sample_values.keys()):
        metric_names = set(sample_values[benchmark].keys())
        metric_names.update(METRIC_SPECS.get(benchmark, {}).keys())
        for metric_name in sorted(metric_names):
            spec = METRIC_SPECS.get(benchmark, {}).get(metric_name)
            if spec is None:
                continue

            per_label: dict[str, dict[str, float | str | None]] = {}
            for label in TRIPLET_LABELS:
                values = sample_values.get(benchmark, {}).get(metric_name, {}).get(label, [])
                median_value = statistics.median(values) if values else None
                per_label[label] = {
                    "value": median_value,
                    "formatted": format_value(median_value, spec.unit),
                }

            baseline_value = per_label["baseline"]["value"]
            refactored_value = per_label["refactored"]["value"]
            delta = None
            if isinstance(baseline_value, float) and isinstance(refactored_value, float) and baseline_value != 0:
                delta = ((refactored_value - baseline_value) / baseline_value) * 100.0

            status = metric_status(benchmark, spec, baseline_value, refactored_value)
            rows.append(
                {
                    "benchmark": benchmark,
                    "metric": metric_name,
                    "kind": spec.kind,
                    "unit": spec.unit,
                    "is_core": benchmark in CORE_BENCHMARKS,
                    "is_primary": spec.primary,
                    "baseline": per_label["baseline"],
                    "refactored": per_label["refactored"],
                    "delta_vs_baseline_pct": delta,
                    "status": status,
                }
            )

    gating_rows = [
        row for row in rows if row["is_core"] or row["is_primary"]
    ]
    required_core_present = CORE_BENCHMARKS.issubset({row["benchmark"] for row in rows})
    overall_pass = bool(gating_rows) and required_core_present and all(
        row["status"] == "pass" for row in gating_rows
    )

    return {
        "input_root": str(input_root),
        "labels": list(TRIPLET_LABELS),
        "overall_pass": overall_pass,
        "rows": rows,
    }


def render_markdown(report: dict) -> str:
    lines = [
        "# Benchmark Triplet Summary",
        "",
        f"- Overall status: {'PASS' if report['overall_pass'] else 'FAIL'}",
        "",
        "| Benchmark | Metric | baseline | refactored | Delta vs baseline | Status |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]

    def sort_key(row: dict) -> tuple[str, str]:
        return (row["benchmark"], row["metric"])

    for row in sorted(report["rows"], key=sort_key):
        lines.append(
            "| `{benchmark}` | {metric} | {baseline} | {refactored} | {delta} | {status} |".format(
                benchmark=row["benchmark"],
                metric=row["metric"],
                baseline=row["baseline"]["formatted"],
                refactored=row["refactored"]["formatted"],
                delta=format_delta(row["delta_vs_baseline_pct"]),
                status=row["status"].upper(),
            )
        )

    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse benchmark triplet logs into JSON and Markdown.")
    parser.add_argument("--input-root", required=True, help="Root directory containing baseline/refactored logs.")
    parser.add_argument("--json-out", help="Path to write JSON report.")
    parser.add_argument("--markdown-out", help="Path to write Markdown summary.")
    args = parser.parse_args()

    report = collect_triplet_report(Path(args.input_root))

    if args.json_out:
        json_path = Path(args.json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    markdown = render_markdown(report)
    if args.markdown_out:
        markdown_path = Path(args.markdown_out)
        markdown_path.parent.mkdir(parents=True, exist_ok=True)
        markdown_path.write_text(markdown, encoding="utf-8")
    elif not args.json_out:
        print(markdown, end="")

    return 0 if report["overall_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
