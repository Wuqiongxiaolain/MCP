# 自定义 Jenkins 镜像

## 构建并启动

```powershell
cd D:\Schoolworks\Coding\DevOps\MCP-
docker compose -f docker/jenkins/docker-compose.yml up -d --build
```

重建后**无需**再手工改 docker.sock 或 `git safe.directory`：入口脚本会自动对齐。

## 设计要点（对齐课程 Docker 规范）

1. Dockerfile 短：单阶段、一条主 `RUN` + 入口脚本
2. 清理 apt 列表；不 COPY 业务源码/日志
3. 基于官方 Jenkins **运行时**镜像；应用仍由 Pipeline 在运行时构建

## 启动时自动处理（持久化）

| 问题 | 方案 |
|------|------|
| `docker.sock` permission denied | 按 sock GID 把 `jenkins` 加入对应组 |
| `fatal: detected dubious ownership`（挂载 `/workspace/MCP-`） | ① `/etc/gitconfig` + `~/.gitconfig` 写 `safe.directory`；② compose 注入 `GIT_CONFIG_*`（Git 插件子进程必读） |

验证（重建后）：

```powershell
docker logs jenkins 2>&1 | Select-String "docker.sock gid|safe.directory"
docker exec -u jenkins jenkins docker ps
docker exec -u jenkins jenkins git config --system --get-all safe.directory
docker exec -u jenkins jenkins git config --show-origin --get-all safe.directory
```

应能看到 `file:/etc/gitconfig` 与 `*` / `/workspace/MCP-/.git`。然后重跑 Pipeline。

## 预装内容

`sudo`、`g++`、`make`、`python3`、`imagemagick`、`librsvg2-bin`（`rsvg-convert`，供 smoke PNG/PDF）、`jq`、`git`、`docker.io` + `docker-cli`
