#!/usr/bin/env python3
"""
Benchmark Results Analyzer for oneTBB Resource Limiting Performance Tests

Parses results from resource_limited_ubenches and generates:
- Summary tables (execution_time and time_per_input)
- SVG bar charts with relative performance vs mode0 baseline
- Statistical analysis (best/worst performers, winner counts)

Usage:
    python analyze_benchmarks.py results.txt
    python analyze_benchmarks.py results.txt --output-dir ./reports
    python analyze_benchmarks.py results.txt --table-only
    python analyze_benchmarks.py results.txt --chart-only
    python analyze_benchmarks.py results.txt --no-stdout

Default behavior:
    - Generates both tables and charts for execution_time and time_per_input
    - Saves files to current directory:
        * benchmark_table_execution_time.md
        * benchmark_chart_execution_time.svg
        * benchmark_table_time_per_input.md
        * benchmark_chart_time_per_input.svg
    - Prints tables to stdout

Generated files:
    - Markdown tables with relative performance (normalized to mode0 baseline)
    - SVG bar charts with grouped bars (one group per benchmark)
    - Summary statistics including best/worst per benchmark and winner counts
"""

import re
import sys
import argparse
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass, field
from collections import defaultdict


@dataclass
class Metrics:
    """Performance metrics for a single benchmark run"""
    construction_time: float = 0.0
    execution_time: float = 0.0
    total_time: float = 0.0
    time_per_execution: float = 0.0
    time_per_input: float = 0.0


@dataclass
class VariantConfig:
    """Configuration for a benchmark variant"""
    mode: str  # 'mode0', 'mode1', 'mode2'
    mode_name: str  # 'join_node', 'resource_limiter', 'pressure_aware_resource_limiter'
    timestamp: Optional[bool] = None  # mode2 only
    local_counter: Optional[bool] = None  # mode2 only
    notify: Optional[bool] = None  # mode2 only
    pressure: Optional[bool] = None  # mode2 only

    def get_short_name(self) -> str:
        """Get short variant name for display"""
        if self.mode == 'mode0':
            return 'mode0'
        elif self.mode == 'mode1':
            return 'mode1'
        else:  # mode2
            pressure_prefix = 'nopressure_' if not self.pressure else ''
            counter = 'local' if self.local_counter else 'global'
            ts = '_ts' if self.timestamp else ''
            # Only show notify suffix when pressure is enabled
            notify = ('_notify' if self.notify else '_nonotify') if self.pressure else ''
            return f'mode2_{pressure_prefix}{counter}{ts}{notify}'

    def get_display_name(self) -> str:
        """Get full display name for legends"""
        if self.mode == 'mode0':
            return 'Mode 0: join_node'
        elif self.mode == 'mode1':
            return 'Mode 1: resource_limiter'
        else:  # mode2
            pressure_prefix = 'no_pressure ' if not self.pressure else ''
            counter = 'local' if self.local_counter else 'global'
            ts = '+timestamp' if self.timestamp else ''
            # Only show notify when pressure is enabled
            notify = ('+notify' if self.notify else '') if self.pressure else ''
            return f'Mode 2: {pressure_prefix}{counter}{ts}{notify}'


@dataclass
class BenchmarkResult:
    """Complete results for one variant"""
    variant: VariantConfig
    configuration: Dict[str, int] = field(default_factory=dict)
    benchmarks: Dict[str, Metrics] = field(default_factory=dict)


