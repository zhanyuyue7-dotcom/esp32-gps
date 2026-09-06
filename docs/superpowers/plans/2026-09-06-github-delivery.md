# GitHub CI 与固件交付计划

**Goal:** 上传本项目到独立 GitHub 仓库，CI 测试与重新构建通过，生成仅接线手册及 BIN 的 ZIP。

**Architecture:** 已成功烧录的 build BIN 作为固定交付快照，以 delivery.json 锁定 SHA256 和烧录地址。CI 编译用于检查源码可构建，不替换实测快照；打包严格按白名单，并将字体许可证合并进手册。

**Tech Stack:** ESP-IDF 5.5.3、CMake/CTest、Node.js、Python stdlib、GitHub Actions、Git/gh CLI。

- [ ] 核对最新源码、BIN 时间和成功烧录日志，修正接线文档冲突。
- [ ] 先运行 scripts/test_package_delivery.py，观察缺少打包器的失败；实现 scripts/package_delivery.py 后验证白名单、原始 SHA256、防路径越界及可重复打包。
- [ ] 添加 delivery.json 和固件快照；配置 .github/workflows/ci.yml，依次运行 host tests、ESP-IDF build、打包及 artifact 上传。
- [ ] 检查待提交文件，不上传缓存、日志、截图、私有凭据；创建独立仓库并推送，不改固件内版本或追溯伪造 Git 历史。
- [ ] 等待 CI 结果并下载 artifact，对本地与 CI ZIP 做 SHA256 一致性检查；交付仓库、CI 和两个 ZIP 地址。

验证命令：python -m unittest discover -s scripts -p 'test_*.py' -v；cmake -S host -B build-host -G Ninja；cmake --build build-host；ctest --test-dir build-host --output-on-failure；python scripts/package_delivery.py。

不接触板卡、不重新烧录、不创建 GitHub Release/tag 或升级业务版本；用户已明确授权上传 GitHub，仓库可见性未回复时采用 private。
