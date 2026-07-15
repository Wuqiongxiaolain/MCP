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
              if command -v apt-get >/dev/null 2>&1; then
                sudo apt-get update
                sudo apt-get install -y g++ make python3 imagemagick jq
              else
                echo "非 apt 环境：请预先安装 g++ make python3 imagemagick jq"
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
            sh 'make test-all'
          }
        }

        stage('Bench') {
          steps {
            sh 'make bench-ci CXXFLAGS="${CXXFLAGS}"'
          }
        }

        stage('Smoke') {
          environment {
            SMOKE_REQUIRE_RASTER = '1'
            GRAPHMCP_LOG = 'info'
          }
          steps {
            sh 'make smoke'
            sh 'make mcp-smoke'
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
            '''
          }
        }
      }
      post {
        always {
          archiveArtifacts artifacts: 'bin/bench_result.json', allowEmptyArchive: true, fingerprint: true
          archiveArtifacts artifacts: 'SMOKE_REPORT.md,mcp-smoke.log', allowEmptyArchive: true
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
                  if command -v apt-get >/dev/null 2>&1; then
                    sudo apt-get update
                    sudo apt-get install -y g++ make
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
          sh '''
            set -euo pipefail
            mkdir -p release-assets
            # 仅 Linux 必有；macOS/Windows 可能未构建
            unstash 'cd-linux'
            unstash 'cd-macos' || true
            unstash 'cd-windows' || true
            mv -f graphmcp-*-linux-x64.tar.gz release-assets/ 2>/dev/null || true
            mv -f graphmcp-*-macos-x64.tar.gz release-assets/ 2>/dev/null || true
            mv -f graphmcp-*-windows-x64.zip release-assets/ 2>/dev/null || true
            ls -la release-assets/

            NOTE_FILE=release-notes.md
            : > "$NOTE_FILE"
            git tag -l --format='%(contents)' "${GRAPHMCP_TAG_NAME}" > "$NOTE_FILE" || true
            if [ ! -s "$NOTE_FILE" ]; then
              printf '%s\n' \
                "graphmcp ${GRAPHMCP_TAG_NAME}" \
                "" \
                "（无 annotated tag message。发版请使用 git tag -a 并填写说明。）" \
                > "$NOTE_FILE"
            fi
            {
              echo ""
              echo "---"
              echo ""
              echo "制品由 Jenkins CD 自动构建上传。"
              # 用变量拼 markdown 反引号，避免 bash 命令替换，也避免 Groovy 转义问题
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

            shopt -s nullglob
            assets=(release-assets/*)
            if [ "${#assets[@]}" -eq 0 ]; then
              echo "没有可上传制品"
              exit 1
            fi

            if gh release view "${GRAPHMCP_TAG_NAME}" >/dev/null 2>&1; then
              gh release upload "${GRAPHMCP_TAG_NAME}" "${assets[@]}" --clobber
            else
              gh release create "${GRAPHMCP_TAG_NAME}" \
                "${assets[@]}" \
                --title "graphmcp ${GRAPHMCP_TAG_NAME}" \
                --notes-file "$NOTE_FILE" \
                --generate-notes \
                --target "${GIT_COMMIT}"
            fi
          '''
        }
      }
    }
  }

  post {
    success { echo "OK mode=${env.GRAPHMCP_PIPELINE_MODE} #${env.BUILD_NUMBER}" }
    failure { echo "FAIL mode=${env.GRAPHMCP_PIPELINE_MODE} #${env.BUILD_NUMBER}" }
  }
}
