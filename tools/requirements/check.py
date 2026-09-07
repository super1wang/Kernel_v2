"""D0.01 登记校验；不发现或执行未来运行时测试，不生成包级通过。"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def read_inputs(root: Path = ROOT) -> tuple[dict, dict, dict]:
    return tuple(json.loads((root / p).read_text(encoding="utf-8")) for p in (
        "tests/manifest.json", "tests/manifests/expected.json", "tests/manifests/conformance.json"))


def validate(manifest: dict, expected: dict, conformance: dict, root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)
    def indexed(items: list[dict], label: str) -> dict:
        result = {item["id"]: item for item in items}
        require(len(result) == len(items), f"{label} 存在重复 ID")
        return result
    def text(value: object) -> bool:
        return isinstance(value, str) and bool(value.strip())

    require(manifest["format"] == "ock.requirements/1", "需求格式版本无效")
    require(expected["format"] == "ock.expected/1", "预期格式版本无效")
    require(conformance["format"] == "ock.conformance-draft/1", "端口格式版本无效")
    require(manifest["task_id"] == expected["task_id"] == "D0.01", "当前包必须为 D0.01")
    for doc, label in ((expected, "预期"), (conformance, "端口")):
        require(doc["maturity"] == "Draft" and doc["review_status"] == "Pending",
                f"{label} 草案不能由登记检查冒充已审查")
    sources = manifest["normative_sources"]
    require({s["path"] for s in sources} == {
        "docs/01_Architecture_v3.3.md", "docs/02_Execution_Plan_v3.3.md"} and len(sources) == 2,
        "必须绑定两份唯一规范")
    for source in sources:
        path = root / source["path"]
        require(path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == source["sha256"],
                f"规范指纹不匹配：{source['path']}")
    arch = (root / "docs/01_Architecture_v3.3.md").read_text(encoding="utf-8-sig")
    plan = (root / "docs/02_Execution_Plan_v3.3.md").read_text(encoding="utf-8-sig")
    reqs = indexed(manifest["requirements"], "需求")
    packages = indexed(manifest["work_packages"], "工作包")
    cases = indexed(expected["future_cases"], "用例")
    increments = indexed(manifest["increments"], "增量")
    invariants = indexed(manifest["invariants"], "不变量")
    require(set(reqs) == {f"R{i:02}" for i in range(1, 25)}, "必须完整登记 R01–R24")
    require({r["primary_family"] for r in reqs.values()} == {f"T{i:02}" for i in range(1, 25)},
            "必须完整登记 T01–T24")
    require(set(increments) == {f"N{i}" for i in range(1, 12)}, "必须完整登记 N1–N11")
    require(set(invariants) == {f"C{i}" for i in range(1, 7)} | {"PlanCompleted"},
            "必须完整登记 C1–C6 与 PlanCompleted")
    normative_tasks = {}
    for m in re.finditer(r"^#### (D\d\.\d{2})｜([^\n]+)\n(.*?)(?=^#### |^### |\Z)", plan, re.M | re.S):
        tid, title, body = m.groups()
        deps = re.search(r"\*\*前置依赖：\*\*(.*)", body).group(1)
        families = re.search(r"\*\*架构依据／测试族：\*\*(.*)", body).group(1)
        normative_tasks[tid] = (title.strip(), re.findall(r"\[(D\d\.\d{2})\]", deps),
                                list(dict.fromkeys(re.findall(r"T\d{2}", families))))
    require(len(normative_tasks) == len(packages) == 64 and set(normative_tasks) == set(packages),
            "64 个稳定编号工作包不可删改或增加")
    for tid, task in packages.items():
        require(tid in normative_tasks, f"未知工作包 {tid}")
        if tid in normative_tasks:
            title, deps, families = normative_tasks[tid]
            require((task["title"], task["dependencies"], task["test_families"]) == (title, deps, families),
                    f"工作包 {tid} 与规范的标题/前置/测试族不一致")
        require(task["gate"] == "G" + tid[1], f"工作包 {tid} 门禁错误")
        require(task["plan_anchor"] == "d" + tid[1:].replace(".", ""), f"工作包 {tid} 锚点不属于自身")
        require(f'<a id="{task["plan_anchor"]}"></a>' in plan, f"工作包 {tid} 锚点缺失")
        require(set(task["dependencies"]) <= packages.keys(), f"工作包 {tid} 前置不存在")
    visiting, visited = set(), set()
    def visit(tid: str) -> None:
        if tid in visiting:
            errors.append(f"工作包依赖成环：{tid}")
            return
        if tid in visited or tid not in packages:
            return
        visiting.add(tid)
        for dep in packages[tid]["dependencies"]:
            visit(dep)
        visiting.remove(tid)
        visited.add(tid)
    for tid in packages:
        visit(tid)
    normative_reqs = {r: (goal, t) for r, goal, t in re.findall(
        r"^\| (R\d{2}) \| (.*?) \| (T\d{2}) \|$", arch, re.M)}
    normative_owners = {r: re.findall(r"\[(D\d\.\d{2})\]", tasks) for r, t, tasks in re.findall(
        r"^\| (R\d{2}) / (T\d{2}) \| (.*?) \|$", plan, re.M)}
    for rid, req in reqs.items():
        require((req["objective"], req["primary_family"]) == normative_reqs.get(rid),
                f"需求 {rid} 与 A01 不一致")
        require(req["owner_tasks"] == normative_owners.get(rid), f"需求 {rid} 责任包与 E05.1 不一致")
        require(req["primary_case"] in cases, f"需求 {rid} 缺少主用例")
        if req["primary_case"] in cases:
            case = cases[req["primary_case"]]
            require(rid in case["requirements"] and case["family"] == req["primary_family"],
                    f"需求 {rid} 主用例关联错误")
        require(text(req["positive"]) and text(req["negative"]), f"需求 {rid} 缺正反行为")
    for cid, case in cases.items():
        require(re.fullmatch(r"T\d{2}\.[a-z0-9_]+\.[a-z0-9_]+", cid) is not None,
                f"用例命名不符合 Txx.component.behavior：{cid}")
        require(case["family"] == cid[:3], f"用例 {cid} 测试族错误")
        require(bool(case["requirements"]) and set(case["requirements"]) <= reqs.keys(),
                f"用例 {cid} 无有效需求")
        require(case["task_id"] in packages, f"用例 {cid} 无有效责任包")
        for rid in case["requirements"]:
            if rid in reqs:
                require(case["task_id"] in reqs[rid]["owner_tasks"], f"用例 {cid} 责任越出需求映射")
                require(case["family"] == reqs[rid]["primary_family"], f"用例 {cid} 与需求测试族不一致")
        require(case["implementation_status"] == "Planned", f"未实现用例 {cid} 不可宣称已运行")
        require(text(case["positive"]) and text(case["negative"]) and case["positive"] != case["negative"],
                f"用例 {cid} 无可区分的正反预期")
        require(type(case["repeat_required"]) is int and case["repeat_required"] >= 1,
                f"用例 {cid} 轮次无效")
        require(bool(case["profile"]) and text(case["backend"]) and bool(case["review_required"]),
                f"用例 {cid} 缺适用性或评审字段")
        require(type(case["fault_probe_required"]) is bool, f"用例 {cid} 缺故障探针声明")
    increment_section = plan.split("### E05.2", 1)[1].split("## E06", 1)[0]
    responsibility = {iid: re.findall(r"D[0-8](?:\.\d{2})?", row) for iid, row in re.findall(
        r"^\| (N\d+)[^|]*\| (.*)$", increment_section, re.M)}
    for iid, increment in increments.items():
        for field in ("contract_tasks", "first_validation_tasks", "final_validation_tasks"):
            require(bool(increment[field]) and set(increment[field]) <= packages.keys(),
                    f"增量 {iid} 缺有效 {field}")
            require(all(any(t == allowed or (len(allowed) == 2 and t.startswith(allowed + "."))
                            for allowed in responsibility.get(iid, [])) for t in increment[field]),
                    f"增量 {iid} 的 {field} 超出 E05.2 的责任范围")
        require(any(t.startswith("D8.") for t in increment["final_validation_tasks"]),
                f"增量 {iid} 缺最终复验位置")
        require(bool(increment["case_ids"]) and set(increment["case_ids"]) <= cases.keys(),
                f"增量 {iid} 缺具体测试子项")
        require(set(increment["case_ids"]) == {c["id"] for c in cases.values() if c.get("increment") == iid},
                f"增量 {iid} 子项双向关联不一致")
    require(all(c.get("increment") in increments for c in cases.values() if "increment" in c),
            "用例引用不存在的增量")
    for iid, invariant in invariants.items():
        require(bool(invariant["owner_tasks"]) and set(invariant["owner_tasks"]) <= packages.keys(),
                f"不变量 {iid} 缺责任包")
        require(text(invariant["positive"]) and text(invariant["negative"]), f"不变量 {iid} 缺正反行为")
        require(bool(invariant["architecture"]) and all(
            re.search(r"^## " + re.escape(ref) + r"｜", arch, re.M) for ref in invariant["architecture"]),
            f"不变量 {iid} 规范章节无效")
    consumers = indexed(manifest["consumers"], "消费者")
    require(set(consumers) == {"C-A", "C-B", "C-C"}, "必须恰好三个长期消费者")
    for cid, c in consumers.items():
        require(c["kind"] == "内核长期验证消费者", f"{cid} 越出内核范围")
        require("Runtime" in c["required_components"], f"{cid} 必须共用 Runtime")
        require(not (set(c["required_components"] + c["optional_components"]) & set(c["forbidden_components"])),
                f"{cid} 选择了禁止组件")
        require(bool(c["milestones"]) and set(c["milestones"]) <= packages.keys(), f"{cid} 验收包错误")
        require(text(c["target"]) and text(c["path"]) and text(c["behavior"]), f"{cid} 缺目标或行为")
    for cid in ("C-A", "C-B"):
        if cid in consumers:
            require(consumers[cid]["requires_document"] is False, f"{cid} 禁止伪 Document")
            require("Workspace" in consumers[cid]["forbidden_components"], f"{cid} 必须排除 Workspace")
    if "C-A" in consumers:
        require({"State", "Durable", "Adapter::SQLite"} <= set(consumers["C-A"]["forbidden_components"]),
                "C-A 必须排除状态和数据库")
    if "C-B" in consumers:
        require("State" in consumers["C-B"]["required_components"], "C-B 必须验证无文档状态")
    if "C-C" in consumers:
        require({"State", "Workspace"} <= set(consumers["C-C"]["required_components"]),
                "C-C 必须验证 Workspace")
    require(manifest["scope"]["kernel_only"] is True and manifest["scope"]["legacy_migration"] is False,
            "本阶段只建设内核，不迁移旧项目")
    rules = conformance["rules"]
    require(rules["common_cases_optional"] is False, "共同必需合同不能降为可选")
    require(rules["not_applicable_is_passed"] is False, "NotApplicable 不等于 Passed")
    require(rules["fault_backend_is_qualified"] is False, "故障后端不得登记合格")
    require(rules["profile_missing_required_capability"] == "Failed", "必需能力缺失必须失败")
    ports = indexed(conformance["ports"], "端口")
    require(set(ports) == {"ExecutorConformance", "StorageConformance", "LoggingConformance", "AssetStorageConformance"},
            "必须登记四类端口合同")
    port_owners = {"ExecutorConformance": "D3.01", "StorageConformance": "D5.01",
                   "LoggingConformance": "D1.06", "AssetStorageConformance": "D6.01"}
    for pid, port in ports.items():
        require(port["owner_task"] == port_owners.get(pid), f"{pid} 端口责任包与规范不一致")
        require(port["owner_task"] in packages, f"端口 {pid} 无责任包")
        require(bool(port["common_required"]) and len(set(port["common_required"])) == len(port["common_required"]),
                f"端口 {pid} 共同必需项缺失或重复")
        require(bool(port["planned_backends"]) and port["qualified_backends"] == [],
                f"端口 {pid} 不可宣称已有合格后端")
    require(conformance["bootstrap"]["requires_future_production_backends"] is False,
            "G0 不能提前要求未来真实端口全部通过")
    checks = indexed(expected["checks"], "命令检查")
    require(set(checks) == {"CHECK.D0.01.traceability", "CHECK.D0.01.negative_guards"},
            "D0.01 必需检查缺失")
    require(all(c["kind"] == "command" and c["argv"] for c in checks.values()),
            "非 CTest 检查不能伪造 CTest 结果")
    required_commands = {
        "CHECK.D0.01.traceability": ["python", "tools/requirements/check.py"],
        "CHECK.D0.01.negative_guards": ["python", "-m", "unittest", "discover", "-s", "tests/requirements", "-v"],
    }
    for cid, check in checks.items():
        require(check["argv"] == required_commands.get(cid), f"{cid} 命令与登记入口不一致")
    return errors


def main() -> int:
    try:
        inputs = read_inputs()
        errors = validate(*inputs)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as exc:
        print(json.dumps({"check_id": "CHECK.D0.01.traceability", "errors": [str(exc)]}, ensure_ascii=False))
        return 2
    m, e, c = inputs
    print(json.dumps({"check_id": "CHECK.D0.01.traceability", "errors": errors,
                      "counts": {"requirements": len(m["requirements"]), "work_packages": len(m["work_packages"]),
                                 "invariants": len(m["invariants"]), "increments": len(m["increments"]),
                                 "consumers": len(m["consumers"]), "planned_cases": len(e["future_cases"])},
                      "scope": "仅 D0.01 登记检查；没有执行 future_cases 或内核运行时"}, ensure_ascii=False, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