class ResultsParser:
    """Parse benchmark results from text output"""

    # Regex patterns
    MODE_PATTERN = re.compile(r'USE_MODE=(\d+)\s+\(([^)]+)\)')
    CONFIG_PATTERN = re.compile(r'num_executions:\s+(\d+)')
    INPUTS_PATTERN = re.compile(r'num_inputs:\s+(\d+)')
    NODES_PATTERN = re.compile(r'num_nodes/tree_depth:\s+(\d+)')
    PRESSURE_PATTERN = re.compile(r'__TBB_USE_PRESSURE=(\d+)')
    TIMESTAMP_PATTERN = re.compile(r'__TBB_USE_TIMESTAMP_IN_REQUEST_ID=(\d+)')
    LOCAL_COUNTER_PATTERN = re.compile(r'__TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID=(\d+)')
    NOTIFY_PATTERN = re.compile(r'__TBB_USE_NOTIFY_ON_REPORT_PRESSURE=(\d+)')
    BENCH_NAME_PATTERN = re.compile(r'(\w+_bench)\s+results:')
    CONSTRUCTION_TIME_PATTERN = re.compile(r'Construction time:\s+([\d.]+(?:e[+-]?\d+)?)\s+seconds')
    EXECUTION_TIME_PATTERN = re.compile(r'Execution time:\s+([\d.]+(?:e[+-]?\d+)?)\s+seconds')
    TIME_PER_EXEC_PATTERN = re.compile(r'Time per execution:\s+([\d.]+(?:e[+-]?\d+)?)\s+seconds')
    TIME_PER_INPUT_PATTERN = re.compile(r'Time per input:\s+([\d.]+(?:e[+-]?\d+)?)\s+seconds')

    def parse_file(self, filepath: str) -> List[BenchmarkResult]:
        """Parse results file and extract all benchmark results"""
        with open(filepath, 'r') as f:
            content = f.read()

        results = []
        current_result = None
        current_bench_name = None

        for line in content.splitlines():
            # Check for new variant
            if match := self.MODE_PATTERN.search(line):
                if current_result:
                    results.append(current_result)

                mode_num = match.group(1)
                mode_name = match.group(2).strip()
                current_result = BenchmarkResult(
                    variant=VariantConfig(
                        mode=f'mode{mode_num}',
                        mode_name=mode_name
                    )
                )
                current_bench_name = None

            if not current_result:
                continue

            # Parse mode2 configuration
            if current_result.variant.mode == 'mode2':
                if match := self.PRESSURE_PATTERN.search(line):
                    current_result.variant.pressure = (match.group(1) == '1')
                if match := self.TIMESTAMP_PATTERN.search(line):
                    current_result.variant.timestamp = (match.group(1) == '1')
                if match := self.LOCAL_COUNTER_PATTERN.search(line):
                    current_result.variant.local_counter = (match.group(1) == '1')
                if match := self.NOTIFY_PATTERN.search(line):
                    current_result.variant.notify = (match.group(1) == '1')

            # Parse configuration
            if match := self.CONFIG_PATTERN.search(line):
                current_result.configuration['num_executions'] = int(match.group(1))
            if match := self.INPUTS_PATTERN.search(line):
                current_result.configuration['num_inputs'] = int(match.group(1))
            if match := self.NODES_PATTERN.search(line):
                current_result.configuration['num_nodes'] = int(match.group(1))

            # Parse benchmark results
            if match := self.BENCH_NAME_PATTERN.search(line):
                current_bench_name = match.group(1)
                current_result.benchmarks[current_bench_name] = Metrics()

            if current_bench_name:
                metrics = current_result.benchmarks[current_bench_name]
                if match := self.CONSTRUCTION_TIME_PATTERN.search(line):
                    metrics.construction_time = float(match.group(1))
                if match := self.EXECUTION_TIME_PATTERN.search(line):
                    metrics.execution_time = float(match.group(1))
                if match := self.TIME_PER_EXEC_PATTERN.search(line):
                    metrics.time_per_execution = float(match.group(1))
                if match := self.TIME_PER_INPUT_PATTERN.search(line):
                    metrics.time_per_input = float(match.group(1))

        # Add last result
        if current_result:
            results.append(current_result)

        return results


