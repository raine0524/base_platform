#pragma once

#include "crx_pch.h"

namespace crx
{
    class CRX_SHARE console : public scheduler
    {
    public:
        console();
        virtual ~console();

        //控制台初始化，返回true表示初始化成功，false将直接退出main函数
        virtual bool init(int argc, char *argv[]) = 0;

        //控制台销毁函数，用于释放当前进程占用的一系列资源
        virtual void destroy() = 0;

        /*
         * 为当前控制台增加可以手动发送的命令
         * @cmd: 表示命令的字符串
         * @f: 当手动输入命令时控制台调用的回调函数，其中std::vector<std::string>是去除命令后的其他参数
         * @comment: 当前命令的注释，用于解释cmd的用途
         * 例如，若在控制台输入字符串"push a b c"
         * 那么"push"将被解释为命令，而"a" "b" "c"将被装入std::vector<std::string>传给该命令对应的回调函数
         */
        void add_cmd(const char *cmd, std::function<void(std::vector<std::string>&)> f,
                     const char *comment);

        //当前进程在后台运行时,使用该接口将信息打印到shell进程的标准输出中
        void pout(const char *fmt, ...);

        /*
         * 控制台带参运行时将会检测最后一个参数是否为-start/-stop
         * "-start": 以服务形式在后台运行
         * "-stop": 终止后台运行的服务
         * "-restart": 重启后台运行的服务
         * @param bind_flag: 是否将当前进程的主线程绑定到某个cpu核上，-1表示不绑定，INT_MAX表示随机绑定到一个核上
         * 除此之外也可以将bing_flag绑定到用户指定的核上，此时bind_flag的取值范围为0~N-1，其中N为核心数
         */
        int run(int argc, char *argv[], const char *conf = "ini/server.ini", int bind_flag = -1);
    };
}