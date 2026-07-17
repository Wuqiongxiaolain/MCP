#!/usr/bin/env bash
# entrypoint-docker-sock.sh
# 原理：
# 1) docker.sock GID 因 Docker Desktop / 重建而变化 → 启动时对齐组，jenkins 可 docker exec
# 2) 宿主机 bind-mount 仓库属主 ≠ jenkins → Git dubious ownership → 写入 safe.directory
# 每次 compose up --build 后无需手工 usermod / git config。
set -euo pipefail

# fix_docker_sock_group: 使 jenkins 能访问 /var/run/docker.sock
fix_docker_sock_group() {
  local sock="${DOCKER_SOCK:-/var/run/docker.sock}"
  if [[ ! -S "$sock" ]]; then
    echo "entrypoint: ${sock} 不是 socket，跳过 docker 组对齐"
    return 0
  fi

  local sock_gid
  sock_gid="$(stat -c '%g' "$sock")"

  local group_name=""
  if getent group "${sock_gid}" >/dev/null 2>&1; then
    group_name="$(getent group "${sock_gid}" | cut -d: -f1)"
  else
    if getent group docker >/dev/null 2>&1; then
      if groupmod -g "${sock_gid}" docker 2>/dev/null; then
        group_name="docker"
      else
        group_name="dockersock"
        groupadd -g "${sock_gid}" dockersock
      fi
    else
      groupadd -g "${sock_gid}" docker
      group_name="docker"
    fi
    group_name="$(getent group "${sock_gid}" | cut -d: -f1)"
  fi

  usermod -aG "${group_name}" jenkins
  echo "entrypoint: docker.sock gid=${sock_gid} -> group=${group_name}; jenkins 已加入该组"
}

# fix_git_safe_directory: 允许 Pipeline 读取挂载仓库（Windows/宿主机 UID 不一致）
fix_git_safe_directory() {
  local home="/var/jenkins_home"
  local cfg="${home}/.gitconfig"
  mkdir -p "${home}"
  touch "${cfg}"
  chown -R jenkins:jenkins "${home}" 2>/dev/null || true

  ensure_safe() {
    local dir="$1"
    if ! git config --file "${cfg}" --get-all safe.directory 2>/dev/null | grep -Fxq "${dir}"; then
      git config --file "${cfg}" --add safe.directory "${dir}"
    fi
  }

  # *：本地 DevOps 栈常见多路径挂载；另显式写出 Job 使用的路径
  ensure_safe '*'
  ensure_safe '/workspace/MCP-'
  ensure_safe '/workspace/MCP-/.git'
  chown jenkins:jenkins "${cfg}"
  echo "entrypoint: git safe.directory 已配置（* 与 /workspace/MCP-）"
}

if [[ "$(id -u)" -eq 0 ]]; then
  fix_docker_sock_group
  fix_git_safe_directory
  # --init-groups：重新加载补充组，否则 usermod 对本进程不生效
  exec setpriv --reuid=jenkins --regid=jenkins --init-groups -- \
    /usr/bin/tini -- /usr/local/bin/jenkins.sh "$@"
fi

exec /usr/bin/tini -- /usr/local/bin/jenkins.sh "$@"