class TableFormatter:
    """Format benchmark results as tables"""

    BENCHMARK_ORDER = [
        'genie_bench',
        'genie_diamond_bench',
        'baseline_cycle_bench',
        'chain_bench',
        'siblings_bench',
        'tree_bench'
    ]

    BENCHMARK_DISPLAY_NAMES = {
        'genie_bench': 'Genie',
        'genie_diamond_bench': 'Genie Diamond',
        'baseline_cycle_bench': 'Baseline Cycle',
        'chain_bench': 'Chain',
        'siblings_bench': 'Siblings',
        'tree_bench': 'Tree'
    }

    @staticmethod
    def sort_variants(results: List[BenchmarkResult]) -> List[BenchmarkResult]:
        """Sort variants: mode0, mode1, then mode2 (pressure=1 first, then pressure=0)"""
        def sort_key(result: BenchmarkResult) -> Tuple:
            v = result.variant
            if v.mode == 'mode0':
                return (0, 0, 0, 0, 0)
            elif v.mode == 'mode1':
                return (1, 0, 0, 0, 0)
            else:  # mode2
                pressure = 0 if v.pressure else 1  # pressure=1 first (0), pressure=0 second (1)
                counter = 0 if v.local_counter is False else 1  # global=0, local=1
                timestamp = 1 if v.timestamp else 0
                notify = 0 if v.notify else 1  # notify first (0), then nonotify (1)
                return (2, pressure, counter, notify, timestamp)

        return sorted(results, key=sort_key)

    def create_table(self, results: List[BenchmarkResult], metric: str,
                     relative_to_baseline: bool = True) -> str:
        """Create a markdown table for the specified metric"""
        sorted_results = self.sort_variants(results)

        # Get baseline (mode0) values
        baseline = None
        if relative_to_baseline:
            baseline = next((r for r in sorted_results if r.variant.mode == 'mode0'), None)

        # Build table header
        header = "| Variant |"
        separator = "|---------|"
        for bench_name in self.BENCHMARK_ORDER:
            display_name = self.BENCHMARK_DISPLAY_NAMES[bench_name]
            header += f" {display_name} |"
            separator += "---------|"
        if relative_to_baseline:
            header += " Average |"
            separator += "---------|"

        lines = [header, separator]

        # Build table rows
        for result in sorted_results:
            row = f"| {result.variant.get_short_name()} |"
            row_values = []

            for bench_name in self.BENCHMARK_ORDER:
                if bench_name in result.benchmarks:
                    value = getattr(result.benchmarks[bench_name], metric)

                    if relative_to_baseline and baseline and bench_name in baseline.benchmarks:
                        baseline_value = getattr(baseline.benchmarks[bench_name], metric)
                        if baseline_value > 0:
                            relative = value / baseline_value
                            row += f" {relative:.3f}x |"
                            row_values.append(relative)
                        else:
                            row += " N/A |"
                    else:
                        row += f" {value:.4f}s |"
                        row_values.append(value)
                else:
                    row += " N/A |"

            if relative_to_baseline and row_values:
                avg = sum(row_values) / len(row_values)
                row += f" {avg:.3f}x |"

            lines.append(row)

        return '\n'.join(lines)

    def create_summary_stats(self, results: List[BenchmarkResult], metric: str) -> str:
        """Create summary statistics section"""
        sorted_results = self.sort_variants(results)
        lines = ["\n## Summary Statistics\n"]

        # Best/worst per benchmark
        lines.append("### Best and Worst Performers per Benchmark\n")
        for bench_name in self.BENCHMARK_ORDER:
            display_name = self.BENCHMARK_DISPLAY_NAMES[bench_name]

            # Get all values for this benchmark
            bench_results = [
                (r.variant.get_short_name(), getattr(r.benchmarks[bench_name], metric))
                for r in sorted_results if bench_name in r.benchmarks
            ]

            if bench_results:
                best = min(bench_results, key=lambda x: x[1])
                worst = max(bench_results, key=lambda x: x[1])
                lines.append(f"**{display_name}:**")
                lines.append(f"- Best: {best[0]} ({best[1]:.4f}s)")
                lines.append(f"- Worst: {worst[0]} ({worst[1]:.4f}s)")
                lines.append("")

        # Winner count
        lines.append("### Winner Count (Best Performance)\n")
        winner_counts = defaultdict(int)

        for bench_name in self.BENCHMARK_ORDER:
            bench_results = [
                (r.variant.get_short_name(), getattr(r.benchmarks[bench_name], metric))
                for r in sorted_results if bench_name in r.benchmarks
            ]
            if bench_results:
                winner = min(bench_results, key=lambda x: x[1])[0]
                winner_counts[winner] += 1

        for variant_name, count in sorted(winner_counts.items(), key=lambda x: -x[1]):
            lines.append(f"- {variant_name}: {count} wins")

        # Average performance per variant
        lines.append("\n### Average Performance per Variant\n")
        for result in sorted_results:
            values = [
                getattr(result.benchmarks[bench_name], metric)
                for bench_name in self.BENCHMARK_ORDER
                if bench_name in result.benchmarks
            ]
            if values:
                avg = sum(values) / len(values)
                lines.append(f"- {result.variant.get_short_name()}: {avg:.4f}s average")

        return '\n'.join(lines)


