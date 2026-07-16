# 自定义 Jenkins 镜像

## 构建并启动

```powershell
cd D:\Schoolworks\Coding\DevOps\MCP-
docker compose -f docker/jenkins/docker-compose.yml up -d --build
```

## 设计要点（对齐课程 Docker 规范）

1. Dockerfile 短：单阶段、一条 `RUN`
2. 清理 apt 列表；不 COPY 源码/日志
3. 基于官方 Jenkins **运行时**镜像；应用仍由 Pipeline 在运行时构建

## 预装内容

`sudo`、`g++`、`make`、`python3`、`imagemagick`、`librsvg2-bin`（`rsvg-convert`，供 smoke PNG/PDF）、`jq`、`git`、`docker.io` + `docker-cli`
