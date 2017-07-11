#!/bin/bash

set -e
#set -x

THIS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

runtime=10
pool=zlog
nclients="1 2"
entry_sizes="0 10 100"
width=16

tmpdir=$(mktemp -d)
trap "rm -rf $tmpdir" EXIT

count=0
ts=`date +"%s"`
for clients in $nclients; do
  for entrysize in $entry_sizes; do
    count=$((count+1))
    runid="ts-${ts}.run-${count}"
    for i in `seq 1 $clients`; do
      tracefn="$runid.nc-$clients.c-$i.es-$entrysize.w-$width.log"
      args="--runtime $runtime --entry_size $entrysize \
            --pool $pool --width $width --trace $tracefn"
      #args="--backend ceph --pool $pool $args"

      lmdbdir=$tmpdir/db.$i
      rm -rf $lmdbdir
      mkdir $lmdbdir
      args="--backend lmdb --lmdbdir $lmdbdir $args"

      echo $args
      ${THIS_DIR}/zlog-append-bench $args &
    done
    wait
  done
done
