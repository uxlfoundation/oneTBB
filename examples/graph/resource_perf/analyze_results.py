#!/usr/bin/env python3
"""
Workload-Specific Results Analyzer for run_benchmarks.py JSON Output

Analyzes JSON results from run_benchmarks.py and generates visualizations:
- Resource sweep charts (execution time vs. num_resources)
- Arrival rate charts (execution time vs. generation_rate)
- Mode comparison line charts (modes 0/1/2 trends)
- Speedup analysis (relative to mode0 baseline)

Usage:
    # List available charts for a workload
    python analyze_results.py chain_test.json --list

    # Generate resource sweep chart
    python analyze_results.py chain_test.json --chart resource-sweep --output chain_sweep.png

    # Generate mode comparison
    python analyze_results.py chain_test.json --chart mode-comparison --output chain_modes.png

    # Generate all charts for a workload
    python analyze_results.py chain_test.json --chart all --output-dir ./charts

    # Print summary statistics
    python analyze_results.py chain_test.json --summary
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from collections import defaultdict
from dataclasses import dataclass

try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend
    import matplotlib.pyplot as plt
    import numpy as np
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Warning: matplotlib not available. Install with: pip install matplotlib")


@dataclass
class ResultData:
    """Parsed result from JSON"""
    benchmark: str
    mode: int
    args: List[str]
    description: str
    execution_time_s: float
    returncode: int
    stdout: str

    # Parsed parameters (extracted from args)
    num_executions: Optional[int] = None
    num_inputs: Optional[int] = None
    num_nodes: Optional[int] = None
    generation_rate: Optional[float] = None
    num_resources: Optional[int] = None
    num_philosophers: Optional[int] = None
    num_times: Optional[int] = None
    work_time_ms: Optional[float] = None
    num_limiters: Optional[int] = None  # For baseline_cycle_bench


class ResultsLoader:
    """Load and parse JSON results from run_benchmarks.py"""

    @staticmethod
    def load(filepath: str) -> List[ResultData]:
        """Load JSON file and parse into ResultData objects"""
        with open(filepath, 'r') as f:
            data = json.load(f)

        results = []
        for r in data['results']:
            result = ResultData(
                benchmark=r['benchmark'],
                mode=r['mode'],
                args=r['args'],
                description=r['description'],
                execution_time_s=r['execution_time_s'],
                returncode=r['returncode'],
                stdout=r.get('stdout', '')
            )

            # Parse common arguments based on benchmark type
            args = result.args
            if result.benchmark in ['genie_bench', 'genie_diamond_bench']:
                # Args: num_executions, num_inputs, generation_rate
                if len(args) >= 3:
                    result.num_executions = int(args[0])
                    result.num_inputs = int(args[1])
                    result.generation_rate = float(args[2])
            elif result.benchmark == 'dining_philosophers_bench':
                # Args: num_executions, num_philosophers, num_times, work_time_ms
                if len(args) >= 1:
                    result.num_executions = int(args[0])
                if len(args) >= 2:
                    result.num_philosophers = int(args[1])
                if len(args) >= 3:
                    result.num_times = int(args[2])
                if len(args) >= 4:
                    result.work_time_ms = float(args[3])
            elif result.benchmark == 'baseline_cycle_bench':
                # Args: num_executions, num_inputs, n_f (num_nodes), generation_rate, num_limiters
                if len(args) >= 1:
                    result.num_executions = int(args[0])
                if len(args) >= 2:
                    result.num_inputs = int(args[1])
                if len(args) >= 3:
                    result.num_nodes = int(args[2])  # N_F
                if len(args) >= 4:
                    result.generation_rate = float(args[3])
                if len(args) >= 5:
                    result.num_limiters = int(args[4])  # L
            else:
                # Most benchmarks: num_executions, num_inputs, num_nodes, generation_rate, num_resources
                if len(args) >= 1:
                    result.num_executions = int(args[0])
                if len(args) >= 2:
                    result.num_inputs = int(args[1])
                if len(args) >= 3:
                    result.num_nodes = int(args[2])
                if len(args) >= 4:
                    result.generation_rate = float(args[3])
                if len(args) >= 5:
                    result.num_resources = int(args[4])

            results.append(result)

        return results


class WorkloadAnalyzer:
    """Analyze workload results and generate charts"""

    MODE_COLORS = {
        0: '#4472C4',  # Blue
        1: '#70AD47',  # Green
        2: '#FF6B6B',  # Red
    }

    MODE_NAMES = {
        0: 'Mode 0 (join_node)',
        1: 'Mode 1 (FCFS limiter)',
        2: 'Mode 2 (priority-aware)',
    }

    def __init__(self, results: List[ResultData]):
        self.results = results
        self.benchmark_type = results[0].benchmark if results else None

    def print_summary(self):
        """Print summary statistics"""
        print("\n" + "="*80)
        print("WORKLOAD SUMMARY")
        print("="*80)

        print(f"\nBenchmark: {self.benchmark_type}")
        print(f"Total configurations: {len(self.results)}")

        # Group by mode
        by_mode = defaultdict(list)
        for r in self.results:
            by_mode[r.mode].append(r)

        print(f"\nConfigurations by mode:")
        for mode in sorted(by_mode.keys()):
            print(f"  Mode {mode}: {len(by_mode[mode])} runs")

        # Execution time stats
        print(f"\nExecution time statistics:")
        times = [r.execution_time_s for r in self.results if r.returncode == 0]
        if times:
            print(f"  Min: {min(times):.2f}s")
            print(f"  Max: {max(times):.2f}s")
            print(f"  Mean: {sum(times)/len(times):.2f}s")
            print(f"  Median: {sorted(times)[len(times)//2]:.2f}s")

        # Best performer per mode
        print(f"\nBest execution time per mode:")
        for mode in sorted(by_mode.keys()):
            mode_results = [r for r in by_mode[mode] if r.returncode == 0]
            if mode_results:
                best = min(mode_results, key=lambda x: x.execution_time_s)
                print(f"  Mode {mode}: {best.execution_time_s:.2f}s - {best.description}")

        # Failures
        failures = [r for r in self.results if r.returncode != 0]
        if failures:
            print(f"\nFailed runs: {len(failures)}")
            for r in failures:
                print(f"  - {r.description} (exit code {r.returncode})")

        print("\n" + "="*80 + "\n")

    def list_available_charts(self):
        """List available chart types for this workload"""
        print("\n" + "="*80)
        print("AVAILABLE CHARTS")
        print("="*80 + "\n")

        # Detect workload type from results
        has_resource_sweep = any(r.num_resources is not None for r in self.results)
        has_rate_sweep = any(r.generation_rate is not None for r in self.results)
        has_philosopher_scaling = any(r.num_philosophers is not None for r in self.results)
        has_work_time = any(r.work_time_ms is not None for r in self.results)
        has_multiple_modes = len(set(r.mode for r in self.results)) > 1

        print("Available chart types for this workload:\n")

        if has_multiple_modes:
            print("  mode-comparison")
            print("    Line chart comparing modes 0/1/2 performance trends")
            print()

        if has_resource_sweep:
            print("  resource-sweep")
            print("    Execution time vs. num_resources (for chain/siblings/tree workloads)")
            print()

        if has_rate_sweep and self.benchmark_type in ['genie_bench', 'genie_diamond_bench']:
            print("  rate-sweep")
            print("    Execution time vs. arrival rate (for genie workloads)")
            print()

        if has_philosopher_scaling:
            print("  philosopher-scaling")
            print("    Execution time vs. num_philosophers (for dining_philosophers workload)")
            print()

        if has_work_time:
            print("  work-time")
            print("    Execution time vs. work_time_ms (for dining_philosophers workload)")
            print()

        # Baseline-specific charts
        has_limiters = any(r.num_limiters is not None for r in self.results)
        if self.benchmark_type == 'baseline_cycle_bench' and has_limiters:
            print("  baseline-nf-scaling")
            print("    Overhead vs. N_F (number of nodes) - shows if overhead grows with consumers")
            print()
            print("  baseline-limiter-scaling")
            print("    Overhead vs. L (number of limiters/handles) - shows handle overhead")
            print()
            print("  baseline-overhead-heatmap")
            print("    2D heatmap of overhead with N_F and L")
            print()

        # Chain-specific charts
        if self.benchmark_type == 'chain_bench' and has_resource_sweep and has_rate_sweep:
            print("  chain-pipelining")
            print("    Execution time vs. R_L for different arrival rates (shows pipelining effect)")
            print()
            print("  chain-speedup-by-rate")
            print("    Speedup vs. R_L grouped by arrival rate r (answers pipelining questions)")
            print()

        # Siblings-specific charts
        if self.benchmark_type == 'siblings_bench' and has_resource_sweep:
            print("  siblings-graph-parallelism")
            print("    Speedup vs. R_L for r=1 (shows graph-based parallelism effectiveness)")
            print()
            print("  siblings-scaling-by-n")
            print("    Execution time vs. R_L grouped by N (number of sibling nodes)")
            print()

        print("  speedup")
        print("    Speedup relative to mode0 baseline")
        print()

        print("  all")
        print("    Generate all applicable charts")
        print()

        print("Usage examples:")
        print(f"  python analyze_results.py results.json --chart mode-comparison --output modes.png")
        print(f"  python analyze_results.py results.json --chart all --output-dir ./charts")
        print("\n" + "="*80 + "\n")

    def create_resource_sweep_chart(self, output_path: str):
        """Create resource sweep chart (execution time vs num_resources)"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting. Install with: pip install matplotlib")
            return

        # Group by mode and num_resources
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            if r.num_resources is not None and r.returncode == 0:
                data_by_mode[r.mode][r.num_resources].append(r.execution_time_s)

        if not data_by_mode:
            print("No resource sweep data found in results")
            return

        # Create plot
        plt.figure(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            resources = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][r]) for r in resources]

            plt.plot(resources, times,
                    marker='o',
                    linewidth=2,
                    markersize=8,
                    color=self.MODE_COLORS[mode],
                    label=self.MODE_NAMES[mode])

        plt.xlabel('Number of Resources', fontsize=12, fontweight='bold')
        plt.ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        plt.title(f'{self.benchmark_type}: Resource Scaling', fontsize=14, fontweight='bold')
        plt.legend(fontsize=10)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()

        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved resource sweep chart: {output_path}")

    def create_rate_sweep_chart(self, output_path: str):
        """Create arrival rate sweep chart (execution time vs generation_rate)"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Group by mode and generation_rate
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            if r.generation_rate is not None and r.returncode == 0:
                data_by_mode[r.mode][r.generation_rate].append(r.execution_time_s)

        if not data_by_mode:
            print("No rate sweep data found in results")
            return

        # Create plot
        plt.figure(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            rates = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][r]) for r in rates]

            # Use log scale for x-axis if rates span large range
            plt.plot(rates, times,
                    marker='o',
                    linewidth=2,
                    markersize=8,
                    color=self.MODE_COLORS[mode],
                    label=self.MODE_NAMES[mode])

        plt.xlabel('Arrival Rate (r)', fontsize=12, fontweight='bold')
        plt.ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        plt.title(f'{self.benchmark_type}: Arrival Rate Sweep', fontsize=14, fontweight='bold')
        plt.legend(fontsize=10)
        plt.grid(True, alpha=0.3)

        # Use log scale if rates span > 1 order of magnitude
        rates_all = [r for mode_data in data_by_mode.values() for r in mode_data.keys()]
        if rates_all and max(rates_all) / min(rates_all) > 10:
            plt.xscale('log')
            plt.xlabel('Arrival Rate (r) - log scale', fontsize=12, fontweight='bold')

        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved rate sweep chart: {output_path}")

    def create_mode_comparison_chart(self, output_path: str):
        """Create mode comparison line chart"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Detect the varying parameter (num_resources, generation_rate, or num_philosophers)
        param_name = None
        param_getter = None

        if any(r.num_resources is not None for r in self.results):
            param_name = 'Number of Resources'
            param_getter = lambda r: r.num_resources
        elif any(r.generation_rate is not None for r in self.results):
            param_name = 'Arrival Rate'
            param_getter = lambda r: r.generation_rate
        elif any(r.num_philosophers is not None for r in self.results):
            param_name = 'Number of Philosophers'
            param_getter = lambda r: r.num_philosophers
        else:
            print("No varying parameter found for mode comparison")
            return

        # Group by mode and parameter value
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            param_val = param_getter(r)
            if param_val is not None and r.returncode == 0:
                data_by_mode[r.mode][param_val].append(r.execution_time_s)

        if not data_by_mode:
            print("No data for mode comparison")
            return

        # Create plot
        plt.figure(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            params = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][p]) for p in params]

            plt.plot(params, times,
                    marker='o',
                    linewidth=2,
                    markersize=8,
                    color=self.MODE_COLORS[mode],
                    label=self.MODE_NAMES[mode])

        plt.xlabel(param_name, fontsize=12, fontweight='bold')
        plt.ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        plt.title(f'{self.benchmark_type}: Mode Comparison', fontsize=14, fontweight='bold')
        plt.legend(fontsize=10)
        plt.grid(True, alpha=0.3)

        # Use log scale if parameter spans > 1 order of magnitude
        params_all = [p for mode_data in data_by_mode.values() for p in mode_data.keys()]
        if params_all and max(params_all) / min(params_all) > 10:
            plt.xscale('log')
            plt.xlabel(f'{param_name} - log scale', fontsize=12, fontweight='bold')

        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved mode comparison chart: {output_path}")

    def create_speedup_chart(self, output_path: str):
        """Create speedup chart (relative to mode0 baseline)"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Get mode0 baseline data
        mode0_data = {}
        for r in self.results:
            if r.mode == 0 and r.returncode == 0:
                # Use description as key (unique per configuration)
                mode0_data[r.description.replace('Mode 0', 'Mode X')] = r.execution_time_s

        if not mode0_data:
            print("No mode0 baseline data found")
            return

        # Calculate speedups for other modes
        speedups = defaultdict(list)
        labels = []

        for r in self.results:
            if r.mode != 0 and r.returncode == 0:
                # Match configuration
                key = r.description.replace(f'Mode {r.mode}', 'Mode X')
                if key in mode0_data:
                    speedup = mode0_data[key] / r.execution_time_s
                    speedups[r.mode].append(speedup)
                    if r.mode == 1 and len(labels) < len(speedups[1]):
                        # Extract parameter description
                        parts = r.description.split(',')[1:]
                        labels.append(','.join(parts).strip() if parts else r.description)

        if not speedups:
            print("No speedup data to chart")
            return

        # Create grouped bar chart
        x = np.arange(len(labels))
        width = 0.35

        fig, ax = plt.subplots(figsize=(12, 6))

        for i, mode in enumerate(sorted(speedups.keys())):
            offset = width * (i - 0.5)
            ax.bar(x + offset, speedups[mode], width,
                  label=self.MODE_NAMES[mode],
                  color=self.MODE_COLORS[mode])

        # Horizontal line at 1.0x (baseline)
        ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.5)

        ax.set_xlabel('Configuration', fontsize=12, fontweight='bold')
        ax.set_ylabel('Speedup vs Mode 0', fontsize=12, fontweight='bold')
        ax.set_title(f'{self.benchmark_type}: Speedup Relative to Mode 0', fontsize=14, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(labels, rotation=45, ha='right', fontsize=8)
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3, axis='y')

        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved speedup chart: {output_path}")

    def create_philosopher_scaling_chart(self, output_path: str):
        """Create philosopher scaling chart (execution time vs num_philosophers)"""
        if not HAS_MATPLOTLIB:
            print("Warning: matplotlib not available, skipping philosopher scaling chart")
            return

        # Group by mode and num_philosophers
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            if r.num_philosophers is not None and r.returncode == 0:
                data_by_mode[r.mode][r.num_philosophers].append(r.execution_time_s)

        if not data_by_mode:
            print("No philosopher scaling data available")
            return

        fig, ax = plt.subplots(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            philosophers = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][n]) for n in philosophers]
            errors = [np.std(data_by_mode[mode][n]) if len(data_by_mode[mode][n]) > 1 else 0
                     for n in philosophers]

            ax.errorbar(philosophers, times, yerr=errors,
                       marker='o', label=self.MODE_NAMES[mode],
                       color=self.MODE_COLORS[mode], capsize=5, linewidth=2, markersize=8)

        ax.set_xlabel('Number of Philosophers', fontsize=12, fontweight='bold')
        ax.set_ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        ax.set_title('Dining Philosophers: Execution Time vs Problem Size', fontsize=14, fontweight='bold')
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved philosopher scaling chart: {output_path}")

    def create_work_time_chart(self, output_path: str):
        """Create work time chart (execution time vs work_time_ms)"""
        if not HAS_MATPLOTLIB:
            print("Warning: matplotlib not available, skipping work time chart")
            return

        # Group by mode and work_time_ms
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            if r.work_time_ms is not None and r.returncode == 0:
                data_by_mode[r.mode][r.work_time_ms].append(r.execution_time_s)

        if not data_by_mode:
            print("No work time data available")
            return

        fig, ax = plt.subplots(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            work_times = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][w]) for w in work_times]
            errors = [np.std(data_by_mode[mode][w]) if len(data_by_mode[mode][w]) > 1 else 0
                     for w in work_times]

            ax.errorbar(work_times, times, yerr=errors,
                       marker='o', label=self.MODE_NAMES[mode],
                       color=self.MODE_COLORS[mode], capsize=5, linewidth=2, markersize=8)

        ax.set_xlabel('Work Time per Think/Eat (ms)', fontsize=12, fontweight='bold')
        ax.set_ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        ax.set_title('Dining Philosophers: Execution Time vs Work Size', fontsize=14, fontweight='bold')
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)
        ax.set_xscale('log')

        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved work time chart: {output_path}")

    def create_baseline_nf_scaling_chart(self, output_path: str):
        """Create baseline N_F scaling chart (overhead vs number of nodes)"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Group by mode and N_F (num_nodes), averaging over different L values
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            if r.num_nodes is not None and r.returncode == 0:
                data_by_mode[r.mode][r.num_nodes].append(r.execution_time_s)

        if not data_by_mode:
            print("No N_F scaling data found")
            return

        plt.figure(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            n_f_values = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][nf]) for nf in n_f_values]
            errors = [np.std(data_by_mode[mode][nf]) if len(data_by_mode[mode][nf]) > 1 else 0
                     for nf in n_f_values]

            plt.errorbar(n_f_values, times, yerr=errors,
                        marker='o', linewidth=2, markersize=8, capsize=5,
                        color=self.MODE_COLORS[mode],
                        label=self.MODE_NAMES[mode])

        plt.xlabel('N_F (Number of Nodes)', fontsize=12, fontweight='bold')
        plt.ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        plt.title('Baseline Cycle: Overhead vs Number of Consumers', fontsize=14, fontweight='bold')
        plt.legend(fontsize=10)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()

        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved N_F scaling chart: {output_path}")

    def create_baseline_limiter_scaling_chart(self, output_path: str):
        """Create baseline limiter scaling chart (overhead vs number of limiters/handles)"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Group by mode and L (num_limiters), averaging over different N_F values
        data_by_mode = defaultdict(lambda: defaultdict(list))

        for r in self.results:
            if r.num_limiters is not None and r.returncode == 0:
                data_by_mode[r.mode][r.num_limiters].append(r.execution_time_s)

        if not data_by_mode:
            print("No limiter scaling data found")
            return

        plt.figure(figsize=(10, 6))

        for mode in sorted(data_by_mode.keys()):
            limiters = sorted(data_by_mode[mode].keys())
            times = [np.mean(data_by_mode[mode][l]) for l in limiters]
            errors = [np.std(data_by_mode[mode][l]) if len(data_by_mode[mode][l]) > 1 else 0
                     for l in limiters]

            plt.errorbar(limiters, times, yerr=errors,
                        marker='o', linewidth=2, markersize=8, capsize=5,
                        color=self.MODE_COLORS[mode],
                        label=self.MODE_NAMES[mode])

        plt.xlabel('L (Number of Limiter Nodes / Handles)', fontsize=12, fontweight='bold')
        plt.ylabel('Execution Time (seconds)', fontsize=12, fontweight='bold')
        plt.title('Baseline Cycle: Overhead vs Number of Handles', fontsize=14, fontweight='bold')
        plt.legend(fontsize=10)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()

        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved limiter scaling chart: {output_path}")

    def create_baseline_overhead_heatmap(self, output_path: str):
        """Create 2D heatmap showing overhead for different N_F and L combinations"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Create separate heatmap for each mode
        fig, axes = plt.subplots(1, 3, figsize=(18, 5))

        for mode_idx, mode in enumerate(sorted(set(r.mode for r in self.results))):
            # Group by (N_F, L) pairs
            data_grid = defaultdict(lambda: defaultdict(list))

            for r in self.results:
                if r.mode == mode and r.num_nodes is not None and r.num_limiters is not None and r.returncode == 0:
                    data_grid[r.num_nodes][r.num_limiters].append(r.execution_time_s)

            if not data_grid:
                continue

            # Convert to 2D array
            n_f_values = sorted(data_grid.keys())
            l_values = sorted(set(l for nf_data in data_grid.values() for l in nf_data.keys()))

            heatmap_data = np.zeros((len(n_f_values), len(l_values)))
            for i, nf in enumerate(n_f_values):
                for j, l in enumerate(l_values):
                    if l in data_grid[nf]:
                        heatmap_data[i, j] = np.mean(data_grid[nf][l])

            # Create heatmap
            im = axes[mode_idx].imshow(heatmap_data, cmap='YlOrRd', aspect='auto')
            axes[mode_idx].set_xticks(range(len(l_values)))
            axes[mode_idx].set_yticks(range(len(n_f_values)))
            axes[mode_idx].set_xticklabels(l_values)
            axes[mode_idx].set_yticklabels(n_f_values)
            axes[mode_idx].set_xlabel('L (Limiters)', fontsize=10, fontweight='bold')
            axes[mode_idx].set_ylabel('N_F (Nodes)', fontsize=10, fontweight='bold')
            axes[mode_idx].set_title(self.MODE_NAMES[mode], fontsize=11, fontweight='bold')

            # Add colorbar
            cbar = plt.colorbar(im, ax=axes[mode_idx])
            cbar.set_label('Execution Time (s)', fontsize=9)

            # Add text annotations
            for i in range(len(n_f_values)):
                for j in range(len(l_values)):
                    if heatmap_data[i, j] > 0:
                        text = axes[mode_idx].text(j, i, f'{heatmap_data[i, j]:.2f}',
                                                   ha="center", va="center", color="black", fontsize=8)

        fig.suptitle('Baseline Cycle: Overhead Heatmap (N_F vs L)', fontsize=14, fontweight='bold')
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved overhead heatmap: {output_path}")

    def create_chain_pipelining_chart(self, output_path: str):
        """Create chain pipelining chart showing execution time vs R_L for different rates"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Group by mode, rate, and num_resources
        data_by_mode_rate = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))

        for r in self.results:
            if r.num_resources is not None and r.generation_rate is not None and r.returncode == 0:
                data_by_mode_rate[r.mode][r.generation_rate][r.num_resources].append(r.execution_time_s)

        if not data_by_mode_rate:
            print("No chain pipelining data found")
            return

        # Get unique rates
        all_rates = sorted(set(rate for mode_data in data_by_mode_rate.values()
                              for rate in mode_data.keys()))

        # Create subplots: one per rate
        n_rates = len(all_rates)
        fig, axes = plt.subplots(1, n_rates, figsize=(6 * n_rates, 5))
        if n_rates == 1:
            axes = [axes]

        for rate_idx, rate in enumerate(all_rates):
            ax = axes[rate_idx]

            for mode in sorted(data_by_mode_rate.keys()):
                if rate in data_by_mode_rate[mode]:
                    resources = sorted(data_by_mode_rate[mode][rate].keys())
                    times = [np.mean(data_by_mode_rate[mode][rate][r]) for r in resources]

                    ax.plot(resources, times,
                           marker='o', linewidth=2, markersize=8,
                           color=self.MODE_COLORS[mode],
                           label=self.MODE_NAMES[mode])

            ax.set_xlabel('R_L (Number of Resources)', fontsize=11, fontweight='bold')
            ax.set_ylabel('Execution Time (seconds)', fontsize=11, fontweight='bold')
            ax.set_title(f'r = {rate}', fontsize=12, fontweight='bold')
            ax.legend(fontsize=9)
            ax.grid(True, alpha=0.3)

        fig.suptitle('Chain: Pipelining Effect (Execution Time vs Resources)',
                    fontsize=14, fontweight='bold', y=1.02)
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved chain pipelining chart: {output_path}")

    def create_chain_speedup_by_rate_chart(self, output_path: str):
        """Create chain speedup chart showing speedup vs R_L grouped by rate"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Get baseline data (R_L=1 for each mode and rate)
        baseline_data = {}  # key: (mode, rate) -> execution_time

        for r in self.results:
            if r.num_resources == 1 and r.generation_rate is not None and r.returncode == 0:
                baseline_data[(r.mode, r.generation_rate)] = r.execution_time_s

        if not baseline_data:
            print("No baseline data (R_L=1) found for speedup calculation")
            return

        # Calculate speedups
        speedup_by_mode_rate = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))

        for r in self.results:
            if r.num_resources is not None and r.generation_rate is not None and r.returncode == 0:
                key = (r.mode, r.generation_rate)
                if key in baseline_data:
                    speedup = baseline_data[key] / r.execution_time_s
                    speedup_by_mode_rate[r.mode][r.generation_rate][r.num_resources].append(speedup)

        if not speedup_by_mode_rate:
            print("No speedup data available")
            return

        # Get unique rates
        all_rates = sorted(set(rate for mode_data in speedup_by_mode_rate.values()
                              for rate in mode_data.keys()))

        # Create subplots: one per rate
        n_rates = len(all_rates)
        fig, axes = plt.subplots(1, n_rates, figsize=(6 * n_rates, 5))
        if n_rates == 1:
            axes = [axes]

        for rate_idx, rate in enumerate(all_rates):
            ax = axes[rate_idx]

            for mode in sorted(speedup_by_mode_rate.keys()):
                if rate in speedup_by_mode_rate[mode]:
                    resources = sorted(speedup_by_mode_rate[mode][rate].keys())
                    speedups = [np.mean(speedup_by_mode_rate[mode][rate][r]) for r in resources]

                    ax.plot(resources, speedups,
                           marker='o', linewidth=2, markersize=8,
                           color=self.MODE_COLORS[mode],
                           label=self.MODE_NAMES[mode])

            # Add baseline line at y=1.0
            ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.5)

            ax.set_xlabel('R_L (Number of Resources)', fontsize=11, fontweight='bold')
            ax.set_ylabel('Speedup vs R_L=1', fontsize=11, fontweight='bold')
            ax.set_title(f'r = {rate}', fontsize=12, fontweight='bold')
            ax.legend(fontsize=9)
            ax.grid(True, alpha=0.3)

        fig.suptitle('Chain: Speedup from Pipelining (Speedup vs Resources)',
                    fontsize=14, fontweight='bold', y=1.02)
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved chain speedup by rate chart: {output_path}")

    def create_siblings_graph_parallelism_chart(self, output_path: str):
        """Create siblings graph parallelism chart showing speedup vs R_L for r=1"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Filter for r=1 configurations only and get baseline data (R_L=1)
        baseline_data = {}  # key: (mode, num_nodes) -> execution_time

        for r in self.results:
            if (r.generation_rate == 1.0 and r.num_resources == 1 and
                r.num_nodes is not None and r.returncode == 0):
                baseline_data[(r.mode, r.num_nodes)] = r.execution_time_s

        if not baseline_data:
            print("No baseline data (r=1, R_L=1) found for speedup calculation")
            return

        # Calculate speedups for r=1 configurations
        speedup_by_mode_n = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))

        for r in self.results:
            if (r.generation_rate == 1.0 and r.num_resources is not None and
                r.num_nodes is not None and r.returncode == 0):
                key = (r.mode, r.num_nodes)
                if key in baseline_data:
                    speedup = baseline_data[key] / r.execution_time_s
                    speedup_by_mode_n[r.mode][r.num_nodes][r.num_resources].append(speedup)

        if not speedup_by_mode_n:
            print("No speedup data available")
            return

        # Get unique N values
        all_n_values = sorted(set(n for mode_data in speedup_by_mode_n.values()
                                  for n in mode_data.keys()))

        # Create subplots: one per N value
        n_plots = len(all_n_values)
        fig, axes = plt.subplots(1, n_plots, figsize=(6 * n_plots, 5))
        if n_plots == 1:
            axes = [axes]

        for n_idx, n_nodes in enumerate(all_n_values):
            ax = axes[n_idx]

            for mode in sorted(speedup_by_mode_n.keys()):
                if n_nodes in speedup_by_mode_n[mode]:
                    resources = sorted(speedup_by_mode_n[mode][n_nodes].keys())
                    speedups = [np.mean(speedup_by_mode_n[mode][n_nodes][r]) for r in resources]

                    ax.plot(resources, speedups,
                           marker='o', linewidth=2, markersize=8,
                           color=self.MODE_COLORS[mode],
                           label=self.MODE_NAMES[mode])

            # Add baseline line at y=1.0
            ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.5)

            # Add ideal speedup line (y=x up to N)
            max_r = max(r for mode_data in speedup_by_mode_n.values()
                       if n_nodes in mode_data for r in mode_data[n_nodes].keys())
            ideal_x = list(range(1, min(max_r, n_nodes) + 1))
            ideal_y = ideal_x
            ax.plot(ideal_x, ideal_y, 'k:', linewidth=1.5, alpha=0.5, label='Ideal (R_L-x)')

            ax.set_xlabel('R_L (Number of Resources)', fontsize=11, fontweight='bold')
            ax.set_ylabel('Speedup vs R_L=1', fontsize=11, fontweight='bold')
            ax.set_title(f'N = {n_nodes} siblings, r = 1', fontsize=12, fontweight='bold')
            ax.legend(fontsize=9)
            ax.grid(True, alpha=0.3)
            ax.set_ylim(bottom=0)

        fig.suptitle('Siblings: Graph-Based Parallelism (r=1, speedup vs R_L)',
                    fontsize=14, fontweight='bold', y=1.02)
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved siblings graph parallelism chart: {output_path}")

    def create_siblings_scaling_by_n_chart(self, output_path: str):
        """Create siblings scaling chart showing execution time vs R_L grouped by N"""
        if not HAS_MATPLOTLIB:
            print("Error: matplotlib required for charting")
            return

        # Group by mode, N, and R_L (filter for r=1 only)
        data_by_mode_n = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))

        for r in self.results:
            if (r.generation_rate == 1.0 and r.num_resources is not None and
                r.num_nodes is not None and r.returncode == 0):
                data_by_mode_n[r.mode][r.num_nodes][r.num_resources].append(r.execution_time_s)

        if not data_by_mode_n:
            print("No data found for siblings scaling")
            return

        # Get unique N values
        all_n_values = sorted(set(n for mode_data in data_by_mode_n.values()
                                  for n in mode_data.keys()))

        # Create subplots: one per mode
        fig, axes = plt.subplots(1, 3, figsize=(18, 5))

        for mode_idx, mode in enumerate(sorted(data_by_mode_n.keys())):
            ax = axes[mode_idx]

            for n_nodes in all_n_values:
                if n_nodes in data_by_mode_n[mode]:
                    resources = sorted(data_by_mode_n[mode][n_nodes].keys())
                    times = [np.mean(data_by_mode_n[mode][n_nodes][r]) for r in resources]

                    ax.plot(resources, times,
                           marker='o', linewidth=2, markersize=8,
                           label=f'N={n_nodes}')

            ax.set_xlabel('R_L (Number of Resources)', fontsize=11, fontweight='bold')
            ax.set_ylabel('Execution Time (seconds)', fontsize=11, fontweight='bold')
            ax.set_title(self.MODE_NAMES[mode], fontsize=12, fontweight='bold')
            ax.legend(fontsize=9)
            ax.grid(True, alpha=0.3)

        fig.suptitle('Siblings: Scaling with N and R_L (r=1)',
                    fontsize=14, fontweight='bold', y=1.02)
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved siblings scaling by N chart: {output_path}")

    def create_all_charts(self, output_dir: Path):
        """Create all applicable charts for this workload"""
        output_dir.mkdir(exist_ok=True, parents=True)

        base_name = self.benchmark_type or 'results'

        # Always create speedup
        self.create_speedup_chart(str(output_dir / f"{base_name}_speedup.png"))

        # Create resource sweep if applicable
        if any(r.num_resources is not None for r in self.results):
            self.create_resource_sweep_chart(str(output_dir / f"{base_name}_resource_sweep.png"))
            # Also create mode comparison (same data, but emphasizes mode differences)
            self.create_mode_comparison_chart(str(output_dir / f"{base_name}_mode_comparison.png"))

        # Create rate sweep for genie benchmarks (replaces mode comparison for these)
        if any(r.generation_rate is not None for r in self.results) and self.benchmark_type in ['genie_bench', 'genie_diamond_bench']:
            self.create_rate_sweep_chart(str(output_dir / f"{base_name}_rate_sweep.png"))

        # Create philosopher scaling if applicable
        if any(r.num_philosophers is not None for r in self.results):
            self.create_philosopher_scaling_chart(str(output_dir / f"{base_name}_philosopher_scaling.png"))
            # Also create mode comparison
            self.create_mode_comparison_chart(str(output_dir / f"{base_name}_mode_comparison.png"))

        # Create work time chart if applicable
        if any(r.work_time_ms is not None for r in self.results):
            self.create_work_time_chart(str(output_dir / f"{base_name}_work_time.png"))

        # Create baseline-specific charts
        if self.benchmark_type == 'baseline_cycle_bench' and any(r.num_limiters is not None for r in self.results):
            self.create_baseline_nf_scaling_chart(str(output_dir / f"{base_name}_nf_scaling.png"))
            self.create_baseline_limiter_scaling_chart(str(output_dir / f"{base_name}_limiter_scaling.png"))
            self.create_baseline_overhead_heatmap(str(output_dir / f"{base_name}_overhead_heatmap.png"))

        # Create chain-specific charts
        if self.benchmark_type == 'chain_bench':
            has_resources = any(r.num_resources is not None for r in self.results)
            has_rates = any(r.generation_rate is not None for r in self.results)
            if has_resources and has_rates:
                self.create_chain_pipelining_chart(str(output_dir / f"{base_name}_pipelining.png"))
                self.create_chain_speedup_by_rate_chart(str(output_dir / f"{base_name}_speedup_by_rate.png"))

        # Create siblings-specific charts
        if self.benchmark_type == 'siblings_bench':
            if any(r.num_resources is not None for r in self.results):
                self.create_siblings_graph_parallelism_chart(str(output_dir / f"{base_name}_graph_parallelism.png"))
                self.create_siblings_scaling_by_n_chart(str(output_dir / f"{base_name}_scaling_by_n.png"))


def main():
    parser = argparse.ArgumentParser(
        description='Analyze JSON results from run_benchmarks.py',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Print summary statistics
  python analyze_results.py chain_test.json --summary

  # List available charts
  python analyze_results.py chain_test.json --list

  # Generate specific chart
  python analyze_results.py chain_test.json --chart mode-comparison --output modes.png

  # Generate all charts
  python analyze_results.py chain_test.json --chart all --output-dir ./charts
        """
    )

    parser.add_argument('json_file', help='JSON results file from run_benchmarks.py')
    parser.add_argument('--summary', action='store_true', help='Print summary statistics')
    parser.add_argument('--list', action='store_true', help='List available chart types')
    parser.add_argument('--chart',
                       choices=['mode-comparison', 'resource-sweep', 'rate-sweep', 'speedup',
                               'philosopher-scaling', 'work-time',
                               'baseline-nf-scaling', 'baseline-limiter-scaling', 'baseline-overhead-heatmap',
                               'chain-pipelining', 'chain-speedup-by-rate',
                               'siblings-graph-parallelism', 'siblings-scaling-by-n',
                               'all'],
                       help='Chart type to generate')
    parser.add_argument('--output', type=Path, help='Output file for single chart (PNG)')
    parser.add_argument('--output-dir', type=Path, default=Path('./charts'),
                       help='Output directory for multiple charts (default: ./charts)')

    args = parser.parse_args()

    # Load results
    print(f"Loading results from {args.json_file}...")
    try:
        results = ResultsLoader.load(args.json_file)
    except Exception as e:
        print(f"Error loading results: {e}")
        return 1

    if not results:
        print("No results found in JSON file")
        return 1

    print(f"Loaded {len(results)} results")

    # Create analyzer
    analyzer = WorkloadAnalyzer(results)

    # Handle commands
    if args.summary:
        analyzer.print_summary()

    if args.list:
        analyzer.list_available_charts()

    if args.chart:
        if not HAS_MATPLOTLIB and args.chart != 'list':
            print("\nError: matplotlib required for charting")
            print("Install with: pip install matplotlib")
            return 1

        if args.chart == 'all':
            analyzer.create_all_charts(args.output_dir)
            print(f"\nAll charts saved to: {args.output_dir}")
        else:
            if not args.output:
                print("Error: --output required for single chart")
                return 1

            if args.chart == 'mode-comparison':
                analyzer.create_mode_comparison_chart(str(args.output))
            elif args.chart == 'resource-sweep':
                analyzer.create_resource_sweep_chart(str(args.output))
            elif args.chart == 'rate-sweep':
                analyzer.create_rate_sweep_chart(str(args.output))
            elif args.chart == 'speedup':
                analyzer.create_speedup_chart(str(args.output))
            elif args.chart == 'philosopher-scaling':
                analyzer.create_philosopher_scaling_chart(str(args.output))
            elif args.chart == 'work-time':
                analyzer.create_work_time_chart(str(args.output))
            elif args.chart == 'baseline-nf-scaling':
                analyzer.create_baseline_nf_scaling_chart(str(args.output))
            elif args.chart == 'baseline-limiter-scaling':
                analyzer.create_baseline_limiter_scaling_chart(str(args.output))
            elif args.chart == 'baseline-overhead-heatmap':
                analyzer.create_baseline_overhead_heatmap(str(args.output))
            elif args.chart == 'chain-pipelining':
                analyzer.create_chain_pipelining_chart(str(args.output))
            elif args.chart == 'chain-speedup-by-rate':
                analyzer.create_chain_speedup_by_rate_chart(str(args.output))
            elif args.chart == 'siblings-graph-parallelism':
                analyzer.create_siblings_graph_parallelism_chart(str(args.output))
            elif args.chart == 'siblings-scaling-by-n':
                analyzer.create_siblings_scaling_by_n_chart(str(args.output))

    # If no action specified, show summary
    if not (args.summary or args.list or args.chart):
        analyzer.print_summary()
        print("\nTip: Use --list to see available charts, --summary for statistics")

    return 0


if __name__ == '__main__':
    sys.exit(main())
