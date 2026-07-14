// Jenkinsfile — graphmcp CI 流水线
//
// 对应 GitHub Actions: .github/workflows/ci.yml
//
// ==== 必需的 Jenkins 插件 ====
//   - Git Plugin（内置）
//   - SonarQube Scanner       （可选 — 仅当启用 SonarQube 分析时需要）
//   - Credentials Binding      （内置 — GitLab 镜像同步需要）
//
// ==== 必需的 Jenkins 系统配置 ====
//   1. Multibranch Pipeline（推荐）或 GitHub Organization Folder
//      → 自动发现 main / dev / feature/** 分支及 PR
//   2. SonarQube Server 安装入口
//      → Manage Jenkins → Configure System → SonarQube servers
//      → 安装名称为 "SonarQube"（或在 SONAR_INSTALLATION_NAME 参数中自定义）
//   3. Credentials（Manage Jenkins → Credentials）：
//      → SONAR_TOKEN          : Secret text — SonarQube 访问令牌
//      → GITLAB_MIRROR_TOKEN  : Secret text — GitLab OAuth2 令牌
//
// ==== 可选：在 Jenkins Folder 级别设置以下环境变量 ====
//   SONAR_ENABLED          = true    （启用 SonarQube 分析）
//   GITLAB_MIRROR_ENABLED  = true    （启用 GitLab 镜像同步）
//   GITLAB_MIRROR_URL      = https://gitlab.com/...（GitLab 仓库地址）