class SVGChartGenerator:
    """Generate SVG bar charts for benchmark results"""

    # Chart dimensions and styling
    WIDTH = 1400
    HEIGHT = 600
    MARGIN_LEFT = 80
    MARGIN_RIGHT = 250  # Space for legend
    MARGIN_TOP = 60
    MARGIN_BOTTOM = 100

    # Colors for variants (matching the sort order)
    VARIANT_COLORS = {
        'mode0': '#4472C4',  # Blue
        'mode1': '#70AD47',  # Green
        # Mode 2 with pressure=1 (existing variants)
        'mode2_global_notify': '#FFC000',  # Orange
        'mode2_global_nonotify': '#ED7D31',  # Dark orange
        'mode2_local_notify': '#FF6B6B',  # Red
        'mode2_local_nonotify': '#C92A2A',  # Dark red
        'mode2_local_ts_notify': '#FF8787',  # Light red
        'mode2_local_ts_nonotify': '#862E3D',  # Maroon
        # Mode 2 with pressure=0 (new nopressure variants)
        'mode2_nopressure_global': '#9B59B6',  # Purple
        'mode2_nopressure_local': '#8E44AD',  # Dark purple
        'mode2_nopressure_local_ts': '#BB8FCE',  # Light purple
    }

    def create_chart(self, results: List[BenchmarkResult], metric: str,
                     output_path: str, baseline_name: str = 'mode0'):
        """Create grouped bar chart (Option A: benchmarks as groups, variants within)"""
        sorted_results = TableFormatter.sort_variants(results)

        # Get baseline values
        baseline = next((r for r in sorted_results if r.variant.get_short_name() == baseline_name), None)
        if not baseline:
            print(f"Warning: Baseline {baseline_name} not found")
            return

        # Calculate relative values
        data = {}  # {benchmark_name: {variant_name: relative_value}}
        for bench_name in TableFormatter.BENCHMARK_ORDER:
            if bench_name not in baseline.benchmarks:
                continue

            baseline_value = getattr(baseline.benchmarks[bench_name], metric)
            if baseline_value == 0:
                continue

            data[bench_name] = {}
            for result in sorted_results:
                if bench_name in result.benchmarks:
                    value = getattr(result.benchmarks[bench_name], metric)
                    relative = value / baseline_value
                    data[bench_name][result.variant.get_short_name()] = relative

        # Generate SVG
        svg_lines = []
        svg_lines.append(f'<svg width="{self.WIDTH}" height="{self.HEIGHT}" xmlns="http://www.w3.org/2000/svg">')
        svg_lines.append('<style>')
        svg_lines.append('  text { font-family: Arial, sans-serif; }')
        svg_lines.append('  .title { font-size: 18px; font-weight: bold; }')
        svg_lines.append('  .axis-label { font-size: 12px; }')
        svg_lines.append('  .legend-text { font-size: 11px; }')
        svg_lines.append('  .grid-line { stroke: #e0e0e0; stroke-width: 1; }')
        svg_lines.append('</style>')

        # Title
        metric_title = "Execution Time" if metric == "execution_time" else "Time per Input"
        svg_lines.append(f'<text x="{self.WIDTH/2}" y="30" class="title" text-anchor="middle">')
        svg_lines.append(f'  {metric_title} (Relative to {baseline_name})')
        svg_lines.append('</text>')

        # Calculate chart dimensions
        chart_width = self.WIDTH - self.MARGIN_LEFT - self.MARGIN_RIGHT
        chart_height = self.HEIGHT - self.MARGIN_TOP - self.MARGIN_BOTTOM

        # Calculate max value for Y-axis
        all_values = [v for bench_data in data.values() for v in bench_data.values()]
        max_value = max(all_values) if all_values else 2.0
        y_max = max(2.0, max_value * 1.1)  # At least 2.0, or 110% of max

        # Draw Y-axis grid lines and labels
        num_y_ticks = 5
        for i in range(num_y_ticks + 1):
            y_value = (y_max / num_y_ticks) * i
            y_pos = self.MARGIN_TOP + chart_height - (chart_height * i / num_y_ticks)

            # Grid line
            svg_lines.append(f'<line x1="{self.MARGIN_LEFT}" y1="{y_pos}" ')
            svg_lines.append(f'      x2="{self.MARGIN_LEFT + chart_width}" y2="{y_pos}" class="grid-line"/>')

            # Y-axis label
            svg_lines.append(f'<text x="{self.MARGIN_LEFT - 10}" y="{y_pos + 4}" ')
            svg_lines.append(f'      class="axis-label" text-anchor="end">{y_value:.1f}x</text>')

        # Draw baseline line at 1.0x
        baseline_y = self.MARGIN_TOP + chart_height - (chart_height * (1.0 / y_max))
        svg_lines.append(f'<line x1="{self.MARGIN_LEFT}" y1="{baseline_y}" ')
        svg_lines.append(f'      x2="{self.MARGIN_LEFT + chart_width}" y2="{baseline_y}" ')
        svg_lines.append('      stroke="#333" stroke-width="2" stroke-dasharray="5,5"/>')

        # Draw bars
        num_benchmarks = len(data)
        num_variants = len(sorted_results)
        group_width = chart_width / num_benchmarks
        bar_width = (group_width * 0.8) / num_variants

        for bench_idx, bench_name in enumerate(TableFormatter.BENCHMARK_ORDER):
            if bench_name not in data:
                continue

            bench_data = data[bench_name]
            group_x = self.MARGIN_LEFT + (bench_idx * group_width)

            for var_idx, result in enumerate(sorted_results):
                variant_name = result.variant.get_short_name()
                if variant_name not in bench_data:
                    continue

                relative_value = bench_data[variant_name]
                bar_height = (relative_value / y_max) * chart_height
                bar_x = group_x + (var_idx * bar_width) + (group_width * 0.1)
                bar_y = self.MARGIN_TOP + chart_height - bar_height

                color = self.VARIANT_COLORS.get(variant_name, '#999999')

                # Draw bar
                svg_lines.append(f'<rect x="{bar_x}" y="{bar_y}" width="{bar_width}" height="{bar_height}" ')
                svg_lines.append(f'      fill="{color}" stroke="#333" stroke-width="1">')
                svg_lines.append(f'  <title>{variant_name}: {relative_value:.3f}x</title>')
                svg_lines.append('</rect>')

            # X-axis label (benchmark name)
            label_x = group_x + group_width / 2
            label_y = self.MARGIN_TOP + chart_height + 20
            display_name = TableFormatter.BENCHMARK_DISPLAY_NAMES[bench_name]
            svg_lines.append(f'<text x="{label_x}" y="{label_y}" class="axis-label" ')
            svg_lines.append(f'      text-anchor="middle" transform="rotate(-45 {label_x} {label_y})">')
            svg_lines.append(f'  {display_name}')
            svg_lines.append('</text>')

        # Draw legend
        legend_x = self.WIDTH - self.MARGIN_RIGHT + 20
        legend_y = self.MARGIN_TOP
        svg_lines.append(f'<text x="{legend_x}" y="{legend_y}" class="legend-text" font-weight="bold">Variants</text>')

        for idx, result in enumerate(sorted_results):
            variant_name = result.variant.get_short_name()
            color = self.VARIANT_COLORS.get(variant_name, '#999999')
            y = legend_y + 20 + (idx * 25)

            # Color box
            svg_lines.append(f'<rect x="{legend_x}" y="{y - 10}" width="15" height="15" fill="{color}" stroke="#333"/>')

            # Variant name
            svg_lines.append(f'<text x="{legend_x + 20}" y="{y + 2}" class="legend-text">{variant_name}</text>')

        # Y-axis label
        svg_lines.append(f'<text x="20" y="{self.HEIGHT/2}" class="axis-label" ')
        svg_lines.append(f'      text-anchor="middle" transform="rotate(-90 20 {self.HEIGHT/2})">Relative Performance</text>')

        svg_lines.append('</svg>')

        # Write to file
        with open(output_path, 'w') as f:
            f.write('\n'.join(svg_lines))


