#!/bin/bash -xv

proj_path=$(cd `dirname $0`; pwd)
build_dir=cmake-build-debug

deploy_dir=deploy
deploy_pkg=release.tar.gz

# build
cd $proj_path
rm -rf $deploy_dir
mkdir -p $build_dir
cd $build_dir
rm -rf *
cmake ..
cd -
cmake --build $proj_path/$build_dir --target all -- -j 4
cd -

make
#ctest --output-on-failure
ctest --verbose
if [ $? -ne 0 ]; then
	echo "build or run test case failed!"
	exit 1
fi
make install

# pack
cd $proj_path
if [ -f $deploy_pkg ] ; then
	rm $deploy_pkg
fi

cd $deploy_dir
tar -czf ../$deploy_pkg *
cd -
exit 0

# deploy & run
toolkit_dir=/root/deploy/toolkit
cloud_host=$CLOUD_HOST
cloud_pwd=$CLOUD_PWD

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
