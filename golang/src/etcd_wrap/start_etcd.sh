#!/bin/sh

local_ip=192.168.10.36
work_path="$( cd "$( dirname $0 )" && pwd )"
cd $work_path

#若需安装etcd，那么启动此脚本时附带参数1
if [ $# = 1 ] && [ $1 = 1 ]; then
	wget https://github.com/etcd-io/etcd/releases/download/v3.3.10/etcd-v3.3.10-linux-amd64.tar.gz
	tar -zxf etcd-v3.3.10-linux-amd64.tar.gz
fi

mkdir -p data logs
seq -f "data/etcd%01g" 0 2 | xargs mkdir -p
pidof etcd | xargs kill -9

# list etcd members
# ./etcd-v3.3.10-linux-amd64/etcdctl --endpoints=$ETCD_ENDPOINTS member list
# etcd health check
# ./etcd-v3.3.10-linux-amd64/etcdctl --endpoints=$ETCD_ENDPOINTS cluster-health
# show all keys
# ./etcd-v3.3.10-linux-amd64/etcdctl --endpoints=$ETCD_ENDPOINTS ls / --recursive

nohup ./etcd-v3.3.10-linux-amd64/etcd --name etcd0 --data-dir $work_path/data/etcd0 \
	--advertise-client-urls http://$local_ip:12379,http://127.0.0.1:12379 \
	--listen-client-urls http://$local_ip:12379,http://127.0.0.1:12379 \
	--initial-advertise-peer-urls http://$local_ip:12380 \
	--listen-peer-urls http://$local_ip:12380 \
	--initial-cluster-token etcd-cluster-1 \
	--initial-cluster etcd0=http://$local_ip:12380,etcd1=http://$local_ip:12480,etcd2=http://$local_ip:12580 \
	--initial-cluster-state new > $work_path/logs/etcd0.log 2>&1 &

nohup ./etcd-v3.3.10-linux-amd64/etcd --name etcd1 --data-dir $work_path/data/etcd1 \
	--advertise-client-urls http://$local_ip:12479,http://127.0.0.1:12479 \
	--listen-client-urls http://$local_ip:12479,http://127.0.0.1:12479 \
	--initial-advertise-peer-urls http://$local_ip:12480 \
	--listen-peer-urls http://$local_ip:12480 \
	--initial-cluster-token etcd-cluster-1 \
	--initial-cluster etcd0=http://$local_ip:12380,etcd1=http://$local_ip:12480,etcd2=http://$local_ip:12580 \
	--initial-cluster-state new > $work_path/logs/etcd1.log 2>&1 &

nohup ./etcd-v3.3.10-linux-amd64/etcd --name etcd2 --data-dir $work_path/data/etcd2 \
	--advertise-client-urls http://$local_ip:12579,http://127.0.0.1:12579 \
	--listen-client-urls http://$local_ip:12579,http://127.0.0.1:12579 \
	--initial-advertise-peer-urls http://$local_ip:12580 \
	--listen-peer-urls http://$local_ip:12580 \
	--initial-cluster-token etcd-cluster-1 \
	--initial-cluster etcd0=http://$local_ip:12380,etcd1=http://$local_ip:12480,etcd2=http://$local_ip:12580 \
	--initial-cluster-state new > $work_path/logs/etcd2.log 2>&1 &

