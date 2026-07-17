# 发布 / 部署报告模板

> CD 或 Jenkins Deploy 填充为 `docs/DEPLOY_RELEASE_REPORT.md`（Artifact / Release 附件）。

## 元信息

| 项 | 值 |
|----|----|
| 版本 / Tag | `{{TAG}}` |
| VERSION 文件 | `{{VERSION}}` |
| commit | `{{COMMIT}}` |
| 触发 | `{{TRIGGER}}`（tag push / workflow_dispatch / Jenkins） |
| dry_run | `{{DRY_RUN}}` |
| 生成时间 | `{{TIME}}` |

## Breaking change（MCP / CLI）

| 是否有破坏性变更 | 说明（工具增删改、必填字段） |
|:----------------:|------------------------------|
| `{{BREAKING}}` | `{{BREAKING_NOTES}}` |

须同步 [CHANGELOG.md](../../CHANGELOG.md) 与 Release notes。契约真源：`src/mcp.hpp` · `toolList()`；OpenAPI 为生成物。

## 制品与哈希

| 平台 | 文件名 | SHA256 |
|------|--------|--------|
| Linux | `{{LINUX_ASSET}}` | `{{LINUX_SHA256}}` |
| Windows | `{{WINDOWS_ASSET}}` | `{{WINDOWS_SHA256}}` |
| macOS | `{{MACOS_ASSET}}` | `{{MACOS_SHA256}}` |

## 健康检查

| 检查 | 命令 | 结果 |
|------|------|:----:|
| 帮助信息 | `graphmcp --help`（或 `./bin/graphmcp --help`） | `{{HEALTH_HELP}}` |
| 单元冒烟（构建机） | `make test`（CD 矩阵内） | `{{HEALTH_TEST}}` |

## 部署目标

| 目标 | 状态 | 说明 |
|------|:----:|------|
| GitHub Release | `{{GH_RELEASE}}` | URL：`{{RELEASE_URL}}` |
| 本地 nginx 下载站 | `{{NGINX_DEPLOY}}` | http://localhost:8081/（Jenkins+Ansible） |

## 回滚方式

1. 保留上一可用 tag 的 Release 资产；新版本有问题时指导用户下载上一 tag。
2. 本地 nginx：从 `/artifacts` 移除坏包后重跑 `deploy_release.yml`，或恢复备份文件。
3. 勿对 `main` 强推；用补丁 tag 或 revert 提交修复。

详见 [RUNBOOK.md](../RUNBOOK.md)。

## 签核

| 角色 | 结论 | 日期 |
|------|------|------|
| 构建/发布 | GO / NO-GO | |
| 对照 DoD | 见 [ACCEPTANCE_DOD](../ACCEPTANCE_DOD.md) | |
