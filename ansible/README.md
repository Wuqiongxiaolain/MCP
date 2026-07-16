# Ansible → 管理 Jenkins 容器 / 发布下载站

## 已就绪的本机前提

Ansible 运行容器 `ansible-runner`（见 `docker/ansible/docker-compose.yml`）已挂载：

| 挂载 | 用途 |
|------|------|
| `/var/run/docker.sock` | 调用 Docker API，操作 `jenkins` / `download-server` 容器 |
| `D:/.../MCP-` → `/ansible-projects/MCP-` | playbook / inventory |
| `D:/.../artifacts` → `/artifacts` | CI/CD 制品与 nginx 下载页 |

启动 Ansible 容器：

```bash
docker compose -f docker/ansible/docker-compose.yml up -d --build
```

手动试跑（配置 Jenkins 工具示例）：

```bash
docker exec ansible-runner ansible-playbook \
  -i /ansible-projects/MCP-/ansible/inventories/docker.yml \
  /ansible-projects/MCP-/ansible/playbooks/configure_jenkins_tools.yml
```

手动试跑（发布下载站）：

```bash
docker exec ansible-runner ansible-playbook \
  -i /ansible-projects/MCP-/ansible/inventories/docker.yml \
  /ansible-projects/MCP-/ansible/playbooks/deploy_release.yml
```

---

## 示例 playbook：configure_jenkins_tools

在宿主机验证标记文件：

```bash
docker exec jenkins cat /var/jenkins_home/managed-by-ansible.txt
docker exec jenkins bash -lc "git --version; curl --version | head -1; jq --version"
```

| 动作 | 位置 |
|------|------|
| 写入标记文件 | `/var/jenkins_home/managed-by-ansible.txt`（数据卷持久） |
| `apt install` | 容器内 `curl` / `jq` / `git`（重建 Jenkins 镜像层会丢，数据卷文件还在） |

不会改 Jenkins 的 `-e` / 端口等**启动参数**；那需要 `docker_container` 重建容器。

## 发布到 nginx 下载站（CI/CD → Ansible）

共享卷：宿主机 `D:/Schoolworks/Coding/DevOps/artifacts` → 容器 `/artifacts`  
下载站：容器 `download-server`（nginx），浏览器 http://localhost:8081/

流程：

1. Jenkins Package 把 `graphmcp*.tar.gz` 拷到 `/artifacts`
2. 阶段 `Deploy download-server`：`docker exec ansible-runner ansible-playbook ... deploy_release.yml`
3. Playbook 生成 `index.html` 并校验 nginx
4. 打开 http://localhost:8081/ 下载制品

构建参数：`DO_DEPLOY=true`（默认），`PIPELINE_MODE=ci` 或 `cd`，Mac/Win 保持 false。
