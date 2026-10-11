#!/usr/bin/env python3
# MIT License
# Copyright (c) 2026 OlJohnny
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Alternate three benchmark executables, save every timing and compare packed RGBA.

Usage: benchmark-color-conversion.py MASTER PORTABLE SIMD --output results
       [--file sample.heic ...] [--passes 12]

Executables are built from tests/color_conversion_benchmark.cc. Master needs the
same benchmark target added, with no conversion changes. A directory containing
an archived executable may also contain its shared library. No third-party Python
packages, Windows APIs or private sample files are required for synthetic inputs.
"""

import argparse
import csv
import hashlib
import io
import os
from pathlib import Path
import statistics
import subprocess


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def save_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("master", type=Path)
    parser.add_argument("portable", type=Path)
    parser.add_argument("simd", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--file", type=Path, action="append", default=[])
    parser.add_argument("--passes", type=int, default=12)
    args = parser.parse_args()
    if args.passes < 6 or args.passes % 3:
        parser.error("passes must be a multiple of three and at least six")
    if len({file.name for file in args.file}) != len(args.file):
        parser.error("sample filenames must be distinct")
    args.output.mkdir(parents=True, exist_ok=True)
    pixel_directory = args.output / "pixels"
    pixel_directory.mkdir(exist_ok=True)
    executables = {name: getattr(args, name).resolve() for name in ("master", "portable", "simd")}
    for executable in executables.values():
        if not executable.is_file():
            parser.error(f"executable does not exist: {executable}")
    cases = [("synthetic", None)]
    for file in args.file:
        cases += [("full", file.resolve()), ("convert", file.resolve())]
    rows, hashes = [], []
    expected_hashes = {}
    variants = list(executables)
    with (args.output / "metadata.log").open("w", encoding="utf-8") as metadata:
        for case_index, (mode, filename) in enumerate(cases):
            for block in range(3):
                order = variants[block:] + variants[:block]
                if case_index % 2:
                    order.reverse()
                for variant in order:
                    executable = executables[variant]
                    command = [str(executable), mode]
                    if filename is not None:
                        command.append(str(filename))
                    command.append(str(args.passes // 3))
                    if block == 0:
                        command.append(str(pixel_directory) if filename is None else str(pixel_directory / "file.rgba"))
                    environment = dict(os.environ)
                    environment["LD_LIBRARY_PATH"] = str(executable.parent) + os.pathsep + environment.get("LD_LIBRARY_PATH", "")
                    result = subprocess.run(command, check=True, capture_output=True, text=True, env=environment)
                    metadata.write(f"{variant} {mode} {filename or 'generated'} block {block}\n{result.stderr}\n")
                    metadata.flush()
                    trial_rows = list(csv.DictReader(io.StringIO(result.stdout)))
                    samples = set()
                    for trial in trial_rows:
                        samples.add(trial["sample"])
                        rows.append(dict(sample=trial["sample"], mode=mode, variant=variant, block=block,
                                         round=trial["round"], milliseconds=float(trial["milliseconds"])))
                    save_csv(args.output / "timings.csv", rows)
                    if block == 0:
                        for sample in sorted(samples):
                            path = pixel_directory / (sample + ".rgba" if filename is None else "file.rgba")
                            digest = sha256(path)
                            key = (sample,)
                            if key in expected_hashes and expected_hashes[key] != digest:
                                raise RuntimeError(f"RGBA mismatch for {sample} {variant} {mode}")
                            expected_hashes[key] = digest
                            hashes.append(dict(sample=sample, mode=mode, variant=variant, sha256=digest))
                            path.unlink()
                        save_csv(args.output / "hashes.csv", hashes)
                print(f"{filename.name if filename else 'generated'} {mode} block {block + 1}/3 complete", flush=True)
    summaries = []
    for sample, mode in dict.fromkeys((row["sample"], row["mode"]) for row in rows):
        for variant in variants:
            values = [row["milliseconds"] for row in rows if
                      (row["sample"], row["mode"], row["variant"]) == (sample, mode, variant)]
            if len(values) != args.passes:
                raise RuntimeError(f"Missing timings for {sample} {mode} {variant}")
            median, sd = statistics.median(values), statistics.stdev(values)
            summaries.append(dict(sample=sample, mode=mode, variant=variant, n=len(values),
                                  median_ms=median, sample_sd_ms=sd))
            print(f"{sample} {mode} {variant}: median {median:.3f} ms, sample SD {sd:.3f} ms, n={len(values)}")
    save_csv(args.output / "statistics.csv", summaries)
    print("All saved RGBA outputs match across variants and full/convert modes.")


if __name__ == "__main__":
    main()
