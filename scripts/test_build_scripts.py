#!/usr/bin/env python3
"""Verify build orchestration using fake tools, without building or networking."""
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class BuildScripts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.log = self.path / "commands.jsonl"
        self.env = dict(os.environ, COMMAND_LOG=str(self.log))
        for name in ("BUILD_JOBS", "CMAKE_BUILD_PARALLEL_LEVEL"):
            self.env.pop(name, None)
        for tool in ("cmake", "ctest", "python-fake", "getconf", "cppcheck-fake"):
            script = self.path / tool
            script.write_text("#!/usr/bin/env python3\n"
                              "import json, os, sys\n"
                              "from pathlib import Path\n"
                              "with open(os.environ['COMMAND_LOG'], 'a') as f:\n"
                              "    f.write(json.dumps([Path(sys.argv[0]).name, *sys.argv[1:]]) + '\\n')\n"
                              "if os.environ.get('FAIL_TOOL') == Path(sys.argv[0]).name and sys.argv[1:] != ['--version']:\n"
                              "    sys.stderr.write('injected analyzer/test failure\\n'); sys.exit(9)\n"
                              "if Path(sys.argv[0]).name == 'getconf': print(3)\n")
            script.chmod(0o755)
        self.env["PATH"] = str(self.path) + os.pathsep + os.environ["PATH"]
        self.env["PYTHON"] = str(self.path / "python-fake")
        self.env["CPPCHECK"] = str(self.path / "cppcheck-fake")

    def jobs(self, **overrides):
        return subprocess.run(["bash", "-c", "set -euo pipefail; source scripts/build-common.sh; echo $build_jobs"],
                              cwd=ROOT, env=dict(self.env, **overrides), text=True, capture_output=True, timeout=5)

    def test_job_precedence(self):
        self.assertEqual(self.jobs().stdout, "3\n")
        self.assertEqual(self.jobs(CMAKE_BUILD_PARALLEL_LEVEL="5").stdout, "5\n")
        self.assertEqual(self.jobs(BUILD_JOBS="2", CMAKE_BUILD_PARALLEL_LEVEL="5").stdout, "2\n")

    def test_invalid_job_counts_fail_before_building(self):
        for value in ("0", "-1", "1.5", "auto", "2; touch unexpected", "01"):
            with self.subTest(value=value):
                result = self.jobs(BUILD_JOBS=value)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("positive integer", result.stderr)

    def test_coverage_resets_counters_and_runs_all_checks_before_reporting(self):
        build = self.path / "build with spaces"
        build.mkdir()
        (build / "stale.gcda").write_text("stale")
        (build / "keep.gcno").write_text("keep")
        result = subprocess.run(["bash", "scripts/coverage.sh", str(build)], cwd=ROOT,
                                env=dict(self.env, BUILD_JOBS="2", GCOV="gcov-14",
                                         COVERAGE_MIN_LINES="100", COVERAGE_MIN_BRANCHES="90"),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((build / "stale.gcda").exists())
        self.assertTrue((build / "keep.gcno").exists())
        commands = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual([command[0] for command in commands],
                         ["python-fake", "cmake", "cmake", "ctest", "python-fake", "python-fake"])
        self.assertIn("-DAGENT_ENABLE_COVERAGE=ON", commands[1])
        self.assertEqual(commands[2][-2:], ["--parallel", "2"])
        self.assertEqual(commands[4][1:3], ["scripts/check_protocol.py", str(build)])
        report = commands[5]
        for option, value in (("--fail-under-line", "100"), ("--fail-under-branch", "90"),
                              ("--gcov-executable", "gcov-14")):
            self.assertEqual(report[report.index(option) + 1], value)

    def test_failed_tests_prevent_reporting_success(self):
        (self.path / "build").mkdir()
        result = subprocess.run(["bash", "scripts/coverage.sh", str(self.path / "build")],
                                cwd=ROOT, env=dict(self.env, BUILD_JOBS="2", FAIL_TOOL="ctest"),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 9)
        commands = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual(commands[-1][0], "ctest")
        self.assertFalse(any("--print-summary" in command for command in commands))

    def test_static_analysis_covers_project_targets_and_uses_parallel_jobs(self):
        build = self.path / "analysis build with spaces"
        result = subprocess.run(["bash", "scripts/static-analysis.sh", str(build)], cwd=ROOT,
                                env=dict(self.env, BUILD_JOBS="3"), capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual([command[0] for command in commands],
                         ["cppcheck-fake", "cmake", "cppcheck-fake"])
        self.assertIn("-DBUILD_TESTING=ON", commands[1])
        self.assertIn("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", commands[1])
        analysis = commands[-1]
        for argument in (f"--project={build}/compile_commands.json",
                         "--file-filter=src/*", "--file-filter=tests/*",
                         f"--file-filter={ROOT}/src/*", f"--file-filter={ROOT}/tests/*",
                         "--std=c++23", "--enable=warning,performance,portability",
                         "--error-exitcode=1", "--suppress=*:*/_deps/*"):
            self.assertIn(argument, analysis)
        self.assertEqual(analysis[-2:], ["-j", "3"])
        self.assertEqual(analysis[analysis.index("-i") + 1], "_deps")
        self.assertTrue((build / "static-analysis/cppcheck.log").exists())

    def test_static_analysis_failure_is_not_hidden_by_tee(self):
        build = self.path / "analysis"
        result = subprocess.run(["bash", "scripts/static-analysis.sh", str(build)], cwd=ROOT,
                                env=dict(self.env, BUILD_JOBS="2", FAIL_TOOL="cppcheck-fake"),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 9, result.stderr)
        self.assertIn("injected analyzer/test failure",
                      (build / "static-analysis/cppcheck.log").read_text())

    def test_missing_analyzer_fails_before_configuring(self):
        result = subprocess.run(["bash", "scripts/static-analysis.sh"], cwd=ROOT,
                                env=dict(self.env, BUILD_JOBS="2", CPPCHECK=str(self.path / "missing")),
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn("requires Cppcheck", result.stderr)
        self.assertFalse(self.log.exists())

    @unittest.skipUnless(shutil.which("cppcheck"), "Cppcheck is not installed")
    def test_real_analyzer_rejects_a_project_defect(self):
        # The compile database contains only this temporary project fixture.
        # Keep its path inside src so the real source filters are exercised.
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", prefix="analysis_probe_",
                                         dir=ROOT / "src") as source:
            source.write("int main() { int* pointer = nullptr; return *pointer; }\n")
            source.flush()
            build = self.path / "defect"
            build.mkdir()
            (build / "compile_commands.json").write_text(json.dumps([{
                "directory": str(ROOT), "file": source.name,
                "arguments": ["c++", "-std=c++23", "-c", source.name]}]))
            result = subprocess.run(["bash", "scripts/static-analysis.sh", str(build)], cwd=ROOT,
                                    env=dict(self.env, BUILD_JOBS="2", CPPCHECK=shutil.which("cppcheck")),
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("nullPointer", (build / "static-analysis/cppcheck.log").read_text())


if __name__ == "__main__":
    unittest.main()
