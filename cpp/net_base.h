#pragma once

#include "stdafx.h"

namespace crx
{
    enum SOCK_TYPE
    {
        UNIX_DOMAIN = 0,    //unix域套接字
        NORM_TRANS,         //跨网络的传输层tcp/ip套接字
    };

    enum PRT_TYPE
    {
        PRT_TCP = 0,
        PRT_UDP,
    };

    enum USR_TYPE
    {
        USR_SERVER = 0,
        USR_CLIENT,
    };

    class net_socket
    {
    public:
        net_socket(SOCK_TYPE type) : m_type(type), m_port(0), m_sock_fd(-1) {}

        /*
         * 创建一个socket，并且设置重用ip地址和端口
         * @ptype：tcp/udp socket
         * @utype：server/client end
         * @ip_addr："127.0.0.1"
         * @port:8080
         */
        int create(PRT_TYPE ptype = PRT_TCP, USR_TYPE utype = USR_SERVER,
                   const char *ip_addr = nullptr, uint16_t port = 0)
        {
            int domain, type, proto;
            if (UNIX_DOMAIN == m_type) {
                domain = AF_UNIX;
                proto = IPPROTO_IP;
            } else {    //NORM_TRANS == m_type
                domain = AF_INET;
                if (PRT_TCP == ptype)
                    proto = IPPROTO_TCP;
                else
                    proto = IPPROTO_UDP;
            }

            if (PRT_TCP == ptype)
                type = SOCK_STREAM | SOCK_NONBLOCK;
            else
                type = SOCK_DGRAM | SOCK_NONBLOCK;

            m_sock_fd = socket(domain, type, proto);    //创建套接字
            if (__glibc_unlikely(-1 == m_sock_fd)) {
                log_error(g_lib_log, "socket create failed: %s\n", strerror(errno));
                return -1;
            }

            bzero(&m_addr, sizeof(m_addr));
            if (UNIX_DOMAIN == m_type) {    //对unix域套接字进行设置
                m_addr.local.sun_family = AF_UNIX;
                std::string abs_name = "daniel-";
                abs_name += get_work_space();
                strncpy(m_addr.local.sun_path+1, abs_name.c_str(), 107);
            } else {    //NORM_TRANS == m_type
                //允许重用ip地址
                socklen_t opt_val = 1;
                if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val))))
                    log_error(g_lib_log, "setsockopt %d *SO_REUSEADDR* failed: %s\n", m_sock_fd, strerror(errno));

                //允许重用端口
                if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt_val, sizeof(opt_val))))
                    log_error(g_lib_log, "setsockopt %d *SO_REUSEPORT* failed: %s\n", m_sock_fd, strerror(errno));

                m_addr.trans.sin_family = AF_INET;
                if (USR_SERVER == utype && !ip_addr)
                    m_addr.trans.sin_addr.s_addr = INADDR_ANY;		//监听任意ip地址发送的连接请求
                else
                    m_addr.trans.sin_addr.s_addr = inet_addr(ip_addr);
                m_addr.trans.sin_port = htons(port);		//将端口由本机字节序转换为网络字节序
                m_port = port;		//保存所用端口(若创建套接字时未指定使用的端口，则系统将自动选择一个可用端口)
            }

            try {
                socklen_t addr_len = (UNIX_DOMAIN == m_type) ? sizeof(m_addr.local) : sizeof(m_addr.trans);
                if (USR_SERVER == utype) {		//服务端
                    //无论传输层协议是tcp还是udp，在server端都需要首先进行绑定操作
                    if (__glibc_unlikely(-1 == bind(m_sock_fd, (struct sockaddr*)&m_addr, addr_len))) {
                        if (NORM_TRANS == m_type)
                            log_error(g_lib_log, "bind socket %d failed: %s\n", m_sock_fd, strerror(errno));
                        throw -2;
                    }

                    //若传输层使用的是tcp协议，则由于tcp是面向连接的，因此需要监听任意ip地址的连接请求
                    if (PRT_TCP == ptype && __glibc_unlikely(-1 == listen(m_sock_fd, 128))) {
                        log_error(g_lib_log, "listen socket %d failed: %s\n", m_sock_fd, strerror(errno));
                        throw -3;
                    }
                } else if (PRT_TCP == ptype && -1 == connect(m_sock_fd, (struct sockaddr*)&m_addr, addr_len)) {
                    //tcp客户端需要执行连接操作
                    if (__glibc_unlikely(EINPROGRESS != errno)) {
                        log_error(g_lib_log, "connect socket %d failed: %s\n", m_sock_fd, strerror(errno));
                        throw -4;
                    }
                }

                if (NORM_TRANS == m_type)   //只有跨网络的套接字才会占用端口
                    get_inuse_port();
                return m_sock_fd;
            } catch (int exp) {
                close(m_sock_fd);
                m_sock_fd = -1;
                return exp;
            }
        }

        /*
         * 开启TCP_NODELAY，禁用Nagle's Algorithm，在每次发送少量数据的情况下，
         * 该算法会缓存数据直到收到已发送数据的Ack时才将数据发送出去
         */
        void set_tcp_nodelay(socklen_t opt_val)
        {
            if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val))))
                log_error(g_lib_log, "setsockopt %d *TCP_NODELAY* failed: %s\n", m_sock_fd, strerror(errno));
        }

        void set_keep_alive(int val, int idle, int interval, int count)
        {
            if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val))))
                log_error(g_lib_log, "setsockopt %d *SO_KEEPALIVE* failed: %s\n", m_sock_fd, strerror(errno));

            if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, SOL_TCP, TCP_KEEPIDLE, &idle, sizeof(idle))))
                log_error(g_lib_log, "setsockopt %d *TCP_KEEPIDLE* failed: %s\n", m_sock_fd, strerror(errno));

            if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, SOL_TCP, TCP_KEEPINTVL, &interval, sizeof(interval))))
                log_error(g_lib_log, "setsockopt %d *TCP_KEEPINTVL* failed: %s\n", m_sock_fd, strerror(errno));

            if (__glibc_unlikely(-1 == setsockopt(m_sock_fd, SOL_TCP, TCP_KEEPCNT, &count, sizeof(count))))
                log_error(g_lib_log, "setsockopt %d *TCP_KEEPCNT* failed: %s\n", m_sock_fd, strerror(errno));
        }

    private:
        void get_inuse_port()
        {
            if (m_port) return;     //若已指定所用端口，则不再获取当前套接占用的端口
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);

            //获取和该套接字相关的一系列地址信息，此处只需要sockaddr_in结构体中的端口字段sin_port
            if (__glibc_unlikely(-1 == getsockname(m_sock_fd, (struct sockaddr*)&addr, &len)))
                log_error(g_lib_log, "socket %d getsockname failed: %s\n", m_sock_fd, strerror(errno));
            else
                m_port = ntohs(addr.sin_port);		//将端口从网络字节序转换为本机字节序
        }

    public:
        SOCK_TYPE m_type;
        union
        {
            struct sockaddr_un local;
            struct sockaddr_in trans;
        } m_addr;
        uint16_t m_port;
        int m_sock_fd;
    };
}
