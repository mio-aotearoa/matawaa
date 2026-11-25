"""Checks for the deployment actions that can change a watch's boot state."""

import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "deployWatch.py"
spec = importlib.util.spec_from_file_location("deploy_watch", SCRIPT)
deploy_watch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy_watch)


class DeployCopyTests(unittest.TestCase):
    def deploy(self, action):
        with (
            patch.object(deploy_watch, "build_asteroid_toplevel", return_value="/nix/store/system"),
            patch.object(deploy_watch, "check_ssh_connectivity"),
            patch.object(deploy_watch, "run_cmd") as run,
            patch.object(deploy_watch, "ssh_exec") as ssh,
        ):
            deploy_watch.deploy_copy(
                Path("/repo"), "fossil-hoki", "watch", 2222, "root",
                action, False, False,
            )
            return run.call_args, ssh.call_args

    def test_boot_stages_profile_and_uses_requested_ssh_port(self):
        copy, ssh = self.deploy("boot")
        self.assertIn("ssh://root@watch:2222", copy.args[0])
        self.assertIn("nix-env -p /nix/var/nix/profiles/system --set", ssh.args[3])
        self.assertIn("/bin/switch-to-configuration boot", ssh.args[3])

    def test_build_defaults_to_flake_even_when_worktree_exists(self):
        with (
            patch.object(Path, "is_dir", return_value=True),
            patch.object(deploy_watch, "run_build_cmd", return_value="/nix/store/system") as build,
        ):
            deploy_watch.build_asteroid_toplevel(Path("/repo"), "fossil-hoki")
            cmd = build.call_args.args[0]
            self.assertEqual(cmd[:2], ["nix", "build"])
            self.assertIn("/repo#nixosConfigurations.fossil-hoki.config.system.build.toplevel", cmd)

    def test_build_uses_worktree_only_when_explicitly_requested(self):
        with (
            patch.object(Path, "is_dir", return_value=True),
            patch.object(deploy_watch, "run_build_cmd", return_value="/nix/store/system") as build,
        ):
            deploy_watch.build_asteroid_toplevel(Path("/repo"), "fossil-hoki", use_worktree=True)
            cmd = build.call_args.args[0]
            self.assertEqual(cmd[0], "nix-build")
            self.assertEqual(cmd[1], "/repo/work/mobile-nixos/examples/asteroid")

    def test_test_action_neither_flashes_nor_changes_default_profile(self):
        _, ssh = self.deploy("test")
        self.assertIn("switch-to-configuration test", ssh.args[3])
        self.assertNotIn("nix-env", ssh.args[3])

    def test_dry_run_never_connects_or_activates(self):
        with (
            patch.object(deploy_watch, "build_asteroid_toplevel", return_value="/nix/store/system"),
            patch.object(deploy_watch, "check_ssh_connectivity") as connect,
            patch.object(deploy_watch, "run_cmd") as run,
            patch.object(deploy_watch, "ssh_exec") as ssh,
        ):
            deploy_watch.deploy_copy(
                Path("/repo"), "fossil-hoki", "watch", 2222, "root",
                "boot", True, True,
            )
            connect.assert_not_called()
            run.assert_not_called()
            ssh.assert_not_called()

    def test_sudo_wraps_the_entire_remote_command(self):
        with patch.object(deploy_watch, "run_cmd") as run:
            deploy_watch.ssh_exec(
                "watch", 2222, "ceres", "first && second", sudo=True
            )
            command = run.call_args.args[0]
            self.assertIn("-tt", command)
            self.assertEqual(command[-1], "sudo -- sh -c 'first && second'")


class DeployMainDefaultsTests(unittest.TestCase):
    def run_main(self, argv):
        with (
            patch("sys.argv", ["deployWatch.py", *argv]),
            patch.object(deploy_watch, "get_repo_root", return_value=Path("/repo")),
            patch.object(deploy_watch, "deploy_copy") as copy,
        ):
            deploy_watch.main()
            return copy.call_args.kwargs

    def test_defaults_to_boot_and_reboot_without_worktree(self):
        kwargs = self.run_main(["--target", "192.168.8.183"])
        self.assertEqual(kwargs["action"], "boot")
        self.assertTrue(kwargs["reboot"])
        self.assertFalse(kwargs["use_worktree"])

    def test_no_reboot_disables_automatic_reboot(self):
        kwargs = self.run_main(["--no-reboot"])
        self.assertEqual(kwargs["action"], "boot")
        self.assertFalse(kwargs["reboot"])

    def test_non_boot_actions_force_reboot_false(self):
        for action in ("switch", "test", "dry-activate"):
            kwargs = self.run_main(["--action", action, "--reboot"])
            self.assertEqual(kwargs["action"], action)
            self.assertFalse(kwargs["reboot"])


if __name__ == "__main__":
    unittest.main()
