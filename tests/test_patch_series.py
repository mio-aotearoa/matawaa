"""Checks for patch-series metadata and CI-safe git identity handling."""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import patch_series  # noqa: E402


class PatchSeriesTests(unittest.TestCase):
    def test_git_committer_args_falls_back_when_identity_is_unset(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            env = {
                k: v
                for k, v in os.environ.items()
                if k not in ("GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL")
            }
            env["GIT_CONFIG_GLOBAL"] = "/dev/null"
            env["GIT_CONFIG_SYSTEM"] = "/dev/null"
            with patch.dict(os.environ, env, clear=True):
                args = patch_series.git_committer_args(repo)
            self.assertEqual(
                args,
                [
                    "-c",
                    f"user.name={patch_series.DEFAULT_COMMITTER_NAME}",
                    "-c",
                    f"user.email={patch_series.DEFAULT_COMMITTER_EMAIL}",
                ],
            )

    def test_git_committer_args_respects_repo_config_and_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            subprocess.run(["git", "config", "user.name", "Local User"], cwd=repo, check=True)
            env = {
                k: v
                for k, v in os.environ.items()
                if k not in ("GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL")
            }
            env["GIT_CONFIG_GLOBAL"] = "/dev/null"
            env["GIT_CONFIG_SYSTEM"] = "/dev/null"
            env["GIT_COMMITTER_EMAIL"] = "env@example.com"
            with patch.dict(os.environ, env, clear=True):
                self.assertEqual(patch_series.git_committer_args(repo), [])

    def test_all_patches_have_mbox_headers(self):
        patches_dir = REPO_ROOT / "patches"
        categories = patch_series.discover_categories(patches_dir)
        ordered = patch_series.patches_in_order(patches_dir, categories)
        self.assertGreater(len(ordered), 0)
        from_re = re.compile(r"^From:\s+\S.*<\S+@\S+>$")
        for category, patch_path in ordered:
            head = patch_path.read_text(errors="replace").splitlines()[:10]
            has_from = any(from_re.match(line) for line in head)
            has_date = any(line.startswith("Date: ") and line[6:].strip() for line in head)
            has_subject = any(line.startswith("Subject: ") and line[9:].strip() for line in head)
            self.assertTrue(has_from, f"Missing valid From: header in {category}/{patch_path.name}")
            self.assertTrue(has_date, f"Missing Date: header in {category}/{patch_path.name}")
            self.assertTrue(has_subject, f"Missing Subject: header in {category}/{patch_path.name}")


if __name__ == "__main__":
    unittest.main()
