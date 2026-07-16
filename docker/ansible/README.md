# Ansible 运行容器

Jenkins `Deploy download-server` 阶段通过 `docker exec ansible-runner ansible-playbook ...` 发布制品到 nginx 下载站。

## 启动

```powershell
cd D:\Schoolworks\Coding\DevOps\MCP-
docker compose -f docker/ansible/docker-compose.yml up -d --build
```

## 手动试跑

```bash
docker exec ansible-runner ansible-playbook \
  -i /ansible-projects/MCP-/ansible/inventories/docker.yml \
  /ansible-projects/MCP-/ansible/playbooks/deploy_release.yml
```

## 挂载

| 路径 | 用途 |
|------|------|
| `/ansible-projects/MCP-` | playbook / inventory（只读） |
| `/artifacts` | CI/CD 制品与 `index.html` |
| `/var/run/docker.sock` | `community.docker` 查询 nginx 容器 |
