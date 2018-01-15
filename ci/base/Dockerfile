ARG BASE
FROM ${BASE}

# build dependencies
COPY debian /src/zlog/debian
COPY ci/install-ceph.sh /src/zlog/ci/
COPY zlog.spec.in install-deps.sh /src/zlog/
RUN /src/zlog/install-deps.sh && \
    /src/zlog/ci/install-ceph.sh

# build and install
COPY . /src/zlog
RUN /src/zlog/ci/install-zlog.sh
