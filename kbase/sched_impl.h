#pragma once

namespace crx
{
    static const int EPOLL_SIZE = 512;

    static const int STACK_SIZE = 256*1024;     //栈大小设置为256K

    class scheduler_impl;
    struct eth_event
    {
        int fd;      //与epoll事件关联的文件描述符
        std::function<void(crx::scheduler *sch, eth_event*)> f;     //回调函数
        eth_event *args;         //回调参数
        scheduler_impl *sch_impl;

        eth_event()
                :fd(-1)
                ,args(nullptr)
                ,sch_impl(nullptr) {}

        virtual ~eth_event() {
            if (-1 != fd && STDIN_FILENO != fd) {
                close(fd);
                fd = -1;
            }
        }
    };

    int simpack_protocol(char *data, size_t len, int& ctx_len);

    class ecs_trans_impl : public eth_event
    {
    public:
        ecs_trans_impl()
                :port(0)
                ,m_sch(nullptr)
                ,m_ctx_len(-1)
                ,m_protocol_args(nullptr)
                ,m_epoll_fd(-1)
                ,m_args(nullptr)
                ,m_ecs_args(nullptr)
        {
            bzero(&m_write_ev, sizeof(m_write_ev));
            bzero(m_pipefd, sizeof(m_pipefd));
        }

        virtual ~ecs_trans_impl()
        {
            if (-1 != m_epoll_fd)
                close(m_epoll_fd);
            if (0 != m_pipefd[1])       //只需要关闭写管道文件描述符
                close(m_pipefd[1]);
        }

        static void ecs_trans_callback(scheduler *sch, eth_event *args);

        static int ecs_simpack(int fd, char *data, size_t len, void *arg)
        {
            auto impl = (ecs_trans_impl*)arg;
            return simpack_protocol(data, len, impl->m_ctx_len);
        }

        static void protocol_hook(int fd, const std::string& ip_addr, uint16_t port,
                                  char *data, size_t len, void *args)
        {
            auto impl = (ecs_trans_impl*)args;
            auto kvs = impl->m_read_se.dump(data, len);
            std::vector<mem_ref> cb_data(kvs.size());
            for (auto& pair : kvs)
                cb_data[atoi(pair.first)] = pair.second;
            impl->m_ecs_f(cb_data, impl->m_ecs_args);
        }

        std::string ip_addr;            //stub variable
        uint16_t port;
        std::string stream_buffer;      //管道流缓冲

        scheduler *m_sch;
        seria m_read_se, m_write_se;
        int m_ctx_len;
        std::function<int(int, char*, size_t, void*)> m_protocol_hook;      //协议钩子
        void *m_protocol_args;  //协议回调参数

        int m_epoll_fd;
        epoll_event m_write_ev;
        int m_pipefd[2];

        //stub as well
        std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> m_f;
        void *m_args;

        std::function<void(std::vector<mem_ref>&, void*)> m_ecs_f;   //执行流回调函数(读)
        void *m_ecs_args;       //回调参数
    };

    class sigctl_impl : public eth_event
    {
    public:
        sigctl_impl()
        {
            sigemptyset(&m_mask);
            bzero(&m_fd_info, sizeof(m_fd_info));
        }
        ~sigctl_impl() override = default;

        static void sigctl_callback(scheduler *sch, eth_event *args);

        void handle_sigs(const std::vector<int>& sigset, bool add);

        sigset_t m_mask;
        signalfd_siginfo m_fd_info;

        std::function<void(int, uint64_t, void*)> m_f;    //信号回调函数
        void *m_args;       //回调参数
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

        std::list<int> m_signals;		//同一个事件可以由多个线程通过发送不同的信号同时触发
        std::function<void(int, void*)> m_f;		//事件触发时的回调函数
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
        gaicb *name_reqs[1];
        addrinfo req_spec;
        sigevent sigev;
        size_t this_co;

        APP_PRT app_prt;
        std::string domain_name;        //连接对端使用的主机地址
        std::string ip_addr;            //转换之后的ip地址
        uint16_t port;					//对端端口

        bool is_connect;
        net_socket conn_sock;
        std::string stream_buffer;      //tcp缓冲流

