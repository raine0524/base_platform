#pragma once

#include "stdafx.h"

namespace crx
{
    struct console_cmd
    {
        std::string cmd;        //命令
        std::function<void(std::vector<std::string>&)> f;     //回调函数
        std::string comment;    //注释
    };

    class console_impl : public impl
    {
    public:
        console_impl(console *c);
        virtual ~console_impl() {}

        void bind_core(int which);       //将线程绑定到指定的cpu核上

        bool preprocess(int argc, char *argv[]);

        bool execute_cmd(const std::string& cmd, std::vector<std::string>& args);

        void listen_keyboard_input();

        void listen_pipe_input();

        void quit_loop(std::vector<std::string>& args);

        void print_help(std::vector<std::string>& args);

    private:
        void start_daemon();

        void connect_service(bool stop_service);

        void cons_pipe_name(const char *argv_0);

        bool check_service_exist();

    public:
        console *m_c;

        /*
         * @m_is_service：当前服务以daemon进程在后台运行时，该值为true，否则为false
         * @m_as_shell：若后台服务正在运行过程中再次启动该程序，则新的进程作为daemon进程的shell存在
         */
        bool m_is_service, m_as_shell;
        std::string m_pipe_dir, m_pipe_name[2];
        std::shared_ptr<eth_event> m_console_ev;

        std::random_device m_random;
        std::vector<console_cmd> m_cmd_vec;
        std::map<std::string, size_t> m_cmd_idx;    //m_cmds根据m_init当前的值区分为两类
    };
}
