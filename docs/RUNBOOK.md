# graphmcp 运维 Runbook

> latest update: v0.2.9-beta, 2026-07-17

本地 DevOps：GitHub Actions 负责日常 CI/CD；Jenkins（Docker）+ Ansible Runner + nginx 下载站负责课程/最终验证与独立发布。

## 组件一览

| 组件 | 入口 | 默认端口 / 路径 |
|------|------|-----------------|
| Jenkins | [`docker/jenkins/`](../docker/jenkins/) | http://localhost:8080 |
| Ansible Runner | [`docker/ansible/`](../docker/ansible/) | 无 UI；`docker exec ansible-runner ...` |
| 下载站 (nginx) | Ansible `deploy_release.yml` | http://localhost:8081/ |
| 制品共享卷 | 宿主机 `.../artifacts` → 容器 `/artifacts` | 见 [`ansible/README.md`](../ansible/README.md) |

Pipeline 定义：根目录 [`Jenkinsfile`](../Jenkinsfile)（对齐 `.github/workflows/ci.yml` 与 `deploy.yml`）。

---

## 1. 启动 / 停止

### Jenkins

```powershell
cd D:\Schoolworks\Coding\DevOps\MCP-
docker compose -f docker/jenkins/docker-compose.yml up -d --build
docker compose -f docker/jenkins/docker-compose.yml down
```

验证：

```powershell
docker logs jenkins 2>&1 | Select-String "docker.sock gid|safe.directory"
docker exec -u jenkins jenkins docker ps
```

细节见 [`docker/jenkins/README.md`](../docker/jenkins/README.md)。

### Ansible Runner

```powershell
docker compose -f docker/ansible/docker-compose.yml up -d --build
docker compose -f docker/ansible/docker-compose.yml down
```

### 下载站

由 playbook 维护 `download-server` 容器；发布后访问 http://localhost:8081/ 。

---

## 2. 日常 CI（Jenkins）

1. 打开 Jenkins Job（Multibranch 或单 Job）。
2. **Build with Parameters**（常用）：

| 参数 | 建议 | 说明 |
|------|------|------|
| `PIPELINE_MODE` | `ci` 或 `auto` | `auto`：普通分支→CI，标签 `v*`→CD |
| `CI_REF` | 分支名或留空 | 在任意 Job 上检出功能分支做 CI |
| `DO_DEPLOY` | `true`/`false` | 是否把制品发到 nginx |

3. 构建成功后：Build → **Artifacts** 下载 `docs/TEST_REPORT.md`、`docs/QUALITY_GATE_REPORT.md` 等。
4. 验收勾选见 [ACCEPTANCE_DOD.md](ACCEPTANCE_DOD.md)。

对齐的 GitHub 路径：push/PR → [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)。

---

## 3. 发版（CD）

### GitHub Actions（主发版）

1. 确认 `VERSION`、CHANGELOG、OpenAPI 已更新。
2. 打 annotated tag 并 push：

```sh
git tag -a v0.2.9-beta -m "release notes..."
git push origin v0.2.9-beta
```

3. 触发 [`.github/workflows/deploy.yml`](../.github/workflows/deploy.yml)：三平台构建 → Release + `DEPLOY_RELEASE_REPORT.md`。
4. 也可 `workflow_dispatch` + `dry_run=true` 只构建不发 Release。

### Jenkins CD

1. `PIPELINE_MODE=cd`，或推送/检出 `v*` 标签且 `auto` 模式。
2. 可选：`CD_BUILD_MACOS` / `CD_BUILD_WINDOWS`（需对应 Agent）。
3. `CD_DRY_RUN=true`：只归档，不调 `gh release`。
4. 成功后检查 Artifacts 与 http://localhost:8081/ 。

---

## 4. Ansible 发布到 nginx

制品须已在共享卷 `/artifacts`（Jenkins Package 阶段会 `cp`）。

```bash
docker exec ansible-runner ansible-playbook \
  -i /ansible-projects/MCP-/ansible/inventories/docker.yml \
  /ansible-projects/MCP-/ansible/playbooks/deploy_release.yml
```

配置 Jenkins 工具示例：

```bash
docker exec ansible-runner ansible-playbook \
  -i /ansible-projects/MCP-/ansible/inventories/docker.yml \
  /ansible-projects/MCP-/ansible/playbooks/configure_jenkins_tools.yml
```

详见 [`ansible/README.md`](../ansible/README.md)。

---

## 5. 失败排查

| 现象 | 排查 |
|------|------|
| Jenkins 无法 `docker ps` | 重建镜像；查 sock GID / 入口脚本日志 |
| `dubious ownership` | 确认 `safe.directory` 与 compose 中 `GIT_CONFIG_*` |
| CI 无 `CI_REF` 参数 | 先跑一次默认构建加载新 `Jenkinsfile` |
| 共享卷无 tar/zip | 检查 Package 阶段是否写入 `/artifacts` |
| Ansible 失败 | `docker ps` 是否有 `ansible-runner`；inventory 路径是否挂载 |
| 下载站 404 | playbook 是否生成 `index.html`；nginx 容器是否在跑 |
| GitHub Sonar job skipped | 检查 `SONAR_ENABLED`、`SONAR_TOKEN`、`SONAR_HOST_URL`；见质量门报告 SKIPPED |

---

## 6. 回滚

| 场景 | 做法 |
|------|------|
| 错误 Release 制品 | 在 GitHub Release 删除错误资产或整次 Release；保留正确 tag 或打补丁 tag |
| nginx 下载站旧包 | 从 `/artifacts` 去掉坏包后重跑 `deploy_release.yml`，或手动替换目录内文件 |
| 应用数据（图/表 store） | 用户侧 `GRAPHMCP_STORE`：用 `rollback`/`checkout` 或恢复目录备份；与发版回滚无关 |
| 流水线代码回退 | `git revert` 或检出上一稳定 tag 重建 |

**禁止**在未确认的情况下对 `main` 做 `push --force`。

---

## 7. SonarCloud（可选）

1. 在 [sonarcloud.io](https://sonarcloud.io) 导入仓库，Project Key 与 [`sonar-project.properties`](../sonar-project.properties) 对齐。
2. GitHub Secrets：`SONAR_TOKEN`、`SONAR_HOST_URL=https://sonarcloud.io`；Variable：`SONAR_ENABLED=true`。
3. 仅 **push** 到 `main`/`dev` 时跑 Sonar job；未配置时质量门报告记 **SKIPPED**（不假装 PASS）。

社区版自建 SonarQube **不一定**带官方 C++ 分析；课程默认以 **cppcheck** 为必过质量门。
