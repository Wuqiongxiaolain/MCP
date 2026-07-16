# Ansible 运行环境：供 Jenkins 经 docker.sock 调用 playbook
FROM python:3.12-slim-bookworm

RUN pip install --no-cache-dir ansible docker requests \
 && ansible-galaxy collection install community.docker \
 && rm -rf /root/.cache

WORKDIR /ansible-projects/MCP-
