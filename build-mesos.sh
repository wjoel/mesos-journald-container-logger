#!/bin/bash

set -e

MESOS_VERSION="${MESOS_VERSION:-1.0.1}"
MESOS_INSTALL_DEPS="${MESOS_INSTALL_DEPS:-false}"

if [ ! -f mesos-$MESOS_VERSION.tar.gz ]; then
    curl --remote-name http://archive.apache.org/dist/mesos/$MESOS_VERSION/mesos-$MESOS_VERSION.tar.gz
fi

if [ ! -d mesos-$MESOS_VERSION ]; then
    tar xf mesos-$MESOS_VERSION.tar.gz
fi

cd mesos-$MESOS_VERSION
if [ ! -d build ]; then
    mkdir build
fi

if [[ ${MESOS_INSTALL_DEPS} != "false" ]]; then
  sudo apt-get -yq install curl libz-dev libsystemd-dev
  sudo apt-get -yq install build-essential python-dev libcurl4-nss-dev libsasl2-dev libsasl2-modules maven libapr1-dev libsvn-dev openjdk-8-jdk-headless openjdk-8-jre-headless cmake
fi;

if [ ! -d ../mesos-install ]; then
  cd build
  ../configure --prefix=$PWD/../../mesos-install --enable-install-module-dependencies
  make -j $(( $(grep -c processor /proc/cpuinfo)-1 )) V=0
  make install
fi
