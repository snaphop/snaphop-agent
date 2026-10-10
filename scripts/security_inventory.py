#!/usr/bin/env python3
"""Inventory actual FetchContent pins and the built Linux executables' OS libraries."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


LICENSES = {"nlohmann_json": "MIT", "Catch2": "BSL-1.0"}


def dependency_sbom(cmake):
    components = []
    blocks = re.findall(r"FetchContent_Declare\(\s*(\w+)\s+(.*?)\n\s*\)", cmake, re.S)
    if not blocks:
        raise ValueError("No FetchContent dependencies found")
    for name, declaration in blocks:
        if name not in LICENSES:
            raise ValueError(f"Add an explicit security inventory mapping for dependency {name}")
        url = re.search(r"\bURL\s+(https://\S+/refs/tags/v([^\s]+))", declaration)
        digest = re.search(r"\bURL_HASH SHA256=([0-9a-f]{64})\b", declaration)
        if not url or not digest:
            raise ValueError(f"Dependency {name} must have a versioned URL and SHA256")
        package = name.lower()
        reference = f"pkg:conan/{package}@{url[2]}"
        components.append({"type": "library", "bom-ref": reference, "name": package,
                           "version": url[2], "purl": reference,
                           "licenses": [{"license": {"id": LICENSES[name]}}],
                           "externalReferences": [{"type": "distribution", "url": url[1],
                                                   "hashes": [{"alg": "SHA-256", "content": digest[1]}]}]})
    return {"bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1,
            "metadata": {"component": {"type": "application", "name": "SnapHop Agent"}},
            "components": components}


def command(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.PIPE)


def runtime_rootfs(build, rootfs):
    # Stage only distro metadata, never binaries, environment or workspace data.
    # ldd is used only on our freshly built, trusted executables.
    packages = set()
    for name in ("agent_cli", "agentd"):
        binary = (build / name).resolve(strict=True)
        output = command("ldd", str(binary))
        if "not found" in output:
            raise ValueError(f"Unresolved runtime library for {name}")
        libraries = re.findall(r"(?:=>\s+|^\s*)(/\S+)", output, re.M)
        if not libraries:
            raise ValueError(f"No dynamic library inventory for {name}")
        for library in libraries:
            resolved = str(Path(library).resolve(strict=True))
            candidates = [resolved, library]
            if resolved.startswith("/usr/"):
                candidates.append(resolved[4:])  # dpkg may record paths before usr-merge.
            for candidate in dict.fromkeys(candidates):
                try:
                    owners = command("dpkg-query", "--search", candidate)
                except subprocess.CalledProcessError:
                    continue
                matched = False
                for row in owners.splitlines():
                    owner, _, path = row.rpartition(": ")
                    if path == candidate:
                        packages.update(owner.split(", "))
                        matched = True
                if matched:
                    break
            else:
                raise ValueError(f"No distro package owns runtime library {library}")
    records = [command("dpkg-query", "--status", package).strip() for package in sorted(packages)]
    status = rootfs / "var/lib/dpkg/status"
    status.parent.mkdir(parents=True, exist_ok=True)
    status.write_text("\n\n".join(records) + "\n")
    etc = rootfs / "etc"
    etc.mkdir(parents=True, exist_ok=True)
    for name in ("os-release", "lsb-release", "debian_version"):
        (etc / name).unlink(missing_ok=True)
    shutil.copyfile("/etc/os-release", etc / "os-release")
    for name in ("lsb-release", "debian_version"):
        source = Path("/etc") / name
        if source.is_file():
            shutil.copyfile(source, etc / name)
    return sorted(packages)


def validate_reports(report):
    sbom = json.loads((report / "dependencies.cdx.json").read_text())
    dependencies = json.loads((report / "dependencies.json").read_text())
    expected = {(p["name"], p["version"]) for p in sbom["components"]}
    actual = {(p["Name"], p["Version"]) for r in dependencies.get("Results", [])
              for p in r.get("Packages", [])}
    if not expected <= actual:
        raise ValueError("Trivy omitted FetchContent packages from vulnerability scanning")
    runtime = json.loads((report / "runtime.json").read_text())
    os_info = runtime.get("Metadata", {}).get("OS", {})
    if os_info.get("Family") not in ("ubuntu", "debian") or os_info.get("EOSL"):
        raise ValueError("Trivy did not detect a supported, maintained Debian/Ubuntu runtime OS")
    expected = {p.split(":")[0] for p in json.loads((report / "runtime-packages.json").read_text())}
    actual = {p["Name"] for r in runtime.get("Results", []) for p in r.get("Packages", [])}
    if not expected or not expected <= actual:
        raise ValueError("Trivy omitted linked distro packages from vulnerability scanning")
    repository = json.loads((report / "repository.json").read_text())
    actual = {p["Name"] for r in repository.get("Results", []) for p in r.get("Packages", [])}
    if "gcovr" not in actual:
        raise ValueError("Trivy omitted optional coverage requirements from vulnerability scanning")
    json.loads((report / "licenses.json").read_text())


def summarize_report(path):
    # Public diagnostics contain counts only, never identifiers, paths, keys,
    # package names or report-controlled text. Raw evidence remains private.
    report = json.loads(path.read_text())
    totals = {severity: 0 for severity in ("CRITICAL", "HIGH", "MEDIUM", "LOW", "UNKNOWN")}
    vulnerabilities = []
    secrets = misconfigurations = 0
    for result in report.get("Results", []):
        vulnerabilities.extend(result.get("Vulnerabilities", []))
        secrets += len(result.get("Secrets", []))
        misconfigurations += len(result.get("Misconfigurations", []))
    for vulnerability in vulnerabilities:
        severity = vulnerability.get("Severity", "UNKNOWN")
        totals[severity if severity in totals else "UNKNOWN"] += 1
    return {
        "vulnerabilities": totals,
        "with_listed_fix": sum(bool(v.get("FixedVersion")) for v in vulnerabilities),
        "without_listed_fix": sum(not bool(v.get("FixedVersion")) for v in vulnerabilities),
        "secrets": secrets,
        "misconfigurations": misconfigurations,
    }


def report_blocks_verification(path):
    report = json.loads(path.read_text())
    if not isinstance(report, dict) or report.get("SchemaVersion") != 2:
        raise ValueError("Missing or unsupported Trivy report schema")
    results = report.get("Results", [])
    if not isinstance(results, list) or any(not isinstance(result, dict) for result in results):
        raise ValueError("Invalid Trivy report results")
    for result in results:
        for field in ("Vulnerabilities", "Secrets", "Misconfigurations"):
            findings = result.get(field, [])
            if not isinstance(findings, list) or any(not isinstance(item, dict) for item in findings):
                raise ValueError("Invalid Trivy finding collection")
    summary = summarize_report(path)
    if sum(summary["vulnerabilities"].values()) or summary["secrets"] or summary["misconfigurations"]:
        print("Trivy finding counts: " + json.dumps(summary, sort_keys=True))
    return bool(summary["vulnerabilities"]["HIGH"] or summary["vulnerabilities"]["CRITICAL"]
                or summary["secrets"] or summary["misconfigurations"])


def main():
    if len(sys.argv) != 3:
        raise ValueError("Usage: security_inventory.py BUILD_DIR REPORT_DIR")
    if sys.argv[1] == "--gate":
        if report_blocks_verification(Path(sys.argv[2])):
            sys.exit(1)
        return
    if sys.argv[1] == "--summarize":
        print("Trivy finding counts: " + json.dumps(summarize_report(Path(sys.argv[2])), sort_keys=True))
        return
    if sys.argv[1] == "--validate":
        validate_reports(Path(sys.argv[2]))
        return
    build, report = map(Path, sys.argv[1:])
    sbom = dependency_sbom(Path("CMakeLists.txt").read_text())
    report.mkdir(parents=True, exist_ok=True)
    (report / "dependencies.cdx.json").write_text(json.dumps(sbom, indent=2) + "\n")
    packages = runtime_rootfs(build, report / "runtime-rootfs")
    (report / "runtime-packages.json").write_text(json.dumps(packages, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Security inventory failed: {error}", file=sys.stderr)
        sys.exit(1)
