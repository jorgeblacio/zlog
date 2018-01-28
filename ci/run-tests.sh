#!/bin/bash
set -e
set -x

# TODO: add coverage and ceph tests

zlog-seqr --port 5678 --streams --daemon

tests="zlog_test_backend_lmdb zlog_test_backend_ram"

if [ -f /etc/ceph/ceph.conf ]; then
  ceph --version || true
  ceph status || true
  tests="${tests} zlog_test_cls_zlog"
  tests="${tests} zlog_test_backend_ceph"
fi

for test_runner in $tests; do
  ${test_runner}
done

CLASSPATH=/usr/share/java/zlog.jar
CLASSPATH=${CLASSPATH}:/usr/share/java/zlog-test.jar
CLASSPATH=${CLASSPATH}:/src/zlog-java-deps/junit-4.12.jar
CLASSPATH=${CLASSPATH}:/src/zlog-java-deps/hamcrest-core-1.3.jar
CLASSPATH=${CLASSPATH}:/src/zlog-java-deps/assertj-core-1.7.1.jar

mkdir db
java -cp $CLASSPATH org.junit.runner.JUnitCore org.cruzdb.zlog.AllTests
