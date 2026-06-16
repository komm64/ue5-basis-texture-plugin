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


def parse_list(path):
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            match = ENTRY_RE.search(line)
            if not match:
                continue
            rows.append(
                {
                    "path": match.group("path").replace("\\", "/"),
                    "size_bytes": int(match.group("size")),
                    "compression": match.group("compression"),
                }
            )
    return rows


def source_to_container_prefix(source_path, project_name):
    if source_path.startswith("/Game/"):
        return f"../../../{project_name}/Content/{source_path[len('/Game/'):]}"
    if source_path.startswith("/Engine/"):
        return f"../../../Engine/Content/{source_path[len('/Engine/'):]}"
    return None


def sum_entries_for_source(entries, source_path, project_name):
    prefix = source_to_container_prefix(source_path, project_name)
    if not prefix:
        return 0
    prefix = prefix.replace("\\", "/")
    return sum(row["size_bytes"] for row in entries if row["path"].startswith(prefix + "."))


def sum_entries_for_basis_asset(entries, destination_path, asset_name, project_name):
    game_destination = destination_path
    if game_destination.startswith("/Game/"):
        container_prefix = f"../../../{project_name}/Content/{game_destination[len('/Game/'):]}/{asset_name}"
    else:
        container_prefix = game_destination.rstrip("/") + "/" + asset_name
    return sum(row["size_bytes"] for row in entries if row["path"].startswith(container_prefix + "."))


def mb(value):
    return round(value / 1024 / 1024, 2)


def main():
    parser = argparse.ArgumentParser(description="Compare a Basis cook against a reference Standard cook.")
    parser.add_argument("--standard-container", required=True)
    parser.add_argument("--basis-container", required=True)
    parser.add_argument("--encoded-manifest", required=True)
    parser.add_argument("--project-name", default="VirtualStudio")
    parser.add_argument("--unrealpak", default=os.environ.get("UNREALPAK_EXE", DEFAULT_UNREALPAK))
    parser.add_argument("--workdir", default=".bench")
    parser.add_argument("--fail-on-unmatched", action="store_true")
    parser.add_argument("--output")
    args = parser.parse_args()

    os.makedirs(args.workdir, exist_ok=True)
    standard_list = os.path.join(args.workdir, "report_standard_container_list.txt")
    basis_list = os.path.join(args.workdir, "report_basis_container_list.txt")
    run_unrealpak_list(args.unrealpak, args.standard_container, standard_list)
    run_unrealpak_list(args.unrealpak, args.basis_container, basis_list)

    standard_entries = parse_list(standard_list)
    basis_entries = parse_list(basis_list)
    with open(args.encoded_manifest, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    destination_path = manifest["destination_path"]
    rows = []
    by_semantic = defaultdict(lambda: {"count": 0, "standard": 0, "basis": 0, "unmatched_basis": 0})
    for source_path, info in sorted(manifest["textures"].items()):
        standard_size = sum_entries_for_source(standard_entries, source_path, args.project_name)
        basis_size = sum_entries_for_basis_asset(basis_entries, destination_path, info["asset_name"], args.project_name)
        unmatched = standard_size == 0 and basis_size > 0
        semantic = info["semantic"]
        by_semantic[semantic]["count"] += 1
        by_semantic[semantic]["standard"] += standard_size
        by_semantic[semantic]["basis"] += basis_size
        if unmatched:
            by_semantic[semantic]["unmatched_basis"] += basis_size
        rows.append(
            {
                "source_texture": source_path,
                "semantic": semantic,
                "standard_bytes": standard_size,
                "basis_bytes": basis_size,
                "unmatched": unmatched,
            }
        )

    total_standard = sum(row["standard_bytes"] for row in rows)
    total_basis = sum(row["basis_bytes"] for row in rows)
    unmatched = [row for row in rows if row["unmatched"]]
    report = {
        "summary": {
            "texture_count": len(rows),
            "matched_count": len(rows) - len(unmatched),
            "unmatched_count": len(unmatched),
            "standard_mb": mb(total_standard),
            "basis_mb": mb(total_basis),
            "delta_mb": mb(total_basis - total_standard),
            "matched_standard_mb": mb(sum(row["standard_bytes"] for row in rows if not row["unmatched"])),
            "matched_basis_mb": mb(sum(row["basis_bytes"] for row in rows if not row["unmatched"])),
            "unmatched_basis_mb": mb(sum(row["basis_bytes"] for row in unmatched)),
        },
        "by_semantic": {
            key: {
                "count": value["count"],
                "standard_mb": mb(value["standard"]),
                "basis_mb": mb(value["basis"]),
                "unmatched_basis_mb": mb(value["unmatched_basis"]),
            }
            for key, value in sorted(by_semantic.items())
        },
        "unmatched": [
            {
                "source_texture": row["source_texture"],
                "semantic": row["semantic"],
                "basis_mb": mb(row["basis_bytes"]),
            }
            for row in sorted(unmatched, key=lambda item: item["basis_bytes"], reverse=True)
        ],
    }

    output = args.output or os.path.join(args.workdir, "basis_cook_delta_report.json")
    with open(output, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    print(json.dumps(report["summary"], indent=2))
    if unmatched:
        print("Largest unmatched Basis-only textures:")
        print(json.dumps(report["unmatched"][:10], indent=2, ensure_ascii=False))
    if args.fail_on_unmatched and unmatched:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
