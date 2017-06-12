#!/bin/bash

set -e
set -x

runtime=10
width=32
nthreads=16
readpcts="0 25 50 75 100"
fillsize=250000
iosizes="10 1000"
pool=zlog3osd

for readpct in $readpcts; do
  for iosize in $iosizes; do
    params="--pool ${pool} --width ${width} --iosize ${iosize}
      --nthreads ${nthreads} --read-pct ${readpct} --fill-size ${fillsize}
      --runtime ${runtime}"
    echo $params
    ./zlog-bench2 ${params}
  done
done