def main():
    parser = argparse.ArgumentParser(
        description='Analyze benchmark results from resource_limited_ubenches'
    )
    parser.add_argument('input_file', help='Input results file (e.g., results.txt)')
    parser.add_argument('--output-dir', default='.', help='Output directory for generated files')
    parser.add_argument('--table-only', action='store_true', help='Generate only tables')
    parser.add_argument('--chart-only', action='store_true', help='Generate only charts')
    parser.add_argument('--no-stdout', action='store_true', help='Do not print tables to stdout')

    args = parser.parse_args()

    # Parse results
    print(f"Parsing {args.input_file}...")
    parser_obj = ResultsParser()
    results = parser_obj.parse_file(args.input_file)

    if not results:
        print("Error: No results found in input file")
        return 1

    print(f"Found {len(results)} variant results")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True, parents=True)

    formatter = TableFormatter()
    chart_gen = SVGChartGenerator()

    # Generate for both metrics
    metrics = ['execution_time', 'time_per_input']
    metric_titles = {'execution_time': 'Execution Time', 'time_per_input': 'Time per Input'}

    for metric in metrics:
        print(f"\n{'='*80}")
        print(f"Processing metric: {metric_titles[metric]}")
        print(f"{'='*80}")

        # Generate table
        if not args.chart_only:
            table = formatter.create_table(results, metric, relative_to_baseline=True)
            stats = formatter.create_summary_stats(results, metric)

            # Save table to file
            table_file = output_dir / f"benchmark_table_{metric}.md"
            with open(table_file, 'w') as f:
                f.write(f"# Benchmark Results: {metric_titles[metric]}\n\n")
                f.write(table)
                f.write('\n')
                f.write(stats)
            print(f"Table saved to: {table_file}")

            # Print to stdout
            if not args.no_stdout:
                print(f"\n{metric_titles[metric]} Table:")
                print(table)
                print(stats)

        # Generate chart
        if not args.table_only:
            chart_file = output_dir / f"benchmark_chart_{metric}.svg"
            chart_gen.create_chart(results, metric, str(chart_file))
            print(f"Chart saved to: {chart_file}")

    print(f"\n{'='*80}")
    print("Analysis complete!")
    print(f"{'='*80}")

    return 0


if __name__ == '__main__':
    sys.exit(main())
