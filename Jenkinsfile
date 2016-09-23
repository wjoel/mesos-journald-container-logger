node {
  stage 'Checkout'
  checkout scm

  stage 'Build Mesos'
  sh "./build-mesos.sh"

  stage 'Compile'
  sh "./compile.sh"

  stage 'Test'
  sh "./mesos-journald-container-logger-tests"
}
