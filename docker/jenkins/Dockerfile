# 基于官方 Jenkins 运行时镜像；仅预装 CI Agent 运行时依赖
# 不 COPY 业务源码、不在镜像内编译应用
# 网络可达时建议改为 jenkins/jenkins:lts-jdk21 以钉版本
FROM jenkins/jenkins:latest

USER root
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      sudo \
      g++ \
      make \
      python3 \
      imagemagick \
      librsvg2-bin \
      jq \
      git \
      docker.io \
      docker-cli \
 && echo 'jenkins ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/jenkins \
 && chmod 440 /etc/sudoers.d/jenkins \
 && rm -rf /var/lib/apt/lists/*

USER jenkins
