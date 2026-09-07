"""对追踪校验器注入反例，禁止清单缺失或虚构完成被接受。"""
from copy import deepcopy
import unittest
from tools.requirements.check import read_inputs, validate


class TraceabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.baseline = read_inputs()

    def setUp(self):
        self.manifest, self.expected, self.conformance = deepcopy(self.baseline)

    def assertRejected(self, message):
        self.assertTrue(any(message in error for error in validate(
            self.manifest, self.expected, self.conformance)), message)

    def test_valid_registration(self):
        self.assertEqual([], validate(self.manifest, self.expected, self.conformance))

    def test_deleted_requirement(self):
        self.manifest["requirements"].pop()
        self.assertRejected("R01–R24")

    def test_deleted_expected_case(self):
        self.expected["future_cases"].pop(0)
        self.assertRejected("缺少主用例")

    def test_duplicate_expected_case(self):
        self.expected["future_cases"].append(deepcopy(self.expected["future_cases"][0]))
        self.assertRejected("重复 ID")

    def test_removed_dependency(self):
        self.manifest["work_packages"][1]["dependencies"] = []
        self.assertRejected("标题/前置/测试族不一致")

    def test_dependency_cycle(self):
        self.manifest["work_packages"][0]["dependencies"] = ["D0.02"]
        self.assertRejected("依赖成环")

    def test_added_work_package(self):
        package = deepcopy(self.manifest["work_packages"][0])
        package["id"] = "D9.01"
        self.manifest["work_packages"].append(package)
        self.assertRejected("64 个稳定编号")

    def test_wrong_requirement_owner(self):
        self.manifest["requirements"][0]["owner_tasks"] = ["D0.01"]
        self.assertRejected("责任包与 E05.1 不一致")

    def test_case_owned_by_unrelated_package(self):
        self.expected["future_cases"][0]["task_id"] = "D7.03"
        self.assertRejected("责任越出需求映射")

    def test_missing_increment(self):
        self.manifest["increments"].pop()
        self.assertRejected("N1–N11")

    def test_increment_lacks_final_revalidation(self):
        self.manifest["increments"][0]["final_validation_tasks"] = ["D3.07"]
        self.assertRejected("缺最终复验位置")

    def test_increment_case_link_mismatch(self):
        self.manifest["increments"][0]["case_ids"].pop()
        self.assertRejected("双向关联不一致")

    def test_missing_plan_completed(self):
        self.manifest["invariants"].pop()
        self.assertRejected("PlanCompleted")

    def test_stateless_consumer_cannot_select_state(self):
        self.manifest["consumers"][0]["optional_components"].append("State")
        self.assertRejected("选择了禁止组件")

    def test_settings_consumer_cannot_require_fake_document(self):
        self.manifest["consumers"][1]["requires_document"] = True
        self.assertRejected("禁止伪 Document")

    def test_planned_case_cannot_claim_passed(self):
        self.expected["future_cases"][0]["implementation_status"] = "Passed"
        self.assertRejected("不可宣称已运行")

    def test_conformance_cannot_waive_common_cases(self):
        self.conformance["rules"]["common_cases_optional"] = True
        self.assertRejected("不能降为可选")

    def test_not_applicable_is_not_passed(self):
        self.conformance["rules"]["not_applicable_is_passed"] = True
        self.assertRejected("NotApplicable 不等于 Passed")

    def test_fault_backend_cannot_be_qualified(self):
        self.conformance["rules"]["fault_backend_is_qualified"] = True
        self.assertRejected("故障后端不得登记合格")

    def test_no_production_backend_is_verified_yet(self):
        self.conformance["ports"][0]["qualified_backends"] = ["CpuPool"]
        self.assertRejected("不可宣称已有合格后端")

    def test_g0_cannot_demand_future_production_backend(self):
        self.conformance["bootstrap"]["requires_future_production_backends"] = True
        self.assertRejected("不能提前要求")

    def test_changed_normative_hash(self):
        self.manifest["normative_sources"][0]["sha256"] = "0" * 64
        self.assertRejected("规范指纹不匹配")

    def test_registering_legacy_migration_is_out_of_scope(self):
        self.manifest["scope"]["legacy_migration"] = True
        self.assertRejected("不迁移旧项目")

    def test_command_cannot_be_replaced_by_noop(self):
        self.expected["checks"][1]["argv"] = ["python", "-c", "pass"]
        self.assertRejected("命令与登记入口不一致")

    def test_task_anchor_must_point_to_own_task(self):
        self.manifest["work_packages"][0]["plan_anchor"] = "d801"
        self.assertRejected("锚点不属于自身")

    def test_stateless_consumer_requires_runtime(self):
        self.manifest["consumers"][0]["required_components"] = []
        self.assertRejected("必须共用 Runtime")

    def test_conformance_owner_matches_plan(self):
        self.conformance["ports"][0]["owner_task"] = "D0.01"
        self.assertRejected("端口责任包与规范不一致")

    def test_increment_owner_matches_normative_responsibility(self):
        self.manifest["increments"][0]["first_validation_tasks"] = ["D0.01"]
        self.assertRejected("超出 E05.2 的责任范围")

    def test_missing_command_check(self):
        self.expected["checks"].pop()
        self.assertRejected("必需检查缺失")


if __name__ == "__main__":
    unittest.main()
