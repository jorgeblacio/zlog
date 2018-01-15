#!/bin/bash
set -e

name="zlog/deps_${IMAGE/:/_}"
docker build -t ${name} --build-arg BASE=${IMAGE} \
  -f ci/base/Dockerfile .
docker run -w /src/zlog ${name} ci/run-tests.sh
