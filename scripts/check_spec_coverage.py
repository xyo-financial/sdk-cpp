#!/usr/bin/env python3
"""Verify the hand-written XYO C++ client still covers the OpenAPI specification.

The C++ SDK is hand-written on top of cpr rather than machine-generated, so
nothing mechanically forces it to stay in step with `xyo-financial/specs`. This
script is that guard: it fails when the spec declares an operation the client
never issues, which is the signal a maintainer needs to implement it by hand.

Scope and limits: this checks request paths only. It deliberately does not try
to verify HTTP methods, schemas or field names, because matching those against
hand-written code produces false positives that train maintainers to ignore the
check. Schema drift is caught by the test suite in tests/client_test.cpp.

Usage:
    python3 scripts/check_spec_coverage.py path/to/openapi.yml
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CLIENT_SOURCE = REPO_ROOT / "src" / "client.cpp"


def literal_for(path: str) -> str:
    """Return the string literal the client is expected to contain for a path.

    Templated paths such as /v1/.../status/{id} are built by concatenation, so
    only the fixed prefix ahead of the first placeholder appears as a literal.
    """
    prefix, sep, _ = path.partition("{")
    return f'"{prefix}' if sep else f'"{path}"'


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {Path(argv[0]).name} <openapi.yml>", file=sys.stderr)
        return 2

    try:
        import yaml
    except ImportError:
        print("error: PyYAML is required (pip install pyyaml)", file=sys.stderr)
        return 2

    spec_path = Path(argv[1])
    if not spec_path.is_file():
        print(f"error: specification not found: {spec_path}", file=sys.stderr)
        return 2

    with spec_path.open(encoding="utf-8") as handle:
        spec = yaml.safe_load(handle)

    paths = (spec or {}).get("paths") or {}
    if not paths:
        print("error: specification declares no paths", file=sys.stderr)
        return 2

    source = CLIENT_SOURCE.read_text(encoding="utf-8")

    spec_version = (spec.get("info") or {}).get("version", "unknown")
    print(f"Specification version: {spec_version}")
    print(f"Checking {len(paths)} path(s) against {CLIENT_SOURCE.relative_to(REPO_ROOT)}\n")

    missing = []
    for path in sorted(paths):
        methods = sorted(
            method.upper()
            for method in (paths[path] or {})
            if method.lower() in {"get", "put", "post", "delete", "patch", "head", "options"}
        )
        covered = literal_for(path) in source
        print(f"  [{'ok' if covered else 'MISSING'}] {path}  ({', '.join(methods) or 'no methods'})")
        if not covered:
            missing.append(path)

    if missing:
        print(
            f"\n{len(missing)} specification path(s) are not implemented by the "
            f"hand-written client:",
            file=sys.stderr,
        )
        for path in missing:
            print(f"  - {path}", file=sys.stderr)
        print(
            "\nImplement them in src/client.cpp and expose them via "
            "include/xyo/client.hpp, then add coverage in tests/client_test.cpp.",
            file=sys.stderr,
        )
        return 1

    print("\nAll specification paths are implemented by the client.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
