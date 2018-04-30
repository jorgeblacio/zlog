#!/bin/bash
set -x
set -e

sizes="1 128 4096 4097 262144 262145"
max_xattr_size=128
stores="omap append xattr omap_append omap_fixed_append xattr_fixed_append"
pool=zlog
max_gbs=64
width=32
qdepth=16
runtime=300
clean_secs=120
target_objsize=16777216
md_size=40

rados purge ${pool} --yes-i-really-really-mean-it
sleep ${clean_secs}

for size in $sizes; do
  for store in $stores; do
    if [ "x$store" = "xxattr" ]; then
      if [ $size -gt $max_xattr_size ]; then
        continue
      fi
    fi

    epo=$((${target_objsize} / ${size}))
    if [ $epo -gt 10000 ]; then
      epo=10000
    fi

    prefix="es-${size}.qd-${qdepth}.w-${width}.epo-${epo}.mdsize-${md_size}"

    bin/rados_append_bench --pool ${pool} --width ${width} \
      --size ${size} --qdepth ${qdepth} --runtime ${runtime} \
      --max_gbs ${max_gbs} --epo ${epo} --prefix ${prefix} \
      --store ${store} --md_size ${md_size}

  rados purge ${pool} --yes-i-really-really-mean-it
  sleep ${clean_secs}

done
done
