# Ansible + Semaphore → 管理 Jenkins 容器内工具/文件

## 已就绪的本机前提

Semaphore 容器 `semaphore` 已挂载：

| 挂载 | 用途 |
|------|------|
| `/var/run/docker.sock` | 调用 Docker API，操作 `jenkins` 容器 |
| `D:/.../MCP-` → `/ansible-projects/MCP-` | 本地 Git 仓库作 Semaphore Repository |
| `semaphore-data` | SQLite 与配置持久化 |

控制节点需一次性准备（**重建 Semaphore 后务必再执行**）：

```bash
# Git：任务进程 HOME 可能非 /home/semaphore，须写 system 级
docker exec -u root semaphore git config --system --add safe.directory /ansible-projects/MCP-
docker exec -u root semaphore git config --system --add safe.directory /ansible-projects/MCP-/.git

# Docker SDK + socket 权限
docker exec -u root semaphore /opt/semaphore/apps/ansible/13.5.0/venv/bin/pip install docker
docker exec -u root semaphore chmod 666 /var/run/docker.sock
```

手动试跑：

```bash
docker exec -u semaphore semaphore ansible-playbook \
  -i /ansible-projects/MCP-/ansible/inventories/docker.yml \
  /ansible-projects/MCP-/ansible/playbooks/configure_jenkins_tools.yml
```

---

## Semaphore UI 点选清单

打开 http://localhost:3000 ，账号 `admin` / `changeme`。

### 1. 新建项目

路径：**Projects** → **New Project**

| 字段 | 填写 |
|------|------|
| Project Name | `MCP-local` |
| （其余） | 默认即可 |

进入该项目。

### 2. 密钥（Key Store）——可跳过 SSH

路径：项目内 **Key Store** → **New Key**

本方案用 Docker socket，**不需要 SSH 私钥**。  
若 UI 强制建一把钥匙，可建空的 / 任意 “None” 类型占位（视版本而定），Template 里不引用即可。

若 Repository 用私有 Git 再补 Git 凭据；本地 `file://` 路径一般不需要。

### 3. 仓库（Repository）

路径：**Repositories** → **New Repository**

| 字段 | 填写 |
|------|------|
| Name | `MCP-ansible` |
| URL / Git URL | `file:///ansible-projects/MCP-` |
| Branch | `main` |
| Access Key | 无 / None（本地路径） |

保存后可点 **Check** / 刷新，应能看到仓库。

> 若 `file://` 校验失败：先在宿主机对本仓库 `git add ansible && git commit`，并在容器内执行  
> `git config --global --add safe.directory /ansible-projects/MCP-`（用 `semaphore` 用户）。

### 4. 清单（Inventory）

路径：**Inventory** → **New Inventory**

| 字段 | 填写 |
|------|------|
| Name | `docker-local` |
| User Credentials / Key | 无（local 连接） |
| Inventory 类型 | **File**（或 Static 视版本） |
| Path / 文件 | `ansible/inventories/docker.yml`（相对仓库根） |

说明：playbook 实际在 `localhost` 上跑，通过 API 操作名为 `jenkins` 的容器。

### 5. 环境变量（Environment，可选）

路径：**Environment** → **New Environment**

| 字段 | 填写 |
|------|------|
| Name | `jenkins-tools` |
| Extra Variables（YAML/JSON） | 见下 |

```yaml
jenkins_container_name: jenkins
jenkins_marker_path: /var/jenkins_home/managed-by-ansible.txt
```

### 6. 模板（Template）——真正执行的任务

路径：**Task Templates** → **New Template**

| 字段 | 填写 |
|------|------|
| Template Name | `configure-jenkins-tools` |
| Playbook Filename | `ansible/playbooks/configure_jenkins_tools.yml` |
| Inventory | `docker-local` |
| Repository | `MCP-ansible` |
| Environment | `jenkins-tools`（若建了） |
| Verbosity | 默认或 1 |

保存 → 打开该模板 → **Run** / **执行**。

### 7. 如何确认成功

- Semaphore 任务日志：`PLAY RECAP` 中 `failed=0`
- 在宿主机验证标记文件：

```bash
docker exec jenkins cat /var/jenkins_home/managed-by-ansible.txt
docker exec jenkins bash -lc "git --version; curl --version | head -1; jq --version"
```

---

## 示例会改什么

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
2. 阶段 `Deploy download-server`：`docker exec semaphore ansible-playbook ... deploy_release.yml`
3. Playbook 生成 `index.html` 并校验 nginx
4. 打开 http://localhost:8081/ 下载制品

构建参数：`DO_DEPLOY=true`（默认），`PIPELINE_MODE=ci` 或 `cd`，Mac/Win 保持 false。
