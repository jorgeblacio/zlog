#!/bin/bash

pool="a"

# for zlog ceph v1 the width will be fixed to something reasonable like num_pgs
# since only a single stripe is created. for zlog ceph v2 we want to create a
# stripe that is large enough to handle all the data for the experiment. this is
# because in the current beta version creating a new stripe interrupts execution
# creating throughput and latency issues.

#max_entry_size=
width=1000
entries_per_object="1024"
runtime="12"
entry_sizes="100 1000"
queue_depths="2 4"
cache_size="0 1024 65536"
cache_eviction="0 1"

export ZLOG_LMDB_BE_SIZE=20

scanv=""

if [ "$1" != "scan" ] ; then
  rm -rf /tmp/zlog.tmp.db
  mkdir /tmp/zlog.tmp.db
  ./bin/create_log_lmdb
else
  scanv="--scan"
fi



for esize in ${entry_sizes}; do
  for qdepth in ${queue_depths}; do
    for csize in ${cache_size}; do
      for cevic in ${cache_eviction}; do
        prefix="es-${esize}.qd-${qdepth}.w-${width}.cs-${csize}.ep-${cevic}"
        bin/zlog_bench2 --pool ${pool} --width ${width} \
          --size ${esize} --qdepth ${qdepth} \
          --runtime ${runtime} --prefix ${prefix} \
          --cache_size ${csize} --cache_eviction ${cevic} \
          --epo ${entries_per_object} --backend lmdb --logname ${prefix} \
          ${scanv}
      done
    done
  done
done
