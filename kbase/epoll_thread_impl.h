#pragma once

namespace crx
{
    class epoll_thread_impl;
    struct eth_event
    {
        int fd;
        epoll_thread_impl *eth_impl;
        std::list<std::string*> data_list;   //待写的数据列表

        uint32_t event;
        std::function<void(eth_event*)> f, f_bk;     //回调函数
        eth_event *args;         //回调参数

        eth_event()
                :fd(-1)
                ,eth_impl(nullptr)
                ,event(EPOLLIN)
                ,args(nullptr) {}

        virtual ~eth_event()
        {
            if (-1 != fd && STDIN_FILENO != fd) {
                close(fd);
                fd = -1;
            }

            for (auto& data : data_list)
                delete data;
            data_list.clear();
        }
    };

    struct eth_sig
    {
        union
        {
            eth_event *ev;
            int fd;
        };
        int type;   //-1-退出信号 0-ev 1-fd
        int op;     //0-删除 1-新增

        eth_sig()
        {
            bzero(this, sizeof(eth_sig));
        }
    };

    class timer_impl : public eth_event
    {
    public:
        timer_impl()
                :m_delay(0)
                ,m_interval(0)
                ,m_args(nullptr) {}
        virtual ~timer_impl() {}

        static void timer_callback(eth_event *args);

        int64_t m_delay, m_interval;			//分别对应于首次触发时的延迟时间以及周期性触发时的间隔时间
        std::function<void(void*)> m_f;	//定时事件触发时的回调函数
        void *m_args;								//回调参数
    };

    class event_impl : public eth_event
    {
    public:
        event_impl()
                :m_args(nullptr) {}
        virtual ~event_impl() {}

        static void event_callback(eth_event *args);

        std::mutex m_mtx;  //对信号资源进行互斥访问
        std::list<std::string> m_signals;		//同一个事件可以由多个线程通过发送不同的信号同时触发

        std::function<void(std::string&, void*)> m_f;		//事件触发时的回调函数
        void *m_args;			//回调参数
    };

    class udp_ins_impl : public eth_event
    {
    public:
        udp_ins_impl()
                :m_recv_buffer(65536, 0)
                ,m_args(nullptr) {}

        static void udp_ins_callback(eth_event *args);

        net_socket m_net_sock;			//udp套接字
        std::string m_recv_buffer;		//接收缓冲区

        std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> m_f;			//收到数据时触发的回调函数
        void *m_args;		//回调参数
    };

    enum APP_PRT    //应用层协议的类型
    {
        PRT_HTTP = 0,			//HTTP协议
    };

    class tcp_client_impl;
    struct tcp_client_conn : public eth_event
    {
        bool resolve_succ;
        struct ub_ctx *ctx;     //名字解析上下文

        net_socket conn_sock;
        tcp_client_impl *tcp_impl;

        std::string domain_name;        //连接对端使用的主机地址
        std::string ip_addr;            //转换之后的ip地址
        uint16_t port;					//对端端口

        tcp_client_conn()
                :resolve_succ(false)
                ,ctx(nullptr)
                ,tcp_impl(nullptr) {}
    };

    struct write_sig
    {
        std::string *data;
        int fd;

        write_sig()
        {
            bzero(this, sizeof(write_sig));
        }
    };

    class tcp_client_impl
    {
    public:
        tcp_client_impl()
                :m_timer(nullptr)
                ,m_resolve_ev(nullptr)
                ,m_write_ev(nullptr)
                ,m_expose(false)
                ,m_eth_impl(nullptr)
                ,m_tcp_args(nullptr) {}

        virtual ~tcp_client_impl()
        {
            if (m_timer) {
                delete m_timer;
                m_timer = nullptr;
            }

            if (m_resolve_ev) {
                delete m_resolve_ev;
                m_resolve_ev = nullptr;
            }

            if (m_write_ev) {
                delete m_write_ev;
                m_write_ev = nullptr;
            }
        }

        static void name_resolve_process(void *args);
        static void name_resolve_callback(void *args, int err, ub_result *result);
        static void resolve_comp_callback(std::string& signal, void *args);
        static void tcp_client_callback(eth_event *ev);
        static void accept_request(std::string& signal, void *args);

        std::mutex m_mtx;
        std::list<tcp_client_conn*> m_resolve_list;

        timer *m_timer;
        event *m_resolve_ev, *m_write_ev;

        bool m_expose;
        APP_PRT m_app_prt;

        epoll_thread_impl *m_eth_impl;
        std::function<void(int, std::string&, void*)> m_tcp_f;		//收到tcp数据流时触发的回调函数
        void *m_tcp_args;		//回调参数
    };

    class tcp_server_impl;
    struct tcp_server_conn : public eth_event
    {
        std::string ip_addr;	//连接对端的ip地址
        uint16_t port;			//对端端口
        tcp_server_impl *ts_impl;
    };

    class tcp_server_impl : public eth_event
    {
    public:
        tcp_server_impl()
                :m_write_ev(nullptr)
                ,m_expose(false)
                ,m_tcp_args(nullptr) {}

        virtual ~tcp_server_impl()
        {
            if (m_write_ev) {
                delete m_write_ev;
                m_write_ev = nullptr;
            }
        }

