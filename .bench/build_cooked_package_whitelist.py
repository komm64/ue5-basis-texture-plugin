import argparse
import json
import os
import re
import subprocess
import sys
from collections import defaultdict


DEFAULT_UNREALPAK = "E:/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealPak.exe"
ENTRY_RE = re.compile(
    r'Display: "(?P<path>[^"]+)" offset: \d+, size: (?P<size>\d+) bytes, .* compression: (?P<compression>[^.]+)\.'
)
COOKED_EXTENSIONS = {".uasset", ".uexp", ".ubulk", ".umap"}


def normalize_path(path):
    return path.replace("\\", "/")


def package_from_container_path(path, project_name):
    path = normalize_path(path)
    while path.startswith("../"):
        path = path[3:]

    project_prefix = f"{project_name}/Content/"
    engine_prefix = "Engine/Content/"
    ext = os.path.splitext(path)[1].lower()
    if ext not in COOKED_EXTENSIONS:
        return None

    stem = path[: -len(ext)]
    if stem.startswith(project_prefix):
        return "/Game/" + stem[len(project_prefix) :]
    if stem.startswith(engine_prefix):
        return "/Engine/" + stem[len(engine_prefix) :]
    return None


def iter_list_entries(list_path):
    with open(list_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            match = ENTRY_RE.search(line)
            if not match:
                continue
            yield {
                "path": normalize_path(match.group("path")),
                "size_bytes": int(match.group("size")),
                "compression": match.group("compression"),
            }


def run_unrealpak_list(unrealpak, container, output):
    with open(output, "w", encoding="utf-8") as f:
        result = subprocess.run(
            [unrealpak, container, "-List"],
            stdout=f,
            stderr=subprocess.STDOUT,
            text=True,
        )
    if result.returncode != 0:
        raise RuntimeError(f"UnrealPak -List failed with exit code {result.returncode}: {container}")


def build_whitelist(list_path, project_name):
    packages = {}
    extension_counts = defaultdict(int)
    extension_sizes = defaultdict(int)
    for entry in iter_list_entries(list_path):
        package = package_from_container_path(entry["path"], project_name)
        if not package:
            continue
        ext = os.path.splitext(entry["path"])[1].lower()
        record = packages.setdefault(
            package,
            {
                "size_bytes": 0,
                "files": [],
            },
        )
        record["size_bytes"] += entry["size_bytes"]
        record["files"].append(entry)
        extension_counts[ext] += 1
        extension_sizes[ext] += entry["size_bytes"]

    return {
        "project_name": project_name,
        "packages": sorted(packages.keys()),
        "package_sizes": packages,
        "summary": {
            "package_count": len(packages),
            "size_bytes": sum(record["size_bytes"] for record in packages.values()),
            "extension_counts": dict(sorted(extension_counts.items())),
            "extension_size_bytes": dict(sorted(extension_sizes.items())),
        },
    }


def main():
    parser = argparse.ArgumentParser(description="Build a UE cooked package whitelist from UnrealPak -List output.")
    parser.add_argument("--container", help=".pak or .utoc file to list with UnrealPak")
    parser.add_argument("--list-file", help="Existing UnrealPak -List output")
    parser.add_argument("--unrealpak", default=os.environ.get("UNREALPAK_EXE", DEFAULT_UNREALPAK))
    parser.add_argument("--project-name", default=os.environ.get("UE_PROJECT_NAME", "VirtualStudio"))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if not args.container and not args.list_file:
        parser.error("Either --container or --list-file is required.")

    list_path = args.list_file
    if not list_path:
        list_path = args.output + ".list.txt"
        run_unrealpak_list(args.unrealpak, args.container, list_path)

    whitelist = build_whitelist(list_path, args.project_name)
    whitelist["source"] = {
        "container": normalize_path(args.container) if args.container else None,
        "list_file": normalize_path(list_path),
    }

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(whitelist, f, indent=2, ensure_ascii=False)

    print(json.dumps(whitelist["summary"], indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
