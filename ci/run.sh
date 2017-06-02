#!/bin/bash

set -e
set -x

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZLOG_DIR=${THIS_DIR}/../

# setup build directories
BUILD_DIR=$(mktemp -d)
DOCS_DIR=$(mktemp -d)
INSTALL_DIR=$(mktemp -d)
trap "rm -rf ${BUILD_DIR} ${DOCS_DIR} ${INSTALL_DIR}" EXIT

# build documentation
${ZLOG_DIR}/doc/build.sh ${DOCS_DIR}
test -r ${DOCS_DIR}/output/html/index.html

CMAKE_BUILD_TYPE=Debug
if [ "${RUN_COVERAGE}" == 1 ]; then
  CMAKE_BUILD_TYPE=Coverage
fi

# build and install zlog
pushd ${BUILD_DIR}
cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DWITH_JNI=1 \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${ZLOG_DIR} ${EXTRA_CMAKE_ARGS}
make -j$(nproc)
make install
popd

PATH=${INSTALL_DIR}/bin:$PATH

mkdir db

# ram backend tests
zlog-test-ram
zlog-db-test
zlog-test-lmdb
${BUILD_DIR}/src/kvstore/bench 10000

if [[ "$OSTYPE" != "darwin"* ]]; then
  pushd ${BUILD_DIR}/src/java

  export LD_LIBRARY_PATH=${INSTALL_DIR}/lib:$LD_LIBRARY_PATH
  # i'm giving up for the time being on how to fix a dynamic library loading
  # issue that is only showing up on debian jessie. see issue #143
  OS_ID=$(lsb_release -si)
  OS_CODE=$(lsb_release -sc)
  if [[ ${OS_ID} == "Debian" && ${OS_CODE} == "jessie" ]]; then
    export LD_LIBRARY_PATH=/usr/lib/jvm/java-7-openjdk-amd64/jre/lib/amd64/xawt/:$LD_LIBRARY_PATH
  fi

  export CP=${INSTALL_DIR}/share/java/zlog.jar
  export CP=${CP}:${INSTALL_DIR}/share/java/zlog-test.jar
  export CP=${CP}:${ZLOG_DIR}/src/java/test-libs/junit-4.12.jar
  export CP=${CP}:${ZLOG_DIR}/src/java/test-libs/hamcrest-core-1.3.jar
  export CP=${CP}:${ZLOG_DIR}/src/java/test-libs/assertj-core-1.7.1.jar

  mkdir db
  java -cp $CP org.junit.runner.JUnitCore com.cruzdb.AllTests

  popd
fi

# ceph backend tests
if [ ! -z ${CEPH_CONF} ]; then
  zlog-seqr --port 5678 --streams --daemon
  zlog-test-cls-zlog
  zlog-test-ceph
fi

if [ "${RUN_COVERAGE}" == 1 ]; then
  pushd ${BUILD_DIR}
  mkdir db
  for test in zlog-test-ram-cov zlog-db-test-cov zlog-test-lmdb-cov bench-cov; do
    make $test
    rm -rf coverage*
    lcov --directory . --capture --output-file coverage.info
    lcov --remove coverage.info '/usr/*' '*/googletest/*' '*.pb.cc' '*.pb.h' --output-file coverage2.info
    bash <(curl -s https://codecov.io/bash) -R ${ZLOG_DIR} -f coverage2.info || \
      echo "Codecov did not collect coverage reports" 
  done
  popd
fi