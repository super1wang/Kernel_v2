# B6 最终精确化 footprint 来源刷新批准

AI 技术批准时间：2026-09-14T07:41:46.974211+00:00。生产来源 `3abde2ebc1f11acccbad106142c96760a95f5d47`。

CommitReport 公共布局变化触发本次刷新。仅刷新来源方法摘要、批准时间及本批准文档绑定；沿用 B6 closure 全部原数值预算、pilot 标识、Native 7 模式和 Embedded 3 配置（Release 含 startup），每项六组 ABBA。方法脚本、分配零窗口、线程等式与结构上界不变。所有方法输入逐字节匹配生产来源。

该批准只允许按原预算测量，不代表机器 Passed；出现失败保留原文，禁止提高阈值。全部通过并归档机器 acceptance 前 B7 HOLD。

- native/win-msvc-debug/occupancy: `7270dd7f8674d34c8ed26c6ca9ae646387388889c671660cd1a115202e54e227`
- native/win-msvc-debug/allocation: `7e753ee3039c2ac2931b86c119854297f662a151b9586a296ae74b6f81b30d56`
- native/win-msvc-release/occupancy: `cc75ac690ad4814347fde1bd99222971a7f5d0aef96049b069454963223aa27c`
- native/win-msvc-release/allocation: `7b65f838cbeced4e68ce8183099bfdc460f927e74f01af1194095beffeb1e563`
- native/win-msvc-release/latency: `0b8f184e9ae344c5f34f191bed910025b0c1d3b7172b89bd449e7572f6a07915`
- native/win-msvc-asan/occupancy: `6f487ef7efb32ad480e5f75a96bbd5ff30188ce60a9d9a0e46e335c9e4f54345`
- native/win-msvc-asan/allocation: `0881a583c2be1aa6cb419544b2e7d7b3ac84836ba19b49e40bea575eda29f5ff`
- embedded/win-msvc-debug/allocation: `7ff3e5d6af2b8d9f86f396b450a85eba9e2009cf02022767d5154ec03f23d7a9`
- embedded/win-msvc-release/occupancy: `450322cbc8600e5f33f4d47aee03fe4933d58ed9e64b9338a75f4f70829bb6a0`
- embedded/win-msvc-asan/allocation: `63733ca04de686a10e12a39d04fb2014d8de549fc9a5117132e190df11dc2302`
- embedded/win-msvc-release/startup: `450322cbc8600e5f33f4d47aee03fe4933d58ed9e64b9338a75f4f70829bb6a0`