        void start_listen(uint16_t port);
        static void tcp_server_callback(eth_event *ev);
        static void read_tcp_stream(eth_event *ev);
        static void accept_write(std::string& signal, void *args);

        net_socket m_net_sock;			//tcp服务端监听套接字
        crx::event *m_write_ev;

        bool m_expose;
        APP_PRT m_app_prt;

        //收到tcp数据流时触发的回调函数 @int类型的参数指明是哪一个连接
        std::function<void(int, const std::string&, uint16_t, std::string&, void*)> m_tcp_f;
        void *m_tcp_args;		//回调参数
    };

    extern std::unordered_map<int, std::string> g_ext_type;

    struct http_client_conn : public tcp_client_conn
    {
        std::string stream_buffer;			//http缓冲流
        int m_status, m_content_len;
        std::unordered_map<std::string, std::string> m_headers;

        http_client_conn()
                :m_status(-1)
                ,m_content_len(-1) {}
    };

    class http_client_impl : public tcp_client_impl
    {
    public:
        //检查当前流中是否存在完整的http响应流，对可能的多个响应进行分包处理并执行相应的回调
        void check_http_stream(int fd, http_client_conn *conn);

        std::function<void(int, int, std::unordered_map<std::string, std::string>&, std::string&, void*)> m_http_f;		//响应的回调函数
        void *m_http_args;		//回调参数
    };

    struct http_server_conn : public tcp_server_conn
    {
        std::string stream_buffer;		//http缓冲流
        int m_content_len;
        std::string m_method;		//请求方法
        std::string m_url;				//url(以"/"开始的字符串)
        std::unordered_map<std::string, std::string> m_headers;

        http_server_conn()
                :m_content_len(-1) {}
    };

    class http_server_impl : public tcp_server_impl
    {
    public:
        //检查当前流中是否存在完整的http请求流，对可能连续的多个请求进行分包处理并执行相应的回调
        void check_http_stream(int fd, http_server_conn *conn);

        //响应的回调函数
        std::function<void(int, const std::string&, const std::string&, std::unordered_map<std::string, std::string>&, std::string*, void*)> m_http_f;
        void *m_http_args;		//回调参数
    };

    struct monitor_ev
    {
        bool recur_flag;
        int watch_id;
        uint32_t mask;
        std::string path;
    };

    class fs_monitor_impl : public eth_event
    {
    public:
        fs_monitor_impl()
                :m_monitor_ev(nullptr) {}

        virtual ~fs_monitor_impl()
        {
            if (m_monitor_ev) {
                delete m_monitor_ev;
                m_monitor_ev = nullptr;
            }
        }

        static void monitor_request(std::string& signal, void *args);
        static void fs_monitory_callback(eth_event *ev);
        void recursive_monitor(const std::string& root_dir, bool add, int mask);
        void trigger_event(bool add, int watch_id, const std::string& path, int recur_flag, uint32_t mask);

        crx::event *m_monitor_ev;
        std::unordered_map<std::string, std::shared_ptr<monitor_ev>> m_path_mev;
        std::unordered_map<int, std::shared_ptr<monitor_ev>> m_wd_mev;

        //响应的回调函数
        std::function<void(const char*, uint32_t, void *args)> m_monitor_f;
        void *m_monitor_args;   //回调参数
    };

    class epoll_thread_impl
    {
    public:
        epoll_thread_impl()
                :m_event(nullptr)
                ,m_obj(nullptr)
                ,m_http_client(nullptr)
                ,m_http_server(nullptr)
                ,m_tcp_client(nullptr)
                ,m_tcp_server(nullptr)
                ,m_fs_monitor(nullptr) {}

        virtual ~epoll_thread_impl()
        {
            if (m_event) {
                delete m_event;
                m_event = nullptr;
            }
        }

        /**
         * 添加epoll事件，每个事件都采用edge-trigger的模式，只要可读/写，就一直进行读/写，直到不再有数据或者
         * 文件不再可读/出错，因此在此函数中会将文件设置为非阻塞状态，fd应当保证是一个有效的文件描述符
         */
        void handle_event(int op, eth_event *eth_ev);

        void join_thread();

        static void thread_proc(epoll_thread_impl *this_ptr);

        static void handle_request(std::string& signal, void *args);

        /**
         * @fd: 非阻塞文件描述符
         * @read_str: 将读到的数据追加在read_str尾部
         * @return param
         * 		-> 1: 等待更多数据可读
         * 		-> 0: 所读文件正常关闭
         * 		-> -1: 出现异常，异常由errno描述
         */
        static int async_read(int fd, std::string& read_str);

        /**
         * @ev: eth_event对象
         * @data: 待写的数据
         */
        void async_write(eth_event *ev, std::string *data);

        void add_event(eth_event *ev);

        void remove_event(eth_event *ev);

    private:
        static void switch_write(void *args);

    public:
        bool m_cover, m_go_done;		//表明是否覆盖当前线程
        int m_epoll_fd;		//epoll线程描述符及等待线程终止信号的事件描述符
        event *m_event;
        std::vector<eth_event*> m_ev_array;
        std::thread m_thread;
        void *m_obj;        //扩展数据区

        http_client *m_http_client;
        http_server *m_http_server;
        tcp_client *m_tcp_client;
        tcp_server *m_tcp_server;
        fs_monitor *m_fs_monitor;
    };
}
