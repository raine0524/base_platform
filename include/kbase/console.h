#pragma once

namespace crx
{
    class CRX_SHARE console : public epoll_thread
    {
    public:
        console();
        virtual ~console();

        //控制台初始化，返回true表示初始化成功，false将直接退出main函数
        virtual bool init(int argc, char *argv[]) = 0;

        //控制台销毁函数，用于释放当前进程占用的一系列资源
        virtual void destroy() = 0;

        /**
         * 为当前控制台增加可以手动发送的命令
         * @cmd: 表示命令的字符串
         * @f: 当手动输入命令时控制台调用的回调函数，其中std::vector<std::string>是去除命令后的其他参数
         * @comment: 当前命令的注释，用于解释cmd的用途
         * 例如，若在控制台输入字符串"push a b c"
         * 那么"push"将被解释为命令，而"a" "b" "c"将被装入std::vector<std::string>传给该命令对应的回调函数
         */
        void add_cmd(const char *cmd, std::function<void(std::vector<std::string>&, console*)> f,
                     const char *comment);

        /**
         * 控制台带参运行时将会检测最后一个参数是否为-start/-stop
         * "-start": 以服务形式在后台运行
         * "-stop": 终止后台运行的服务
         * "-restart": 重启后台运行的服务
         */
        int run(int argc, char *argv[]);
    };
}