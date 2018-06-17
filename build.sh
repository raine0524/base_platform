#!/bin/bash -xv

proj_path=$(cd `dirname $0`; pwd)
build_dir=cmake-build-debug
deploy_pkg=release.tar.gz

cloud_host=$CLOUD_HOST
cloud_pwd=$CLOUD_PWD

# 构建
mkdir -p $build_dir
cd $proj_path/$build_dir
rm -f CMakeCache.txt
cmake ..
make install

# 打包
cd $proj_path
if [ -f $deploy_pkg ] ; then
	rm $deploy_pkg
fi
tar -czvf $deploy_pkg deploy/

# 部署 & 运行
/usr/bin/expect << EOF
set timeout -1
spawn ssh -o StrictHostKeyChecking=no -l root $cloud_host
expect "*password:"
send "$cloud_pwd\r"
expect "*~#"
send "rm -rf deploy* release.tar.gz\r"
expect "*~#"
send "exit\r"
expect eof
spawn scp -o StrictHostKeyChecking=no $deploy_pkg root@$cloud_host:~
expect "*password:"
send "$cloud_pwd\r"
expect eof
spawn ssh -o StrictHostKeyChecking=no -l root $cloud_host
expect "*password:"
send "$cloud_pwd\r"
expect "*~#"
send "tar -xzvf $deploy_pkg\r"
expect "*~#"
send "exit\r"
expect eof
EOF