        tcp_client_conn()
                :this_co(0)
                ,app_prt(PRT_NONE)
                ,port(0)
                ,is_connect(false)
        {
            stream_buffer.reserve(4096);
            name_reqs[0] = new gaicb;
            bzero(name_reqs[0], sizeof(gaicb));
        }

        ~tcp_client_conn() override
        {
            freeaddrinfo(name_reqs[0]->ar_result);
            delete name_reqs[0];
        }
    };

    class tcp_client_impl
    {
    public:
        tcp_client_impl()
                :m_co_id(0)
                ,m_sch(nullptr)
                ,m_app_prt(PRT_NONE)
                ,m_protocol_args(nullptr)
                ,m_args(nullptr) {}

        static void name_resolve_callback(int signo, uint64_t sigval, void *args)
        {
            if (SIGRTMIN+14 == signo) {
                auto tcp_impl = (tcp_client_impl*)args;
                tcp_impl->m_sch->co_yield(sigval);
            }
        }

        static void tcp_client_callback(scheduler *sch, eth_event *ev);

        size_t m_co_id;
        scheduler *m_sch;
        APP_PRT m_app_prt;

        std::function<int(int, char*, size_t, void*)> m_protocol_hook;      //协议钩子
        void *m_protocol_args;  //协议回调参数

        std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> m_f;    //收到tcp数据流时触发的回调函数
        void *m_args;		//回调参数
    };

    struct tcp_server_conn : public eth_event
    {
        std::string ip_addr;        //连接对端的ip地址
        uint16_t port;              //对端端口
        std::string stream_buffer;  //tcp缓冲流

        tcp_server_conn() : port(0)
        {
            stream_buffer.reserve(8192);        //预留8k字节
        }
    };

    class tcp_server_impl : public eth_event
    {
    public:
        tcp_server_impl()
                :m_addr_len(0)
                ,m_app_prt(PRT_NONE)
                ,m_sch(nullptr)
                ,m_args(nullptr) {}

        void start_listen(scheduler_impl *impl, uint16_t port);

        static void tcp_server_callback(scheduler *sch, eth_event *ev);

        static void read_tcp_stream(scheduler *sch, eth_event *ev);

        struct sockaddr_in m_accept_addr;
        socklen_t m_addr_len;

        net_socket m_net_sock;			//tcp服务端监听套接字
        APP_PRT m_app_prt;
        scheduler *m_sch;

        std::function<int(int, char*, size_t, void*)> m_protocol_hook;      //协议钩子
        void *m_protocol_args;  //协议回调参数

        //收到tcp数据流时触发的回调函数 @int类型的参数指明是哪一个连接
        std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> m_f;
        void *m_args;		//回调参数
    };

    extern std::unordered_map<int, std::string> g_ext_type;

    struct http_client_conn : public tcp_client_conn
    {
        int status, content_len;
        std::unordered_map<std::string, const char*> headers;

        http_client_conn() : status(-1), content_len(-1) {}
    };

    class http_client_impl : public tcp_client_impl
    {
    public:
        http_client_impl() : m_http_args(nullptr) {}

        static void protocol_hook(int fd, const std::string& ip_addr, uint16_t port,
                                  char *data, size_t len, void *arg);

        //检查当前流中是否存在完整的http响应流，对可能的多个响应进行分包处理并执行相应的回调
        static int check_http_stream(int fd, char* data, size_t len, void* arg);

        std::function<void(int, int, std::unordered_map<std::string, const char*>&,
                const char*, size_t, void*)> m_http_f;		//响应的回调函数
        void *m_http_args;		//回调参数
    };

    struct http_server_conn : public tcp_server_conn
    {
        int content_len;
        const char *method;		//请求方法
        const char *url;				//url(以"/"开始的字符串)
        std::unordered_map<std::string, const char*> headers;

        http_server_conn() : content_len(-1), method(nullptr), url(nullptr) {}
    };

    class http_server_impl : public tcp_server_impl
    {
    public:
        http_server_impl() : m_http_args(nullptr) {}

        static void protocol_hook(int fd, const std::string& ip_addr, uint16_t port,
                                  char *data, size_t len, void *arg);

        //检查当前流中是否存在完整的http请求流，对可能连续的多个请求进行分包处理并执行相应的回调
        static int check_http_stream(int fd, char* data, size_t len, void* arg);

