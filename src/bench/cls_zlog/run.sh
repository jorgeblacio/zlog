#!/bin/bash
set -e
#set -x

cooloff=60
runtime=600
pool=zlog
esizes="0 128 1024 4096"
qdepths="8 16 32 64"
widths="64"
objsize=8388608

for es in $esizes; do
  for qd in $qdepths; do
    for w in $widths; do
      logfn="$es-$qd-$w.log"
      echo $logfn

      num_stripes=$(($(($objsize/$((es+1))))+1))

      ./zlog-cls-zlog-bench --pool $pool --runtime $runtime \
        --qdepth $qd --esize $es --width $w \
        --v2_num_stripes $num_stripes --iops-log $logfn

      ##rados purge zlog --yes-i-really-really-mean-it
      sleep $cooloff
    done
  done
done
