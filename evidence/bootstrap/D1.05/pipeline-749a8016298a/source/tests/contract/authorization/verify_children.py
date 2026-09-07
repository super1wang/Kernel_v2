"""D1.04固定主体中的真实编译/安装控制；不从expected生成发现结果。"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import save_json, sha_file
from tools.evidence.process import execute


CONTROLS = {
    "authentication_source": (
        """void accepted(SessionAuthority& session, const CallerDescription& request) {
  auto caller = session.verify(request);
  auto authority = session.callers();
  auto grant = authority->authenticate(request);
  if (caller) { (void)(*caller)->view(); (void)(*caller)->authority(); }
  if (grant) { (void)authority->validate(**grant); }
}
""",
        {
            "construct_caller": (
                "void rejected() { VerifiedCaller forged{}; }",
                r"C2512|C2248|C2661|C2660|C2280",
            ),
            "copy_session": (
                "void rejected(const SessionAuthority& source) { SessionAuthority copy(source); }",
                r"C2280|C2248",
            ),
        },
    ),
    "permit_origin_binding": (
        """void accepted(SessionAuthority& session, const VerifiedCaller& caller,
              const ActionRequest& request) {
  auto prepared = session.prepare(caller, request);
  if (prepared) {
    auto binding = (*prepared)->current_expected_binding();
    auto permit = (*prepared)->issue();
    if (binding && permit) { (void)(*prepared)->consume(**permit, *binding); }
  }
}
""",
        {
            "construct_action": (
                "void rejected() { ActionAuthorization forged{}; }",
                r"C2512|C2248|C2661|C2660|C2280",
            ),
            "extract_grant": (
                "void rejected(const VerifiedCaller& caller) { caller.grant(); }",
                r"C2039|C2248",
            ),
        },
    ),
    "group_substitution": (
        """struct ReadOnlyDigest final : TrustedGroupDigestPort {
  Result<ContractDigest> fingerprint(const GroupSnapshot& group) override {
    const OperationSelector& envelope = group.envelope();
    const auto anchor = group.anchor_target();
    const auto members = group.members();
    (void)envelope; (void)anchor; (void)members;
    return ContractDigest{};
  }
};
""",
        {
            "modify_members": (
                "void rejected(const GroupSnapshot& group) { group.members()[0].targets.clear(); }",
                r"C2662|C3892|C2678|C2679",
            ),
            "construct_group": (
                "void rejected() { GroupSnapshot forged{}; }",
                r"C2512|C2248|C2661|C2660|C2280",
            ),
        },
    ),
    "transmission_start_arbitration": (
        """struct ReservedSlot final : TransmissionReservation {
  explicit ReservedSlot(std::size_t size) : size_(size) {}
  std::size_t capacity() const noexcept override { return size_; }
private:
  std::size_t size_;
};
struct CompilableSink final : TransmissionStartPort {
  Result<std::unique_ptr<TransmissionReservation>> reserve(std::size_t size) override {
    if (!size || size > received.size())
      return make_unexpected(ock::contracts::error(ContractsErrc::BudgetExceeded));
    auto slot = std::make_unique<ReservedSlot>(size);
    issued = slot.get();
    return std::unique_ptr<TransmissionReservation>(std::move(slot));
  }
  StartResult start_now(const PreparedTransmission& transmission,
                        TransmissionReservation& reservation) noexcept override {
    auto bytes = transmission.bytes();
    (void)transmission.binding(); (void)transmission.projection();
    if (&reservation != issued || bytes.empty() || bytes.size() > reservation.capacity())
      return StartResult::NotStarted;
    for (std::size_t i = 0; i < bytes.size(); ++i) received[i] = bytes[i];
    issued = nullptr;
    return StartResult::Started;
  }
  std::array<std::byte, 1024> received{};
  ReservedSlot* issued = nullptr;
};
""",
        {
            "construct_transmission": (
                "void rejected(TransmissionBinding binding) { PreparedTransmission forged({}, binding, {}); }",
                r"C2248|C2661|C2660|C2280",
            ),
            "modify_bytes": (
                "void rejected(const PreparedTransmission& frame) { frame.bytes()[0] = std::byte{}; }",
                r"C3892|C2106|C2678|C2679",
            ),
        },
    ),
}


def succeeded(command):
    return (
        command["status"] == "Exited"
        and command["exit_code"] == 0
        and command["process_tree"]["active_after"] == 0
    )


def main():
    parser = argparse.ArgumentParser()
    for name in (
        "case", "binary", "includes", "generator", "platform", "toolset",
        "sdk", "config", "work", "runtime-dir", "root-build", "target-metadata",
    ):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    short = args.case.split(".")[-1]
    if short not in CONTROLS and short != "internal_component_boundary":
        raise ValueError("unsupported policy child case")
    runtime = Path(args.runtime_dir)
    assert (runtime / "cl.exe").is_file()
    os.environ["PATH"] = str(runtime) + os.pathsep + os.environ.get("PATH", "")
    os.environ["MSBUILDDISABLENODEREUSE"] = "1"
    out = Path(args.work) / uuid.uuid4().hex
    out.mkdir(parents=True, exist_ok=False)
    commands = []

    def command(name, argv):
        streams = [out / (name + suffix) for suffix in ("-stdout.log", "-stderr.log")]
        row = execute(argv, ROOT, streams[0], streams[1], 240)
        row["raw"] = [
            {"path": path.name, "sha256": sha_file(path), "size": path.stat().st_size}
            for path in streams
        ]
        commands.append(row)
        save_json(out / "commands.json", {"case": args.case, "commands": commands})
        return row, b"".join(path.read_bytes() for path in streams)

    native, _ = command("native", [args.binary, args.case])
    assert succeeded(native)
    structure = {"header_sha256": sha_file(ROOT / "packages/runtime/policy/policy.hpp")}
    if short == "internal_component_boundary":
        cmake = ROOT / "tests/contract/authorization/CMakeLists.txt"
        assert not re.search(r"install\s*\(", cmake.read_text(encoding="utf-8"))
        target = json.loads(Path(args.target_metadata).read_text(encoding="utf-8"))
        assert target["link_libraries"] == "OCK::CoreContracts"
        assert target["core_contracts_links"] == "OCK::Foundation"
        save_json(out / "target-metadata.json", target)
        prefix = out / "install"
        installed, _ = command("install", [
            "cmake", "--install", args.root_build, "--config", args.config,
            "--prefix", str(prefix),
        ])
        assert succeeded(installed)
        assert not (prefix / "include/ock/runtime/policy.hpp").exists()
        assert not any("policy" in path.name.lower() for path in prefix.rglob("*.lib"))
        assert not any(path.name.lower() == "policy.hpp" for path in prefix.rglob("*.hpp"))
        for component in ("CoreContracts", "Runtime"):
            source = out / component
            source.mkdir()
            lines = [
                "cmake_minimum_required(VERSION 3.25)",
                "project(InstalledPolicyBoundary LANGUAGES NONE)",
                f"find_package(OCK 0.1.0 CONFIG REQUIRED COMPONENTS {component})",
            ]
            if component == "CoreContracts":
                lines += ["if(OCK_RUNTIME_AVAILABLE)",
                          'message(FATAL_ERROR "Runtime unexpectedly available")', "endif()"]
            (source / "CMakeLists.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
            configured, raw = command("installed-" + component, [
                "cmake", "-S", str(source), "-B", str(source / "build"),
                f"-DOCK_DIR={prefix}/lib/cmake/OCK",
            ])
            if component == "CoreContracts":
                assert succeeded(configured)
            else:
                assert configured["status"] == "Exited" and configured["exit_code"] != 0
                assert configured["process_tree"]["active_after"] == 0
                assert b"OCK component Runtime is not implemented in the current SDK" in raw
        structure.update(
            cmake_sha256=sha_file(cmake), actual_target=target,
            installed_config_sha256=sha_file(prefix / "lib/cmake/OCK/OCKConfig.cmake"),
        )
    else:
        positive, negative = CONTROLS[short]
        prefix = (
            '#include <array>\n#include "packages/runtime/policy/policy.hpp"\n'
            "using namespace ock::contracts;\nusing namespace ock::runtime::policy;\n"
        )
        sources = {"positive": (positive, ""), **negative}
        includes = [path for path in args.includes.split("|") if path]
        lines = ["cmake_minimum_required(VERSION 3.25)",
                 "project(PolicyCompileContract LANGUAGES CXX)",
                 "set(CMAKE_CXX_STANDARD 20)", "set(CMAKE_CXX_STANDARD_REQUIRED ON)"]
        for name, (body, _) in sources.items():
            (out / (name + ".cpp")).write_text(prefix + body + "\n", encoding="utf-8")
            quoted = " ".join("[==[" + path.replace("\\", "/") + "]==]" for path in includes)
            lines += [f"add_library({name} OBJECT EXCLUDE_FROM_ALL {name}.cpp)",
                      f"target_compile_options({name} PRIVATE /EHsc /utf-8 /Zc:__cplusplus /permissive-)",
                      f"target_include_directories({name} PRIVATE {quoted})"]
        (out / "CMakeLists.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        argv = ["cmake", "-S", str(out), "-B", str(out / "build"), "-G", args.generator,
                f"-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake",
                "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded"]
        for switch, value in (("-A", args.platform), ("-T", args.toolset)):
            if value:
                argv += [switch, value]
        if args.sdk:
            argv += ["-DCMAKE_SYSTEM_VERSION=" + args.sdk]
        configured, _ = command("configure", argv)
        assert succeeded(configured)
        for name, (_, diagnostic) in sources.items():
            built, raw = command(name, [
                "cmake", "--build", str(out / "build"), "--config", args.config,
                "--target", name, "--parallel", "2", "--", "/nr:false",
            ])
            if name == "positive":
                assert succeeded(built)
            else:
                assert built["status"] == "Exited" and built["exit_code"] != 0
                assert built["process_tree"]["active_after"] == 0
                text = raw.decode("utf-8", errors="replace")
                assert re.search(r"error (?:" + diagnostic + r")", text), text
                assert name + ".cpp" in text
        structure["compile_controls"] = list(sources)
    save_json(out / "structure.json", structure)
    print(json.dumps({"case": args.case, "status": "Passed", "evidence": str(out)}))


if __name__ == "__main__":
    main()
