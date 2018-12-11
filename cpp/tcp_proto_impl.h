#pragma once

#include "stdafx.h"

namespace crx
{
    enum APP_PRT    //应用层协议的类型
    {
        PRT_NONE = 0,       //使用原始的传输层协议
        PRT_HTTP,			//HTTP协议
        PRT_SIMP,           //SIMP协议(私有)
    };

    //tcp套接字需要异步写操作
    class tcp_event : public eth_event
    {
    public:
        tcp_event(SOCK_TYPE type)
        :event(EPOLLIN)
        ,is_connect(false)
        ,release_conn(false)
        ,conn_sock(type)
        {
            stream_buffer.reserve(8192);
        }

        int async_write(const char *data, size_t len);

        void release();

        uint32_t event;
        std::string cache_data;     //缓存数据，等待可写事件
        std::shared_ptr<impl> ext_data;         //扩展数据

        bool is_connect, release_conn;
        std::string ip_addr;            //转换之后的ip地址
        net_socket conn_sock;
        std::string stream_buffer;      //tcp缓冲流
    };

    class tcp_client_impl;
    class tcp_client_conn : public tcp_event
    {
    public:
        tcp_client_conn(SOCK_TYPE type)
        :tcp_event(type)
        ,cnt(0)
        ,last_conn(1)
        {
            name_reqs[0] = new gaicb;
            bzero(name_reqs[0], sizeof(gaicb));
        }

        virtual ~tcp_client_conn()
        {
            if (name_reqs[0]->ar_result)
                freeaddrinfo(name_reqs[0]->ar_result);
            delete name_reqs[0];
        }

        void tcp_client_callback(uint32_t events);

        void retry_connect();

        //解析域名时需要用到的相关设施
        gaicb *name_reqs[1];
        addrinfo req_spec;
        sigevent sigev;

        std::shared_ptr<tcp_client_impl> tcp_impl;
        std::string domain_name;        //连接对端使用的主机地址
        int retry, timeout, cnt, last_conn;
    };

    class tcp_impl_xutil
    {
    public:
        tcp_impl_xutil(SOCK_TYPE type)
        :m_sch(nullptr)
        ,m_app_prt(PRT_NONE)
        ,m_type(type) {}

        scheduler *m_sch;
        APP_PRT m_app_prt;
        SOCK_TYPE m_type;

        //tcp_client需要一个秒盘做重连，tcp_server需要一个分钟盘做会话管理
        timer_wheel m_wheel;
        std::function<int(int, char*, size_t)> m_protocol_hook;      //协议钩子
        std::function<void(int, const std::string&, uint16_t, char*, size_t)> m_f;    //收到tcp数据流时触发的回调函数
    };

    class tcp_client_impl : public impl
    {
    public:
        tcp_client_impl(SOCK_TYPE type = NORM_TRANS) : m_util(type) {}

        void name_resolve_callback(int signo, uint64_t sigval)
        {
            m_util.m_sch->co_yield(sigval);
        }

        tcp_impl_xutil m_util;
    };

    class tcp_server_impl;
    class tcp_server_conn : public tcp_event
    {
    public:
        tcp_server_conn(SOCK_TYPE type) : tcp_event(type) {}

        void read_tcp_stream(uint32_t events);

        tcp_server_impl *tcp_impl;
    };

    class tcp_server_impl : public tcp_event
    {
    public:
        tcp_server_impl(SOCK_TYPE type)
        :tcp_event(type)
        ,m_addr_len(0)
        ,m_util(type) {}

        void tcp_server_callback(uint32_t events);

        struct sockaddr_in m_accept_addr;
        socklen_t m_addr_len;
        tcp_impl_xutil m_util;
    };

    template<typename CONN_TYPE>
    void handle_stream(int conn, CONN_TYPE conn_ins)
    {
        if (conn_ins->stream_buffer.empty()) return;
        auto sch_impl = conn_ins->sch_impl.lock();
        if (conn_ins->tcp_impl->m_util.m_protocol_hook) {
            conn_ins->stream_buffer.push_back(0);
            char *start = &conn_ins->stream_buffer[0];
            size_t buf_len = conn_ins->stream_buffer.size()-1, read_len = 0;
            while (read_len < buf_len) {
                size_t remain_len = buf_len-read_len;
                int ret = conn_ins->tcp_impl->m_util.m_protocol_hook(conn, start, remain_len);
                if (0 == ret) break;

                int abs_ret = std::abs(ret);
                assert(abs_ret <= remain_len);
                if (ret > 0)
                    conn_ins->tcp_impl->m_util.m_f(conn_ins->fd, conn_ins->ip_addr,
                            conn_ins->conn_sock.m_port, start, ret);

                start += abs_ret;
                read_len += abs_ret;
            }

            if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
                return;

            conn_ins->stream_buffer.pop_back();
            if (read_len == buf_len)
                conn_ins->stream_buffer.clear();
            else if (read_len > 0)
                conn_ins->stream_buffer.erase(0, read_len);
        } else {
            conn_ins->tcp_impl->m_util.m_f(conn_ins->fd, conn_ins->ip_addr, conn_ins->conn_sock.m_port,
                    &conn_ins->stream_buffer[0], conn_ins->stream_buffer.size());
        }
    }
}