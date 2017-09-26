#pragma once

namespace crx
{
    struct console_cmd
    {
        std::string comment;    //注释
        std::function<void(std::vector<std::string>&, console*)> f;     //回调函数
    };

    class console_impl
    {
    public:
        console_impl(console *c);
        virtual ~console_impl() {}

        bool preprocess(int argc, char *argv[]);
        bool execute_cmd(const std::vector<std::string>& args);
        void wait_shell_input(int wr_fifo);
        void wait_pipe_input(int stdout_bk);

        static void start_daemon(std::vector<std::string>& args, console *c);
        static void stop_daemon(std::vector<std::string>& args, console *c);
        static void quit_loop(std::vector<std::string>& args, console *c);
        static void print_help(std::vector<std::string>& args, console *c);

    private:
        void connect_service(bool stop_service);
        void cons_pipe_name(const char *argv_0);
        bool check_service_exist();

    public:
        console *m_c;
        /**
         * @m_is_service：当前服务以daemon进程在后台运行时，该值为true，否则为false
         * @m_init：预处理完成后该值为true，主要用于区分当前的控制台命令是带参运行形式的命令还是运行时命令
         * @m_as_shell：若后台服务正在运行过程中再次启动该程序，则新的进程作为daemon进程的shell存在
         */
        bool m_is_service, m_init, m_as_shell;
        std::string m_pipe_name[2], m_pipe_dir;
        std::map<bool, std::map<std::string, console_cmd>> m_cmds;		//m_cmds根据m_init当前的值区分为两类
    };
}
