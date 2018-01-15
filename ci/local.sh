#!/bin/bash
set -e

images="
fedora:27
ubuntu:xenial
"

for image in ${images}; do
  name="zlog/deps_${image/:/_}"
  docker build -t ${name} --build-arg BASE=${image} \
    -f ci/base/Dockerfile .
  docker run -w /src/zlog ${name} ci/run-tests.sh
done
