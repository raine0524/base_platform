#!/bin/bash -xv

proj_path=$(cd `dirname $0`; pwd)
build_dir=cmake-build-debug
deploy_pkg=release.tar.gz

toolkit_dir=/root/deploy/toolkit
cloud_host=$CLOUD_HOST
cloud_pwd=$CLOUD_PWD

# build
mkdir -p $build_dir
cd $proj_path/$build_dir
rm -rf *
cmake ..
cd ..
cmake --build $proj_path/$build_dir --target all -- -j 4
cd -
make install

# pack
cd $proj_path
if [ -f $deploy_pkg ] ; then
	rm $deploy_pkg
fi
tar -czf $deploy_pkg deploy/
exit

# deploy & run
/usr/bin/expect << EOF
set timeout -1
spawn ssh -o StrictHostKeyChecking=no -l root $cloud_host
expect "*password:"
send "$cloud_pwd\r"
expect "*~#"
send "$toolkit_dir/stop_tools.sh\r"
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
send "tar -xzf $deploy_pkg\r"
expect "*~#"
send "$toolkit_dir/start_tools.sh\r"
expect "*~#"
send "exit\r"
expect eof
EOF