        //响应的回调函数
        std::function<void(int, const char*, const char*, std::unordered_map<std::string, const char*>&,
                           const char*, size_t, void*)> m_http_f;
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
        SUS_STATUS sus_sts;
        std::function<void(crx::scheduler *sch, void *arg)> f;
        void *arg;

        ucontext_t ctx;
        char *stack;
        size_t capacity, size;

        coroutine_impl()
                :sus_sts(STS_WAIT_EVENT)
                ,arg(nullptr)
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
                ,m_ecs_trans(nullptr)
                ,m_sigctl(nullptr)
                ,m_tcp_client(nullptr)
                ,m_tcp_server(nullptr)
                ,m_http_client(nullptr)
                ,m_http_server(nullptr)
                ,m_fs_monitor(nullptr) {}

        virtual ~scheduler_impl() = default;

    public:
        size_t co_create(std::function<void(crx::scheduler *sch, void *arg)>& f, void *arg, scheduler *sch,
                      bool is_main_co, bool is_share = false, const char *comment = nullptr);

        static void coroutine_wrap(uint32_t low32, uint32_t hi32);

        void main_coroutine(scheduler *sch);

        void save_stack(coroutine_impl *co, const char *top);

    public:
        void add_event(eth_event *ev, uint32_t event = EPOLLIN);

        void remove_event(eth_event *ev);

        /*
         * 添加epoll事件，每个事件都采用edge-trigger的模式，只要可读/写，就一直进行读/写，直到不再有数据或者
         * 文件不再可读/出错，因此在此函数中会将文件设置为非阻塞状态，fd应当保证是一个有效的文件描述符
         */
        void handle_event(int op, int fd, uint32_t event);

        /*
         * @fd: 非阻塞文件描述符
         * @read_str: 将读到的数据追加在read_str尾部
         * @return param
         *      -> 1: 等待更多数据可读
         *      -> 0: 所读文件正常关闭
         *      -> -1: 出现异常，异常由errno描述
         */
        int async_read(eth_event *ev, std::string& read_str);

        /*
         * @ev: eth_event对象
         * @data: 待写的数据
         */
        void async_write(eth_event *ev, const char *data, size_t len);

    public:
        scheduler *m_sch;
        int m_running_co, m_next_co;
        std::vector<coroutine_impl*> m_cos;     //第一个协程为主协程，且在进程的生命周期内常驻内存
        std::vector<size_t> m_unused_cos;       //those coroutines that unused

        bool m_go_done;
        int m_epoll_fd;		//epoll线程描述符及等待线程终止信号的事件描述符
        std::vector<eth_event*> m_ev_array;
        void *m_obj;        //扩展数据区

        ecs_trans *m_ecs_trans;
        sigctl *m_sigctl;

        tcp_client *m_tcp_client;
        tcp_server *m_tcp_server;
        http_client *m_http_client;
        http_server *m_http_server;
        fs_monitor *m_fs_monitor;
    };

    template<typename IMPL_TYPE, typename CONN_TYPE>
    void handle_stream(int conn, IMPL_TYPE impl, CONN_TYPE conn_ins)
    {
        if (conn_ins->stream_buffer.empty())
            return;

        auto sch_impl = (scheduler_impl*)impl->m_sch->m_obj;
        if (impl->m_protocol_hook) {
            char *start = &conn_ins->stream_buffer[0];
            size_t buf_len = conn_ins->stream_buffer.size(), read_len = 0;
            while (read_len < buf_len) {
                size_t remain_len = buf_len-read_len;
                int ret = impl->m_protocol_hook(conn, start, remain_len, impl->m_protocol_args);
                if (0 == ret)
                    break;

                int abs_ret = std::abs(ret);
                assert(abs_ret <= remain_len);
                if (ret > 0)
                    impl->m_f(conn_ins->fd, conn_ins->ip_addr, conn_ins->port,
                              start, ret, impl->m_args);

                start += abs_ret;
                read_len += abs_ret;
            }
            if (read_len && sch_impl->m_ev_array[conn])
                conn_ins->stream_buffer.erase(0, read_len);
        } else {
            impl->m_f(conn_ins->fd, conn_ins->ip_addr, conn_ins->port, &conn_ins->stream_buffer[0],
                      conn_ins->stream_buffer.size(), impl->m_args);
        }
    }
}