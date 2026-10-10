#!/usr/bin/env python3
"""Check scan coverage, failure handling, inventory drift and real secret detection."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
SCANNER = os.environ.get("TRIVY") or shutil.which("trivy")
if not SCANNER and (ROOT / ".tools/trivy/trivy").is_file():
    SCANNER = str(ROOT / ".tools/trivy/trivy")
spec = importlib.util.spec_from_file_location("inventory", ROOT / "scripts/security_inventory.py")
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


class SecurityInventory(unittest.TestCase):
    def test_all_fetchcontent_pins_and_hashes_are_inventoried(self):
        data = inventory.dependency_sbom((ROOT / "CMakeLists.txt").read_text())
        packages = {p["name"]: p for p in data["components"]}
        self.assertEqual(set(packages), {"nlohmann_json", "catch2"})
        for name, package in packages.items():
            self.assertEqual(package["purl"], f'pkg:conan/{name}@{package["version"]}')
            self.assertEqual(len(package["externalReferences"][0]["hashes"][0]["content"]), 64)
        changed = (ROOT / "CMakeLists.txt").read_text().replace("v3.12.0", "v99.0.1")
        self.assertEqual(inventory.dependency_sbom(changed)["components"][0]["version"], "99.0.1")

    def test_unknown_or_unpinned_dependency_fails_closed(self):
        source = (ROOT / "CMakeLists.txt").read_text()
        for changed in (source.replace("nlohmann_json\n", "unexpected\n"),
                        source.replace("URL_HASH SHA256=", "UNPINNED "), ""):
            with self.subTest(source=changed[:30]), self.assertRaises(ValueError):
                inventory.dependency_sbom(changed)

    def test_empty_scan_results_are_rejected_as_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'dependencies.cdx.json').write_text(json.dumps(
                inventory.dependency_sbom((ROOT / 'CMakeLists.txt').read_text())))
            (root / 'dependencies.json').write_text('{"Results": []}')
            with self.assertRaisesRegex(ValueError, 'omitted FetchContent'):
                inventory.validate_reports(root)

    def test_report_validation_requires_supported_os_and_every_inventory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sbom = inventory.dependency_sbom((ROOT / 'CMakeLists.txt').read_text())
            (root / 'dependencies.cdx.json').write_text(json.dumps(sbom))
            (root / 'dependencies.json').write_text(json.dumps({'Results': [{'Packages': [
                {'Name': c['name'], 'Version': c['version']} for c in sbom['components']]}]}))
            (root / 'runtime-packages.json').write_text('["libexample:amd64"]')
            (root / 'runtime.json').write_text('{"Metadata": {"OS": {"Family": "none"}}}')
            with self.assertRaisesRegex(ValueError, 'supported, maintained'):
                inventory.validate_reports(root)
            (root / 'runtime.json').write_text('{"Metadata": {"OS": {"Family": "ubuntu"}}, "Results": []}')
            with self.assertRaisesRegex(ValueError, 'omitted linked'):
                inventory.validate_reports(root)
            (root / 'runtime.json').write_text(json.dumps({'Metadata': {'OS': {'Family': 'ubuntu'}},
                'Results': [{'Packages': [{'Name': 'libexample'}]}]}))
            (root / 'repository.json').write_text('{"Results": []}')
            with self.assertRaisesRegex(ValueError, 'coverage requirements'):
                inventory.validate_reports(root)
            (root / 'repository.json').write_text('{"Results": [{"Packages": [{"Name": "gcovr"}]}]}')
            (root / 'licenses.json').write_text('{}')
            inventory.validate_reports(root)

    def test_unowned_runtime_library_fails_instead_of_disappearing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "agent_cli").touch()
            library = root / "unowned.so"
            library.touch()
            def command(*args):
                if args[0] == "ldd":
                    return f"libcustom.so => {library} (0x1234)\n"
                raise subprocess.CalledProcessError(1, args)
            with patch.object(inventory, "command", side_effect=command), self.assertRaises(ValueError):
                inventory.runtime_rootfs(root, root / "staged")

    def test_transitive_libraries_and_loader_are_staged_with_distro_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("agent_cli", "agentd", "liba.so", "loader.so"):
                (root / name).touch()
            def command(*args):
                if args[0] == "ldd":
                    return f"liba => {root}/liba.so (0x1234)\n {root}/loader.so (0x5678)\n"
                if args[1] == "--search":
                    name = "liba:amd64" if args[2].endswith("liba.so") else "loader:amd64"
                    return f"{name}: {args[2]}\n"
                return f"Package: {args[2]}\nStatus: install ok installed\nVersion: 1.0\n"
            with patch.object(inventory, "command", side_effect=command):
                packages = inventory.runtime_rootfs(root, root / "staged")
            self.assertEqual(packages, ["liba:amd64", "loader:amd64"])
            self.assertIn("Package: loader:amd64", (root / "staged/var/lib/dpkg/status").read_text())
            self.assertTrue((root / "staged/etc/os-release").is_file())


class SecurityScripts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.log = self.path / "commands.jsonl"
        tool = self.path / "fake-tool"
        tool.write_text("#!/usr/bin/env python3\n"
                        "import json, os, sys\nfrom pathlib import Path\n"
                        "with open(os.environ['COMMAND_LOG'], 'a') as log:\n"
                        "    log.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                        "if '--output' in sys.argv:\n"
                        "    Path(sys.argv[sys.argv.index('--output')+1]).write_text('{}')\n"
                        "if os.environ.get('FAIL_SCAN') == sys.argv[1]: sys.exit(7)\n")
        tool.chmod(0o755)
        self.env = dict(os.environ, TRIVY=str(tool), SECURITY_PYTHON=str(tool),
                        COMMAND_LOG=str(self.log))

    def run_scan(self, **env):
        return subprocess.run(["bash", "scripts/security-check.sh", str(self.path / "build with spaces")],
                              cwd=ROOT, env=dict(self.env, **env), text=True,
                              capture_output=True, timeout=5)

    def test_all_scans_are_required_and_security_reports_are_private(self):
        result = self.run_scan()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual([c[0] for c in calls],
                         ["--version", "scripts/security_inventory.py", "fs", "sbom", "rootfs", "fs",
                          "scripts/security_inventory.py"])
        self.assertEqual(calls[2][calls[2].index('--scanners')+1], 'vuln,misconfig,secret')
        for call in calls[2:5]:
            self.assertIn('UNKNOWN,LOW,MEDIUM,HIGH,CRITICAL', call)
            self.assertEqual(call[call.index('--exit-code')+1], '1')
            self.assertNotIn('--ignore-unfixed', call)
        self.assertIn('--license-full', calls[-2])
        self.assertEqual(calls[-1][1], '--validate')
        self.assertIn('pip:requirements-.*\\.txt', calls[2])
        reports = self.path / 'build with spaces/security'
        self.assertEqual(reports.stat().st_mode & 0o777, 0o700)
        self.assertEqual((reports / 'repository.json').stat().st_mode & 0o777, 0o600)

    def test_a_failed_scan_does_not_skip_other_scans_or_report_success(self):
        result = self.run_scan(FAIL_SCAN='sbom')
        self.assertEqual(result.returncode, 1)
        self.assertIn('dependencies scan failed', result.stderr)
        self.assertTrue((self.path / 'build with spaces/security/licenses.json').exists())

    def test_missing_scanner_fails_before_inventory(self):
        result = self.run_scan(TRIVY=str(self.path / 'missing'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('require Trivy', result.stderr)
        self.assertFalse(self.log.exists())

    @unittest.skipUnless(SCANNER, 'Trivy is not installed')
    def test_real_secret_scanner_rejects_a_synthetic_private_key(self):
        # Synthetic bytes, not a credential. Only the presence of a detection is asserted.
        scanner = SCANNER
        (self.path / 'fixture.pem').write_text(
            '-----BEGIN RSA PRIVATE KEY-----\n' + 'A' * 128 + '\n-----END RSA PRIVATE KEY-----\n')
        report = self.path / 'report.json'
        result = subprocess.run([scanner, 'fs', '--scanners', 'secret', '--exit-code', '1',
                                 '--format', 'json', '--output', str(report), str(self.path)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1)
        self.assertTrue(any(r.get('Secrets') for r in json.loads(report.read_text()).get('Results', [])))


if __name__ == '__main__':
    unittest.main()
