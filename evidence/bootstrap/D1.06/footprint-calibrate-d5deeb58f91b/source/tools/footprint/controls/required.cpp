// 确实被 EXE 导入并调用；遗漏此 DLL 必须使分发集合验证失败。
extern "C" __declspec(dllexport) int footprint_required_value(){return 53;}
