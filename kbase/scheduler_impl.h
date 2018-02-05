#pragma once

namespace crx
{
    class scheduler_impl;
    struct eth_event
    {
        int fd, co_id;      //@fd: 该epoll事件对应的文件描述符, @co_id: 处理该事件的协程
        scheduler_impl *sch_impl;

        std::function<void(crx::scheduler *sch, eth_event*)> f;     //回调函数
        eth_event *args;         //回调参数

        eth_event()
                :fd(-1)
                ,co_id(0)
                ,args(nullptr) {}

        virtual ~eth_event()
        {
            if (-1 != fd && STDIN_FILENO != fd) {
                close(fd);
                fd = -1;
            }
        }
    };

    class timer_impl : public eth_event
    {
    public:
        timer_impl() : m_delay(0), m_interval(0), m_args(nullptr) {}
        ~timer_impl() override = default;

        static void timer_callback(scheduler *sch, eth_event *args);

        int64_t m_delay, m_interval;        //分别对应于首次触发时的延迟时间以及周期性触发时的间隔时间
        std::function<void(void*)> m_f;     //定时事件触发时的回调函数
        void *m_args;                       //回调参数
    };

    class event_impl : public eth_event
    {
    public:
        event_impl() : m_args(nullptr) {}
        ~event_impl() override = default;

        static void event_callback(scheduler *sch, eth_event *args);

        std::list<std::string> m_signals;		//同一个事件可以由多个线程通过发送不同的信号同时触发
        std::function<void(std::string&, void*)> m_f;		//事件触发时的回调函数
        void *m_args;			//回调参数
    };

    class udp_ins_impl : public eth_event
    {
    public:
        udp_ins_impl() : m_recv_buffer(65536, 0), m_args(nullptr) {}
        ~udp_ins_impl() override = default;

        static void udp_ins_callback(scheduler *sch, eth_event *args);

        struct sockaddr_in m_send_addr, m_recv_addr;
        socklen_t m_recv_len;

        net_socket m_net_sock;			//udp套接字
        std::string m_recv_buffer;		//接收缓冲区

        std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> m_f;			//收到数据时触发的回调函数
        void *m_args;		//回调参数
    };

    enum APP_PRT    //应用层协议的类型
    {
        PRT_NONE = 0,       //使用原始的传输层协议
        PRT_HTTP,			//HTTP协议
    };

    struct tcp_client_conn : public eth_event       //名字解析使用getaddrinfo_a
    {
        std::string domain_name;        //连接对端使用的主机地址
        std::string ip_addr;            //转换之后的ip地址
        uint16_t port;					//对端端口

        bool is_connect;
        net_socket conn_sock;
        std::string stream_buffer;      //tcp缓冲流

        tcp_client_conn() : port(0), is_connect(false) {}
    };

    class tcp_client_impl
    {
    public:
        tcp_client_impl() : m_app_prt(PRT_NONE), m_sch(nullptr), m_tcp_args(nullptr) {}

        static void tcp_client_callback(scheduler *sch, eth_event *ev);

        APP_PRT m_app_prt;
        std::list<tcp_client_conn*> m_resolve_list;

        scheduler *m_sch;
        std::function<void(int, std::string&, void*)> m_tcp_f;		//收到tcp数据流时触发的回调函数
        void *m_tcp_args;		//回调参数
    };

    struct tcp_server_conn : public eth_event
    {
        std::string ip_addr;        //连接对端的ip地址
        uint16_t port;              //对端端口
        std::string stream_buffer;  //tcp缓冲流
    };

    class tcp_server_impl : public eth_event
    {
    public:
        tcp_server_impl()
                :m_addr_len(0)
                ,m_app_prt(PRT_NONE)
                ,m_tcp_args(nullptr) {}

        void start_listen(scheduler_impl *impl, uint16_t port);
        static void tcp_server_callback(scheduler *sch, eth_event *ev);
        static void read_tcp_stream(scheduler *sch, eth_event *ev);

        struct sockaddr_in m_accept_addr;
        socklen_t m_addr_len;

        net_socket m_net_sock;			//tcp服务端监听套接字
        APP_PRT m_app_prt;

        //收到tcp数据流时触发的回调函数 @int类型的参数指明是哪一个连接
        std::function<void(int, const std::string&, uint16_t, std::string&, void*)> m_tcp_f;
        void *m_tcp_args;		//回调参数
    };

    extern std::unordered_map<int, std::string> g_ext_type;

    struct http_client_conn : public tcp_client_conn
    {
        int m_status, m_content_len;
        std::unordered_map<std::string, std::string> m_headers;

