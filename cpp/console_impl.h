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

        void execute_cmd(const std::string& cmd, std::vector<std::string>& args);

        void listen_keyboard_input();

        void quit_loop(std::vector<std::string>& args);

        void print_help(std::vector<std::string>& args);

    private:
        bool check_service_exist();

        void start_daemon();

        void connect_service();

        void stop_service(bool pout);

        void tcp_callback(bool client, int conn, const std::string& ip_addr, uint16_t port, char *data, size_t len);

    public:
        console *m_c;

        /*
         * @m_is_service：当前服务以daemon进程在后台运行时，该值为true，否则为false
         * @m_as_shell：若后台服务正在运行过程中再次启动该程序，则新的进程作为daemon进程的shell存在
         */
        bool m_is_service, m_as_shell, m_close_exp;
        std::random_device m_random;
        std::vector<console_cmd> m_cmd_vec;

        int m_conn;     //无论何时都只能存在一个有效连接
        tcp_client m_client;
        tcp_server m_server;
        std::string m_simp_buf;
    };
}
