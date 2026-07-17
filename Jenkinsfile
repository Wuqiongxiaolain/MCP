// Jenkinsfile - 对齐 .github/workflows/ci.yml（主链）与 deploy.yml
// 不含：SonarQube、GitLab 镜像、bump-version、update-bench-baseline
//
// Agent 标签：
//   linux  —— CI / CD Linux（必备）
//   macos  —— CD macOS（可选，关闭参数 CD_BUILD_MACOS）
//   windows —— CD Windows（可选；需 MSYS2 于 C:\\msys64，对齐 deploy.yml）
//
// 凭据（仅非 dry-run 发版）：Secret text，ID = github-token
// 发版 Agent 需安装 GitHub CLI：https://cli.github.com/

pipeline {
  agent none

  options {
    timestamps()
    // CI 对齐 cancel-in-progress；CD 标签构建不中止进行中的任务更安全
    disableConcurrentBuilds(abortPrevious: false)
    timeout(time: 45, unit: 'MINUTES')
  }

  parameters {
    choice(
      name: 'PIPELINE_MODE',
      choices: ['auto', 'ci', 'cd'],
      description: 'auto：分支→CI，标签 v*→CD；也可强制 ci / cd'
    )
    booleanParam(
      name: 'CD_DRY_RUN',
      defaultValue: true,
      description: 'CD：仅构建+归档，不创建 GitHub Release（对齐 deploy.yml dry_run）'
    )
    string(
      name: 'CD_REF',
      defaultValue: '',
      description: 'CD 检出的分支/commit（留空=当前 Multibranch 检出）'
    )
    booleanParam(
      name: 'CD_BUILD_MACOS',
      defaultValue: false,
      description: 'CD 是否构建 macOS（需 Agent 标签 macos）'
    )
    booleanParam(
      name: 'CD_BUILD_WINDOWS',
      defaultValue: false,
      description: 'CD 是否构建 Windows（需 Agent 标签 windows + MSYS2）'
    )
    booleanParam(
      name: 'DO_DEPLOY',
      defaultValue: true,
      description: '构建成功后发布制品到 nginx 下载站（经 Ansible）'
    )
  }

  environment {
    CI = 'true'
    CXXFLAGS = '-std=c++17 -O2 -Wall -Wextra'
  }

  stages {
    stage('Resolve mode') {
      agent { label 'linux' }
      steps {
        script {
          // 步骤：根据参数与分支/标签名推断跑 CI 还是 CD
          def mode = params.PIPELINE_MODE ?: 'auto'
          def branch = env.BRANCH_NAME ?: env.GIT_BRANCH ?: ''
          if (branch.startsWith('origin/')) {
            branch = branch.substring(7)
          }
          def tag = env.TAG_NAME ?: ''
          if (!tag && (branch ==~ /^v\d.*/)) {
            tag = branch
          }

          if (mode == 'auto') {
            mode = (tag || (branch ==~ /^v\d.*/)) ? 'cd' : 'ci'
          }

          env.GRAPHMCP_PIPELINE_MODE = mode
          env.GRAPHMCP_TAG_NAME = tag ? tag : "build-${env.BUILD_NUMBER}"
          env.GRAPHMCP_IS_TAG = tag ? 'true' : 'false'
          echo "mode=${env.GRAPHMCP_PIPELINE_MODE} tag=${env.GRAPHMCP_TAG_NAME} dry_run=${params.CD_DRY_RUN}"
        }
      }
    }

    // -------------------- CI：对齐 ci.yml → build-test-package --------------------
    stage('CI') {
      when {
        beforeAgent true
        expression { env.GRAPHMCP_PIPELINE_MODE == 'ci' }
      }
      agent { label 'linux' }
      options { timeout(time: 15, unit: 'MINUTES') }
      stages {
        stage('Install deps') {
          steps {
            sh '''
              set -euo pipefail
              # 步骤：已有工具则跳过；否则用 root / sudo 装依赖（Jenkins 官方镜像默认无 sudo）
              # rsvg-convert：SMOKE_REQUIRE_RASTER=1 需要真实 PNG/PDF（ImageMagick 在 Debian 上常禁 SVG）
              missing=0
              for c in g++ make python3 jq rsvg-convert; do
                command -v "$c" >/dev/null 2>&1 || missing=1
              done
              command -v convert >/dev/null 2>&1 || command -v magick >/dev/null 2>&1 || missing=1
              if [ "$missing" -eq 0 ]; then
                echo "依赖已就绪，跳过安装"
                exit 0
              fi
              if ! command -v apt-get >/dev/null 2>&1; then
                echo "非 apt 环境：请预先安装 g++ make python3 imagemagick librsvg2-bin jq"
                exit 1
              fi
              if [ "$(id -u)" -eq 0 ]; then
                apt-get update
                apt-get install -y g++ make python3 imagemagick librsvg2-bin jq
              elif command -v sudo >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y g++ make python3 imagemagick librsvg2-bin jq
              else
                echo "无 root/sudo：请在 Agent 预装依赖，或为 jenkins 配置免密 sudo"
                exit 1
              fi
            '''
          }
        }

        stage('Build') {
          steps {
            sh 'make all CXXFLAGS="${CXXFLAGS}"'
          }
        }

        stage('OpenAPI drift check') {
          steps {
            // 原理：用 dump-tools 重写 openapi 再 diff，防止契约与 toolList 漂移
            sh '''
              set -euo pipefail
              make docs-api
              git diff --exit-code -- docs/api_reference/openapi.yaml
            '''
          }
        }

        stage('Unit tests') {
          steps {
            sh '''
              set -euo pipefail
              mkdir -p docs/ci_results
              bash scripts/ci_capture.sh unit-main bin/graphmcp_tests
              bash scripts/ci_capture.sh unit-version bin/graphmcp_version_tests
              bash scripts/ci_capture.sh unit-cursor bin/graphmcp_cursor_tests
            '''
          }
        }

        stage('Smoke') {
          environment {
            SMOKE_REQUIRE_RASTER = '1'
            GRAPHMCP_LOG = 'info'
          }
          steps {
            sh 'bash scripts/ci_capture.sh smoke bash tests/smoke_test.sh bin/graphmcp'
            sh 'bash scripts/ci_capture.sh mcp-smoke bash tests/mcp_smoke.sh bin/graphmcp'
            sh 'bash scripts/ci_capture.sh table-smoke bash tests/table_smoke.sh bin/graphmcp'
            sh 'bash scripts/ci_capture.sh perf-smoke python3 scripts/mcp_perf_smoke.py bin/graphmcp'
          }
        }

        stage('Bench') {
          environment {
            CI = 'true'
          }
          steps {
            // 失败告警并重试，最多 3 次；连续 3 次失败才阻断（scripts/bench_ci_retry.sh）
            sh 'bash scripts/ci_capture.sh bench make bench-ci CXXFLAGS="${CXXFLAGS}"'
          }
        }

        stage('Export regression') {
          steps {
            sh 'bash scripts/ci_capture.sh export-testout bash scripts/export-example-testout.sh bin/graphmcp'
          }
        }

        stage('Assemble test report') {
          steps {
            // 只组装，不重跑；失败不阻断（报告仍 archive）
            sh 'python3 scripts/generate_docs_test_report.py --from-ci || true'
          }
        }

        stage('Package') {
          steps {
            sh '''
              set -euo pipefail
              mkdir -p dist
              cp bin/graphmcp dist/
              cp README.md VERSION dist/
              tar czf "graphmcp-${BUILD_NUMBER}.tar.gz" -C dist .
              # 写入共享卷，供 Ansible/nginx 下载站发布
              mkdir -p /artifacts
              cp -f "graphmcp-${BUILD_NUMBER}.tar.gz" /artifacts/
              ls -la /artifacts/graphmcp-${BUILD_NUMBER}.tar.gz
            '''
          }
        }
      }
      post {
        always {
          // 即使前面 stage 失败也尝试组装一次（幂等）
          sh 'python3 scripts/generate_docs_test_report.py --from-ci || true'
          archiveArtifacts artifacts: 'docs/TEST_REPORT.md,docs/TEST_REPORT.json,docs/SMOKE_REPORT.md,docs/images/test-report-summary.svg', allowEmptyArchive: true
          archiveArtifacts artifacts: 'docs/ci_results/**', allowEmptyArchive: true
          archiveArtifacts artifacts: 'bin/bench_result.json', allowEmptyArchive: true, fingerprint: true
          archiveArtifacts artifacts: 'mcp-smoke.log', allowEmptyArchive: true
          archiveArtifacts artifacts: 'examples/example_testout/TEST_REPORT.md,examples/example_testout/TEST_REPORT.json', allowEmptyArchive: true
          archiveArtifacts artifacts: "bin/graphmcp,graphmcp-${env.BUILD_NUMBER}.tar.gz", allowEmptyArchive: true, fingerprint: true
        }
      }
    }

    // -------------------- CD：对齐 deploy.yml → build-package（并行）--------------------
    stage('CD Build') {
      when {
        beforeAgent true
        expression { env.GRAPHMCP_PIPELINE_MODE == 'cd' }
      }
      failFast false
      parallel {
        stage('Linux') {
          agent { label 'linux' }
          options { timeout(time: 25, unit: 'MINUTES') }
          stages {
            stage('Deps') {
              steps {
                sh '''
                  set -euo pipefail
                  if command -v g++ >/dev/null 2>&1 && command -v make >/dev/null 2>&1; then
                    echo "依赖已就绪，跳过安装"
                    exit 0
                  fi
                  if ! command -v apt-get >/dev/null 2>&1; then
                    exit 0
                  fi
                  if [ "$(id -u)" -eq 0 ]; then
                    apt-get update
                    apt-get install -y g++ make
                  elif command -v sudo >/dev/null 2>&1; then
                    sudo apt-get update
                    sudo apt-get install -y g++ make
                  else
                    echo "无 root/sudo：请在 Agent 预装 g++ make"
                    exit 1
                  fi
                '''
              }
            }
            stage('Checkout ref') {
              when { expression { !(params.CD_REF ?: '').trim().isEmpty() } }
              steps {
                checkout([
                  $class: 'GitSCM',
                  branches: [[name: params.CD_REF.trim()]],
                  userRemoteConfigs: scm.userRemoteConfigs
                ])
              }
            }
            stage('Build & test') {
              steps {
                sh '''
                  set -euo pipefail
                  make all CXXFLAGS="${CXXFLAGS}"
                  make test CXXFLAGS="${CXXFLAGS}"
                '''
              }
            }
            stage('Package') {
              steps {
                sh '''
                  set -euo pipefail
                  mkdir -p dist
                  cp bin/graphmcp dist/
                  cp README.md VERSION dist/
                  if [ -d skills/graphmcp ]; then
                    mkdir -p dist/skills
                    cp -R skills/graphmcp dist/skills/
                  fi
                  tar czf "graphmcp-${GRAPHMCP_TAG_NAME}-linux-x64.tar.gz" -C dist .
                  # 写入共享卷，供 Ansible/nginx 下载站发布
                  mkdir -p /artifacts
                  cp -f graphmcp-*-linux-x64.tar.gz /artifacts/
                  ls -la /artifacts/graphmcp-*-linux-x64.tar.gz
                '''
                stash name: 'cd-linux', includes: 'graphmcp-*-linux-x64.tar.gz'
                archiveArtifacts artifacts: 'graphmcp-*-linux-x64.tar.gz', fingerprint: true
              }
            }
          }
        }

        stage('macOS') {
          when {
            beforeAgent true
            expression { params.CD_BUILD_MACOS == true }
          }
          agent { label 'macos' }
          options { timeout(time: 25, unit: 'MINUTES') }
          stages {
            stage('Checkout ref') {
              when { expression { !(params.CD_REF ?: '').trim().isEmpty() } }
              steps {
                checkout([
                  $class: 'GitSCM',
                  branches: [[name: params.CD_REF.trim()]],
                  userRemoteConfigs: scm.userRemoteConfigs
                ])
              }
            }
            stage('Build & test') {
              steps {
                sh '''
                  set -euo pipefail
                  make all CXXFLAGS="${CXXFLAGS}"
                  make test CXXFLAGS="${CXXFLAGS}"
                '''
              }
            }
            stage('Package') {
              steps {
                sh '''
                  set -euo pipefail
                  mkdir -p dist
                  cp bin/graphmcp dist/
                  cp README.md VERSION dist/
                  if [ -d skills/graphmcp ]; then
                    mkdir -p dist/skills
                    cp -R skills/graphmcp dist/skills/
                  fi
                  tar czf "graphmcp-${GRAPHMCP_TAG_NAME}-macos-x64.tar.gz" -C dist .
                '''
                stash name: 'cd-macos', includes: 'graphmcp-*-macos-x64.tar.gz'
                archiveArtifacts artifacts: 'graphmcp-*-macos-x64.tar.gz', fingerprint: true
              }
            }
          }
        }

        stage('Windows') {
          when {
            beforeAgent true
            expression { params.CD_BUILD_WINDOWS == true }
          }
          agent { label 'windows' }
          options { timeout(time: 25, unit: 'MINUTES') }
          stages {
            stage('Checkout ref') {
              when { expression { !(params.CD_REF ?: '').trim().isEmpty() } }
              steps {
                checkout([
                  $class: 'GitSCM',
                  branches: [[name: params.CD_REF.trim()]],
                  userRemoteConfigs: scm.userRemoteConfigs
                ])
              }
            }
            stage('Build & test') {
              // 对齐 deploy.yml：MSYS2 MinGW64 + 静态链接；默认路径 C:\\msys64
              steps {
                bat '''
                  set CHERE_INVOKING=1
                  C:\\msys64\\usr\\bin\\bash.exe -lc "mkdir -p bin && make all CXXFLAGS='-std=c++17 -O2 -Wall -Wextra' 'MKDIR=mkdir -p $(BIN)' EXE=.exe 'STATIC=-static -static-libgcc -static-libstdc++'"
                  C:\\msys64\\usr\\bin\\bash.exe -lc "mkdir -p bin && make test CXXFLAGS='-std=c++17 -O2 -Wall -Wextra' 'MKDIR=mkdir -p $(BIN)' EXE=.exe"
                '''
              }
            }
            stage('Package') {
              steps {
                powershell '''
                  New-Item -ItemType Directory -Path dist -Force | Out-Null
                  Copy-Item "bin/graphmcp.exe" "dist/graphmcp.exe"
                  Copy-Item "README.md" "dist/README.md"
                  Copy-Item "VERSION" "dist/VERSION"
                  if (Test-Path "skills/graphmcp") {
                    New-Item -ItemType Directory -Path "dist/skills" -Force | Out-Null
                    Copy-Item -Recurse "skills/graphmcp" "dist/skills/graphmcp"
                  }
                  $asset_name = "graphmcp-$($env:GRAPHMCP_TAG_NAME)-windows-x64.zip"
                  if (Test-Path $asset_name) { Remove-Item $asset_name -Force }
                  Compress-Archive -Path "dist/*" -DestinationPath $asset_name
                '''
                stash name: 'cd-windows', includes: 'graphmcp-*-windows-x64.zip'
                archiveArtifacts artifacts: 'graphmcp-*-windows-x64.zip', fingerprint: true
              }
            }
          }
        }
      }
    }

    // -------------------- CD：对齐 deploy.yml → release（非 dry-run + 真 tag）--------------------
    stage('CD Release') {
      when {
        beforeAgent true
        allOf {
          expression { env.GRAPHMCP_PIPELINE_MODE == 'cd' }
          expression { params.CD_DRY_RUN == false }
          expression { env.GRAPHMCP_IS_TAG == 'true' }
        }
      }
      agent { label 'linux' }
      options { timeout(time: 10, unit: 'MINUTES') }
      steps {
        withCredentials([string(credentialsId: 'github-token', variable: 'GH_TOKEN')]) {
          // unstash 是 Pipeline 步骤，不能写在 sh 脚本里
          unstash 'cd-linux'
          script {
            try {
              unstash 'cd-macos'
            } catch (ignored) {
              echo '无 macOS stash，跳过'
            }
            try {
              unstash 'cd-windows'
            } catch (ignored) {
              echo '无 Windows stash，跳过'
            }
          }
          sh '''
            set -euo pipefail
            mkdir -p release-assets
            # 仅 Linux 必有；macOS/Windows 可能未构建
            mv -f graphmcp-*-linux-x64.tar.gz release-assets/ 2>/dev/null || true
            mv -f graphmcp-*-macos-x64.tar.gz release-assets/ 2>/dev/null || true
            mv -f graphmcp-*-windows-x64.zip release-assets/ 2>/dev/null || true
            ls -la release-assets/

            NOTE_FILE=release-notes.md
            : > "$NOTE_FILE"
            git tag -l --format='%(contents)' "${GRAPHMCP_TAG_NAME}" > "$NOTE_FILE" || true
            if [ ! -s "$NOTE_FILE" ]; then
              printf '%s\n' "graphmcp ${GRAPHMCP_TAG_NAME}" "" "（无 annotated tag message。发版请使用 git tag -a 并填写说明。）" > "$NOTE_FILE"
            fi
            {
              echo ""
              echo "---"
              echo ""
              echo "制品由 Jenkins CD 自动构建上传。"
              # 用变量拼 markdown 反引号，避免命令替换，也避免 Groovy 转义问题
              BT='`'
              echo "- Tag：${BT}${GRAPHMCP_TAG_NAME}${BT}"
              echo "- Build：${BT}${BUILD_NUMBER}${BT}"
              echo "- Commit：${BT}${GIT_COMMIT}${BT}"
              echo "- 平台：以 release-assets 内实际文件为准"
            } >> "$NOTE_FILE"

            if ! command -v gh >/dev/null 2>&1; then
              echo "未安装 gh，跳过 Release；制品已由 archiveArtifacts 保留"
              exit 0
            fi

            # 原理：用 find 判空，避免 bash 的 shopt/数组（Jenkins sh 多为 dash）
            if [ -z "$(find release-assets -type f -print -quit)" ]; then
              echo "没有可上传制品"
              exit 1
            fi

            if gh release view "${GRAPHMCP_TAG_NAME}" >/dev/null 2>&1; then
              gh release upload "${GRAPHMCP_TAG_NAME}" release-assets/* --clobber
            else
              gh release create "${GRAPHMCP_TAG_NAME}" release-assets/* --title "graphmcp ${GRAPHMCP_TAG_NAME}" --notes-file "$NOTE_FILE" --generate-notes --target "${GIT_COMMIT}"
            fi
          '''
        }
      }
    }

    // -------------------- 发布：共享卷制品 → Ansible → nginx 下载站 --------------------
    stage('Deploy download-server') {
      when {
        beforeAgent true
        allOf {
          expression { params.DO_DEPLOY == true }
          anyOf {
            expression { env.GRAPHMCP_PIPELINE_MODE == 'ci' }
            expression { env.GRAPHMCP_PIPELINE_MODE == 'cd' }
          }
        }
      }
      agent { label 'linux' }
      options { timeout(time: 10, unit: 'MINUTES') }
      steps {
        // 原理：Jenkins 已把 tar 写入 /artifacts；经 docker.sock 在 ansible-runner 容器内执行 playbook
        // 注意：sh ''' 内禁止 \\( \\) 等 Groovy 非法转义；用两次 find，兼容 dash
        sh '''
          set -euo pipefail
          echo "=== /artifacts 当前制品 ==="
          ls -la /artifacts || true
          found_tar="$(find /artifacts -maxdepth 1 -name 'graphmcp*.tar.gz' -print -quit)"
          found_zip="$(find /artifacts -maxdepth 1 -name 'graphmcp*.zip' -print -quit)"
          if [ -z "$found_tar" ] && [ -z "$found_zip" ]; then
            echo "共享卷中无 graphmcp 制品，无法发布"
            exit 1
          fi
          if ! command -v docker >/dev/null 2>&1; then
            echo "Jenkins 容器内无 docker 客户端，无法触发 Ansible 发布"
            exit 1
          fi
          docker exec ansible-runner ansible-playbook -i /ansible-projects/MCP-/ansible/inventories/docker.yml /ansible-projects/MCP-/ansible/playbooks/deploy_release.yml
          echo "发布完成：请访问 http://localhost:8081/"
        '''
      }
    }
  }

  post {
    success { echo "OK mode=${env.GRAPHMCP_PIPELINE_MODE} #${env.BUILD_NUMBER}" }
    failure { echo "FAIL mode=${env.GRAPHMCP_PIPELINE_MODE} #${env.BUILD_NUMBER}" }
  }
}
