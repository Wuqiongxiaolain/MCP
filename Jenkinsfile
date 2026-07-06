// Jenkins declarative pipeline for graphmcp
// Stages: checkout -> build -> unit tests -> SonarQube analysis -> package -> deploy (Ansible)
pipeline {
    agent any

    options {
        timestamps()
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }

    environment {
        SONARQUBE_SERVER = 'sonarqube'          // Jenkins "SonarQube servers" config name
        ARTIFACT_DIR     = 'dist'
    }

    stages {
        stage('Checkout') {
            steps {
                checkout scm
            }
        }

        stage('Build') {
            steps {
                sh 'make clean || true'
                sh 'make all CXXFLAGS="-std=c++17 -O2 -Wall -Wextra"'
            }
        }

        stage('Unit Tests') {
            steps {
                sh 'make test'
            }
        }

        stage('Smoke Test (CLI + MCP)') {
            steps {
                // end-to-end: create from mermaid example, export drawio + svg,
                // then drive the MCP stdio server with a scripted session
                sh '''
                    export GRAPHMCP_STORE=ci-store
                    ./bin/graphmcp create --input examples/flowchart.mmd --name ci-smoke
                    GID=$(./bin/graphmcp list | head -n1 | awk '{print $1}')
                    ./bin/graphmcp export --id "$GID" --to drawio -o ci-smoke.drawio
                    ./bin/graphmcp export --id "$GID" --to svg    -o ci-smoke.svg
                    printf '%s\\n%s\\n' \
                      '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
                      '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
                      | ./bin/graphmcp serve | grep -q graph_create
                '''
            }
        }

        stage('SonarQube Analysis') {
            steps {
                withSonarQubeEnv("${SONARQUBE_SERVER}") {
                    sh 'sonar-scanner -Dproject.settings=sonar-project.properties'
                }
            }
        }

        stage('Quality Gate') {
            steps {
                timeout(time: 10, unit: 'MINUTES') {
                    waitForQualityGate abortPipeline: true
                }
            }
        }

        stage('Package') {
            steps {
                sh '''
                    mkdir -p ${ARTIFACT_DIR}
                    cp bin/graphmcp ${ARTIFACT_DIR}/
                    cp README.md ${ARTIFACT_DIR}/
                    tar czf graphmcp-${BUILD_NUMBER}.tar.gz -C ${ARTIFACT_DIR} .
                '''
                archiveArtifacts artifacts: 'graphmcp-*.tar.gz', fingerprint: true
            }
        }

        stage('Deploy') {
            when { branch 'main' }
            steps {
                sh '''
                    ansible-playbook -i ansible/inventory.ini ansible/deploy.yml \
                        -e "artifact=$(pwd)/graphmcp-${BUILD_NUMBER}.tar.gz"
                '''
            }
        }
    }

    post {
        always {
            cleanWs(deleteDirs: true, patterns: [
                [pattern: 'ci-store/**', type: 'INCLUDE'],
                [pattern: 'test-store-tmp/**', type: 'INCLUDE']
            ])
        }
        failure {
            echo 'Build failed - check console output and SonarQube report.'
        }
    }
}
