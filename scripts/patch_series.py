"""Shared patch-series helpers for the apply and rebuild commands."""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path


CATEGORY_PREFIX = re.compile(r"^\d{2}-")
DEFAULT_COMMITTER_NAME = "Matawaa Patch Applier"
DEFAULT_COMMITTER_EMAIL = "matawaa@localhost"


def git_committer_args(repo: Path) -> list[str]:
    """Return fallback git -c identity flags when no committer identity is configured."""
    args: list[str] = []
    for env_var, config_key, default_value in (
        ("GIT_COMMITTER_NAME", "user.name", DEFAULT_COMMITTER_NAME),
        ("GIT_COMMITTER_EMAIL", "user.email", DEFAULT_COMMITTER_EMAIL),
    ):
        if os.environ.get(env_var, "").strip():
            continue
        result = subprocess.run(
            ["git", "config", "--get", config_key],
            cwd=repo,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode != 0 or not result.stdout.strip():
            args.extend(["-c", f"{config_key}={default_value}"])
    return args


def discover_categories(patches: Path) -> list[str]:
    """Discover category folders; their numeric prefixes define apply order."""
    if not patches.is_dir():
        raise ValueError(f"Patch directory is missing: {patches}")
    categories = sorted(
        entry.name
        for entry in patches.iterdir()
        if entry.is_dir() and not entry.name.startswith(".")
    )
    if not categories:
        raise ValueError(f"No patch category folders found in {patches}")
    invalid = [name for name in categories if not CATEGORY_PREFIX.match(name)]
    if invalid:
        raise ValueError(
            "Patch category folders must start with a two-digit numeric ordering prefix: "
            + ", ".join(invalid)
        )
    return categories


def patches_in_order(patches: Path, categories: list[str]) -> list[tuple[str, Path]]:
    """Return patch files in category order and lexical filename order."""
    return [
        (category, patch)
        for category in categories
        for patch in sorted((patches / category).glob("*.patch"))
    ]
