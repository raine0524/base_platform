#pragma once

namespace crx
{
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
        net_socket() : m_port(0), m_sock_fd(-1)
        {
            bzero(&m_addr, sizeof(m_addr));
        }

        /**
         * 创建一个socket，并且设置重用ip地址和端口
         * @ptype：tcp/udp socket
         * @utype：server/client end
         * @ip_addr："127.0.0.1"
         * @port:8080
         */
        int create(PRT_TYPE ptype = PRT_TCP, USR_TYPE utype = USR_SERVER,
                   const char *ip_addr = nullptr, uint16_t port = 0)
        {
            if (PRT_TCP == ptype)		//根据指定的协议创建对应的套接字
                m_sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
            else		//PRT_UDP == ptype
                m_sock_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);

            if (-1 == m_sock_fd) {
                perror("net_socket::create::socket");
                return -1;
            }
            m_ptype = ptype;		//保存当前套接所用的协议类型

            //允许重用ip地址
            socklen_t opt_val = 1;
            if (-1 == setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)))
                perror("set_reuse_addr::setsockopt failed");

            //允许重用端口
            if (-1 == setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt_val, sizeof(opt_val)))
                perror("set_reuse_port::setsockopt failed");

            m_addr.sin_family = AF_INET;
            if (USR_SERVER == utype && !ip_addr)
                m_addr.sin_addr.s_addr = INADDR_ANY;		//监听任意ip地址发送的连接请求
            else
                m_addr.sin_addr.s_addr = inet_addr(ip_addr);
            m_addr.sin_port = htons(port);		//将端口由本机字节序转换为网络字节序
            m_port = port;		//保存所用端口(若创建套接字时未指定使用的端口，则系统将自动选择一个可用端口)

            if (USR_SERVER == utype) {		//服务器端
                /**
                 * 将套接字与地址进行绑定(通常在指定端口上监听任意ip地址的请求)
                 * 无论传输层协议是tcp还是udp，在服务器端都需要首先进行绑定操作
                 */
                if (-1 == bind(m_sock_fd, (struct sockaddr*)&m_addr, sizeof(m_addr))) {
                    perror("net_socket::create::bind");
                    return -1;
                }

                //若传输层使用的是tcp协议，则由于tcp是面向连接的，因此需要监听任意ip地址的连接请求
                if (PRT_TCP == ptype && -1 == listen(m_sock_fd, 128)) {
                    perror("net_socket::create::listen");
                    return -1;
                }
            } else if (PRT_TCP == ptype && -1 == connect(m_sock_fd, (struct sockaddr*)&m_addr,
                                                         sizeof(m_addr))) {		//tcp客户端需要执行连接操作
                if (EINPROGRESS != errno) {
                    perror("net_socket::create::connect");
                    return -1;
                }
            }

            get_inuse_port();		//获取当前套接字所占用的端口
            return m_sock_fd;
        }

        /**
         * 开启TCP_NODELAY，禁用Nagle's Algorithm，在每次发送少量数据的情况下，
         * 该算法会缓存数据直到收到已发送数据的Ack时才将数据发送出去
         */
        void set_tcp_nodelay(socklen_t opt_val)
        {
            if (PRT_TCP != m_ptype)
                return;

            if (-1 == setsockopt(m_sock_fd, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val)))
                perror("net_socket::set_tcp_nodelay::setsockopt");
        }

        void set_keep_alive(socklen_t opt_val)
        {
            if (PRT_TCP != m_ptype)
                return;

            if (-1 == setsockopt(m_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &opt_val, sizeof(opt_val)))
                perror("net_socket::set_keep_alive::setsockopt");
        }

    private:
        void get_inuse_port()
        {
            if (m_port)		//若已指定所用端口，则不再获取当前套接占用的端口
                return;

            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            //获取和该套接字相关的一系列地址信息，此处只需要sockaddr_in结构体中的端口字段sin_port
            if (-1 == getsockname(m_sock_fd, (struct sockaddr*)&addr, &len)) {
                perror("get_inuse_port::getsockname");
                return;
            }
            m_port = ntohs(addr.sin_port);		//将端口从网络字节序转换为本机字节序
        }

    public:
        PRT_TYPE m_ptype;
        uint16_t m_port;
        struct sockaddr_in m_addr;
        int m_sock_fd;
    };
}
