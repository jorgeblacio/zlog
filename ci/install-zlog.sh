#!/bin/bash
set -e
set -x

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZLOG_DIR=${THIS_DIR}/../

BUILD_DIR=$(mktemp -d)
trap "rm -rf ${BUILD_DIR}" EXIT

CMAKE_BUILD_TYPE=Debug

CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
             -DWITH_JNI=ON \
             -DCMAKE_INSTALL_PREFIX=/usr"

pushd ${BUILD_DIR}
cmake ${CMAKE_FLAGS} ${ZLOG_DIR}
make -j$(nproc)
make install
popd

# stash the java test dependencies. there has got to be a better way to do this,
# but it probably depends on switching over to use maven...
mkdir /src/zlog-java-deps
mv ${BUILD_DIR}/src/java/test-libs/*.jar /src/zlog-java-deps/