        http_client_conn() : m_status(-1), m_content_len(-1) {}
    };

    class http_client_impl : public tcp_client_impl
    {
    public:
        http_client_impl() : m_http_args(nullptr) {}

        //检查当前流中是否存在完整的http响应流，对可能的多个响应进行分包处理并执行相应的回调
        void check_http_stream(int fd, http_client_conn *conn);

        std::function<void(int, int, std::unordered_map<std::string, std::string>&, std::string&, void*)> m_http_f;		//响应的回调函数
        void *m_http_args;		//回调参数
    };

    struct http_server_conn : public tcp_server_conn
    {
        int m_content_len;
        std::string m_method;		//请求方法
        std::string m_url;				//url(以"/"开始的字符串)
        std::unordered_map<std::string, std::string> m_headers;

        http_server_conn() : m_content_len(-1) {}
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
        fs_monitor_impl() : m_monitor_args(nullptr) {}

        static void fs_monitory_callback(scheduler *sch, eth_event *ev);
        void recursive_monitor(const std::string& root_dir, bool add, uint32_t mask);
        void trigger_event(bool add, int watch_id, const std::string& path, bool recur_flag, uint32_t mask);

        struct stat m_st;
        std::unordered_map<std::string, std::shared_ptr<monitor_ev>> m_path_mev;
        std::unordered_map<int, std::shared_ptr<monitor_ev>> m_wd_mev;

        //响应的回调函数
        std::function<void(const char*, uint32_t, void *args)> m_monitor_f;
        void *m_monitor_args;   //回调参数
    };

    struct coroutine_impl : public coroutine
    {
        std::function<void(crx::scheduler *sch, void *arg)> f;
        void *arg;

        ucontext_t ctx;
        char *stack;
        size_t capacity, size;

        coroutine_impl()
                :arg(nullptr)
                ,stack(nullptr)
                ,capacity(0)
                ,size(0)
        {
            co_id = 0;
            status = CO_UNKNOWN;
            is_share = false;
        }

        virtual ~coroutine_impl()
        {
            if (stack) {
                delete []stack;
                stack = nullptr;
            }
        }
    };

    class scheduler_impl
    {
    public:
        scheduler_impl(scheduler *sch)
                :m_sch(sch)
                ,m_running_co(-1)
                ,m_next_co(-1)
                ,m_go_done(false)
                ,m_epoll_fd(-1)
                ,m_obj(nullptr)
                ,m_http_client(nullptr)
                ,m_http_server(nullptr)
                ,m_tcp_client(nullptr)
                ,m_tcp_server(nullptr)
                ,m_fs_monitor(nullptr) {}

        virtual ~scheduler_impl() = default;

        size_t co_create(std::function<void(crx::scheduler *sch, void *arg)>& f, void *arg, scheduler *sch,
                      bool is_main_co, bool is_share = false, const char *comment = nullptr);

        void main_coroutine(scheduler *sch);

        void save_stack(coroutine_impl *co, const char *top);

        void add_event(eth_event *ev, uint32_t event = EPOLLIN);

        void remove_event(eth_event *ev);

        /**
         * @fd: 非阻塞文件描述符
         * @read_str: 将读到的数据追加在read_str尾部
         * @return param
         *      -> 1: 等待更多数据可读
         *      -> 0: 所读文件正常关闭
         *      -> -1: 出现异常，异常由errno描述
         */
        int async_read(int fd, std::string& read_str);

        /**
         * @ev: eth_event对象
         * @data: 待写的数据
         */
        void async_write(eth_event *ev, const char *data, size_t len);

    private:
        static void coroutine_wrap(uint32_t low32, uint32_t hi32);

        /**
         * 添加epoll事件，每个事件都采用edge-trigger的模式，只要可读/写，就一直进行读/写，直到不再有数据或者
         * 文件不再可读/出错，因此在此函数中会将文件设置为非阻塞状态，fd应当保证是一个有效的文件描述符
         */
        void handle_event(int op, int fd, uint32_t event);

    public:
        scheduler *m_sch;
        int m_running_co, m_next_co;
        std::vector<coroutine_impl*> m_cos;     //第一个协程为主协程，且在进程的生命周期内常驻内存
        std::list<size_t> m_unused_list;        //those coroutines that unused

        bool m_go_done;
        int m_epoll_fd;		//epoll线程描述符及等待线程终止信号的事件描述符
        std::vector<eth_event*> m_ev_array;
        void *m_obj;        //扩展数据区

        http_client *m_http_client;
        http_server *m_http_server;
        tcp_client *m_tcp_client;
        tcp_server *m_tcp_server;
        fs_monitor *m_fs_monitor;
    };
}