pipeline {

    // ── 全局 agent：所有 stage 默认在此运行 ────────────────────────
    agent {
        label 'linux'
    }

    // ── 流水线选项 ────────────────────────────────────────────────
    options {
        // 同分支禁止并发（相当于 GHA concurrency group + cancel-in-progress）
        disableConcurrentBuilds()
        timeout(time: 20, unit: 'MINUTES')
        buildDiscarder(logRotator(daysToKeepStr: '30', numToKeepStr: '30'))
        timestamps()
        ansiColor('xterm')
    }

    // ── 环境变量 ──────────────────────────────────────────────────
    environment {
        GRAPHMCP_LOG    = 'info'
        // 使用临时 store 目录避免多构建间残留干扰
        GRAPHMCP_STORE  = 'ci-store'
    }

    // ── 参数（Multibranch 下用默认值自动运行，不阻塞） ────────────
    parameters {
        booleanParam(
            name: 'SONAR_ENABLED',
            defaultValue: false,
            description: '是否执行 SonarQube 静态分析（需要 Jenkins 已配置 SonarQube Server）'
        )
        booleanParam(
            name: 'GITLAB_MIRROR_ENABLED',
            defaultValue: false,
            description: '是否在 main 分支构建成功后同步到 GitLab 备份仓'
        )
        string(
            name: 'GITLAB_MIRROR_URL',
            defaultValue: '',
            description: 'GitLab 镜像仓库 HTTPS 地址（如 https://gitlab.com/ns/repo.git）'
        )
        string(
            name: 'SONAR_INSTALLATION_NAME',
            defaultValue: 'SonarQube',
            description: 'Jenkins 中配置的 SonarQube Server 安装名称'
        )
    }

    stages {

        // ═══════════════════════════════════════════════════════════
        // Stage 1: 检出代码
        // ═══════════════════════════════════════════════════════════
        stage('Checkout') {
            steps {
                checkout scm
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 2: 安装构建依赖
        // ═══════════════════════════════════════════════════════════
        stage('Install Dependencies') {
            steps {
                sh '''
                    sudo apt-get update -qq
                    sudo apt-get install -y -qq g++ make python3 imagemagick jq
                '''
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 3: 编译
        // ═══════════════════════════════════════════════════════════
        stage('Build') {
            steps {
                sh '''
                    make all CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"
                '''
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 4: 单元测试（3 组测试套件）
        // ═══════════════════════════════════════════════════════════
        stage('Unit Test') {
            steps {
                sh 'make test-all'
            }
            post {
                always {
                    archiveArtifacts(
                        artifacts: 'examples/example_testout/TEST_REPORT.md, examples/example_testout/TEST_REPORT.json',
                        allowEmptyArchive: true
                    )
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 5: CLI 冒烟测试
        // ═══════════════════════════════════════════════════════════
        stage('CLI Smoke Test') {
            environment {
                SMOKE_REQUIRE_RASTER = '1'
            }
            steps {
                sh 'make smoke'
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 6: MCP 协议冒烟测试
        // ═══════════════════════════════════════════════════════════
        stage('MCP Smoke Test') {
            steps {
                sh 'make mcp-smoke'
            }
            post {
                always {
                    archiveArtifacts(
                        artifacts: 'SMOKE_REPORT.md, mcp-smoke.log',
                        allowEmptyArchive: true
                    )
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 7: 打包制品
        // ═══════════════════════════════════════════════════════════
        stage('Package') {
            steps {
                sh '''
                    mkdir -p dist
                    cp bin/graphmcp dist/
                    cp README.md dist/
                    tar czf "graphmcp-${BUILD_NUMBER}.tar.gz" -C dist .
                '''
            }
            post {
                always {
                    archiveArtifacts(
                        artifacts: 'bin/graphmcp, graphmcp-*.tar.gz',
                        allowEmptyArchive: true
                    )
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 8: SonarQube 静态分析（仅在 main/dev 分支且启用时）
        // ═══════════════════════════════════════════════════════════
        stage('SonarQube Analysis') {
            when {
                anyOf {
                    branch 'main'
                    branch 'dev'
                }
                beforeAgent true
            }
            stages {
                stage('SonarQube — 配置检查') {
                    steps {
                        script {
                            env.SKIP_SONAR = 'true'   // 默认跳过

                            if (params.SONAR_ENABLED != true) {
                                echo '[SonarQube] SONAR_ENABLED 未开启，跳过分析'
                                return
                            }

                            // 尝试从 Credentials 解析 Token
                            try {
                                withCredentials([
                                    string(credentialsId: 'SONAR_TOKEN', variable: 'TOKEN'),
                                    string(credentialsId: 'SONAR_HOST_URL', variable: 'HOST')
                                ]) {
                                    env.SONAR_TOKEN_VALUE      = TOKEN
                                    env.SONAR_HOST_URL_VALUE   = HOST
                                }
                            } catch (Exception _e) {
                                echo "[SonarQube] WARNING: 缺少 SONAR_TOKEN 或 SONAR_HOST_URL credential，已跳过分析"
                                env.SKIP_SONAR = 'true'
                                return
                            }

                            if (!env.SONAR_TOKEN_VALUE || !env.SONAR_HOST_URL_VALUE) {
                                echo '[SonarQube] WARNING: SONAR_TOKEN 或 SONAR_HOST_URL 为空，已跳过分析'
                                env.SKIP_SONAR = 'true'
                                return
                            }

                            env.SKIP_SONAR = 'false'
                        }
                    }
                }

                stage('SonarQube — Build Wrapper') {
                    when { expression { env.SKIP_SONAR == 'false' } }
                    steps {
                        sh '''
                            curl -fsSL "${SONAR_HOST_URL_VALUE}/static/cpp/build-wrapper-linux-x86.zip" \
                                -o build-wrapper.zip
                            unzip -q build-wrapper.zip
                            rm -rf bw-output
                            ./build-wrapper-linux-x86/build-wrapper-linux-x86-64 \
                                --out-dir bw-output \
                                make all CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"
                        '''
                    }
                }

                stage('SonarQube — SonarScanner') {
                    when { expression { env.SKIP_SONAR == 'false' } }
                    steps {
                        withSonarQubeEnv(installationName: params.SONAR_INSTALLATION_NAME) {
                            sh 'sonar-scanner'
                        }
                    }
                }

                stage('SonarQube — Quality Gate') {
                    when { expression { env.SKIP_SONAR == 'false' } }
                    steps {
                        timeout(time: 10, unit: 'MINUTES') {
                            waitForQualityGate abortPipeline: true
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════
        // Stage 9: GitLab 镜像同步（仅 main 分支 + 前序成功）
        // ═══════════════════════════════════════════════════════════
        stage('GitLab Mirror') {
            when {
                branch 'main'
                beforeAgent true
            }
            steps {
                script {
                    if (params.GITLAB_MIRROR_ENABLED != true || !params.GITLAB_MIRROR_URL) {
                        echo '[GitLab Mirror] 未启用，跳过同步'
                        return
                    }

                    withCredentials([
                        string(credentialsId: 'GITLAB_MIRROR_TOKEN', variable: 'GITLAB_TOKEN')
                    ]) {
                        sh '''
                            set -euo pipefail

                            if [ -z "${GITLAB_TOKEN:-}" ]; then
                                echo "WARNING: GITLAB_MIRROR_TOKEN credential 已绑定但值为空，跳过同步"
                                exit 0
                            fi

                            MIRROR_URL="${GITLAB_MIRROR_URL}"
                            AUTH_URL="${MIRROR_URL}"
                            AUTH_URL="${AUTH_URL/https:\\/\\//https:\\/\\/oauth2:${GITLAB_TOKEN}@}"
                            AUTH_URL="${AUTH_URL/http:\\/\\//http:\\/\\/oauth2:${GITLAB_TOKEN}@}"

                            git remote remove gitlab-backup 2>/dev/null || true
                            git remote add gitlab-backup "${AUTH_URL}"

                            push_ok=0
                            for attempt in 1 2; do
                                if git push gitlab-backup HEAD:main; then
                                    echo "GitLab 镜像同步成功（第 ${attempt} 次）"
                                    push_ok=1
                                    break
                                fi
                                echo "第 ${attempt} 次镜像同步失败"
                                if [ "${attempt}" -lt 2 ]; then
                                    echo "等待 5 秒后重试..."
                                    sleep 5
                                fi
                            done

                            if [ "${push_ok}" -ne 1 ]; then
                                echo "WARNING: GitLab 镜像同步连续 2 次失败，不阻断主流水线"
                            fi
                        '''
                    }
                }
            }
        }
    }

    // ── 善后：总是执行的清理 ─────────────────────────────────────
    post {
        cleanup {
            echo '清理工作空间（保留制品归档不受影响）'
            cleanWs(
                deleteDirs: true,
                patterns: [
                    [pattern: 'ci-store/**', type: 'INCLUDE'],
                    [pattern: 'smoke-test-store-*/**', type: 'INCLUDE'],
                    [pattern: 'test-store-tmp/**', type: 'INCLUDE'],
                    [pattern: 'bw-output/**', type: 'INCLUDE'],
                    [pattern: 'build-wrapper*/**', type: 'INCLUDE'],
                    [pattern: 'dist/**', type: 'INCLUDE'],
                ]
            )
        }
    }
}
