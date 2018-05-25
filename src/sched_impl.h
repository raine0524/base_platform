#pragma once

namespace crx
{
    static const int EPOLL_SIZE = 512;

    static const int STACK_SIZE = 256*1024;     //栈大小设置为256K

    class scheduler_impl;
    class eth_event : public impl
    {
    public:
        eth_event() :fd(-1) {}
        virtual ~eth_event()
        {
            if (-1 != fd && STDIN_FILENO != fd)
                close(fd);
        }

        /*
         * @read_str: 将读到的数据追加在read_str尾部
         * @return param
         *      -> 1: 等待更多数据可读
         *      -> 0: 所读文件正常关闭
         *      -> -1: 出现异常，异常由errno描述
         */
        int async_read(std::string& read_str);

        int fd;      //与epoll事件关联的文件描述符
        std::function<void()> f;     //回调函数
        std::weak_ptr<scheduler_impl> sch_impl;
    };

#define GET_BIT(field, n)   (field & 1<<n)

#define SET_BIT(field, n)   (field |= 1<<n)

#define CLR_BIT(field, n)   (field &= ~(1<<n))

#pragma pack(1)
    /*
     * simp协议的头部，此处是对ctl_flag字段更详细的表述：
     *          -->第0位: 1-表示当前请求由库这一层处理  0-表示路由给上层应用
     *          -->第1位: 1-表示推送notify 0-非推送
     *          -->第2位: 当为非推送时这一位有效 1-request 0-response
     *          -->第3位: 1-表示registry发送的数据 0-其他服务发送
     *          -->第31位: 1-加密(暂不支持) 0-非加密
     *          其余字段暂时保留
     */
    struct simp_header
    {
        uint32_t magic_num;         //魔数，4个字节依次为0x5f3759df
        uint32_t version;           //版本，填入发布日期，比如1.0.0版本的值设置为20180501
        uint32_t length;            //body部分长度，整个一帧的大小为sizeof(simp_header)+length
        uint16_t type;              //类型
        uint16_t cmd;               //命令
        uint16_t result;            //请求结果
        uint32_t ses_id;            //会话id
        uint32_t req_id;            //请求id
        uint32_t ctl_flag;          //控制字段
        unsigned char token[16];    //请求携带token，表明请求合法(token=md5(current_timestamp+name))

        simp_header()
        {
            bzero(this, sizeof(simp_header));
            magic_num = htonl(0x5f3759df);
            version = htonl(20180501);
        }
    };
#pragma pack()

    int simpack_protocol(char *data, size_t len);

    class sigctl_impl : public eth_event
    {
    public:
        sigctl_impl()
        {
            sigemptyset(&m_mask);
            bzero(&m_fd_info, sizeof(m_fd_info));
        }

        void sigctl_callback();

        void handle_sig(int signo, bool add);

        sigset_t m_mask;
        signalfd_siginfo m_fd_info;
        std::unordered_map<int, std::function<void(uint64_t)>> m_sig_cb;
    };

    class timer_impl : public eth_event
    {
    public:
        timer_impl() : m_delay(0), m_interval(0) {}

        void timer_callback();

        uint64_t m_delay, m_interval;        //分别对应于首次触发时的延迟时间以及周期性触发时的间隔时间
        std::function<void()> m_f;
    };

    class timer_wheel_impl : public impl
    {
    public:
        timer_wheel_impl() : m_slot_idx(0) {}

        virtual ~timer_wheel_impl() { m_timer.detach(); }

        void timer_wheel_callback();

        timer m_timer;
        size_t m_slot_idx;
        std::vector<std::list<std::function<void()>>> m_slots;
    };

    class event_impl : public eth_event
    {
    public:
        void event_callback();

        std::list<int> m_signals;		//同一个事件可以由多个线程通过发送不同的信号同时触发
        std::function<void(int)> m_f;
    };

    class udp_ins_impl : public eth_event
    {
    public:
        udp_ins_impl() : m_recv_buffer(65536, 0)
        {
            bzero(&m_send_addr, sizeof(m_send_addr));
        }

        void udp_ins_callback();

        struct sockaddr_in m_send_addr, m_recv_addr;
        socklen_t m_recv_len;

        net_socket m_net_sock;			//udp套接字
        std::string m_recv_buffer;		//接收缓冲区
        std::function<void(const std::string&, uint16_t, char*, size_t)> m_f;       //收到数据时触发的回调函数
    };

    enum APP_PRT    //应用层协议的类型
    {
        PRT_NONE = 0,       //使用原始的传输层协议
        PRT_HTTP,			//HTTP协议
        PRT_SIMP,           //SIMP协议(私有)
    };

    //只有tcp套接字才需要异步写操作
    class tcp_event : public eth_event
    {
    public:
        tcp_event() :is_connect(false), event(EPOLLIN)
        {
            stream_buffer.reserve(8192);
        }

        int async_write(const char *data, size_t len);

        uint32_t event;
        std::list<std::string> cache_data;      //缓存队列，等待可写事件
        std::shared_ptr<impl> ext_data;         //扩展数据

        bool is_connect;
        std::string ip_addr;            //转换之后的ip地址
        net_socket conn_sock;
        std::string stream_buffer;      //tcp缓冲流
    };

    class tcp_impl_xutil
    {
    public:
        tcp_impl_xutil() : m_sch(nullptr), m_app_prt(PRT_NONE) {}

        scheduler *m_sch;
        APP_PRT m_app_prt;

        //tcp_client需要一个秒盘做重连，tcp_server需要一个分钟盘做会话管理
        timer_wheel m_timer_wheel;
        std::function<int(int, char*, size_t)> m_protocol_hook;      //协议钩子
        std::function<void(int, const std::string&, uint16_t, char*, size_t)> m_f;    //收到tcp数据流时触发的回调函数
    };

    class tcp_client_impl;
    class tcp_client_conn : public tcp_event
    {
    public:
        tcp_client_conn() :cnt(0), last_conn(1)
        {
            name_reqs[0] = new gaicb;
            bzero(name_reqs[0], sizeof(gaicb));
        }

        virtual ~tcp_client_conn()
        {
            freeaddrinfo(name_reqs[0]->ar_result);
            delete name_reqs[0];
        }

        void tcp_client_callback();

        void retry_connect();

        //解析域名时需要用到的相关设施
        gaicb *name_reqs[1];
        addrinfo req_spec;
        sigevent sigev;

        std::shared_ptr<tcp_client_impl> tcp_impl;
        std::string domain_name;        //连接对端使用的主机地址
        int retry, timeout, cnt, last_conn;
    };

    class tcp_client_impl : public impl
    {
    public:
        void name_resolve_callback(uint64_t sigval)
        {
            m_util.m_sch->co_yield(sigval);
        }

        tcp_impl_xutil m_util;
    };

    class tcp_server_impl;
    class tcp_server_conn : public tcp_event
    {
    public:
        void read_tcp_stream();

        tcp_server_impl *tcp_impl;
    };

    class tcp_server_impl : public tcp_event
    {
    public:
        tcp_server_impl() : m_addr_len(0) {}

        void tcp_server_callback();

        struct sockaddr_in m_accept_addr;
        socklen_t m_addr_len;
        tcp_impl_xutil m_util;
    };

    extern std::unordered_map<int, std::string> g_ext_type;

    struct http_xutil
    {
        int status, content_len;
        const char *method, *url;		//请求方法/url(以"/"开始的字符串)
        std::unordered_map<std::string, const char*> headers;

        http_xutil() : content_len(-1) {}
    };

    struct http_funcs
    {
        std::function<void(int, int, std::unordered_map<std::string, const char*>&, char*, size_t)> m_http_cli;
        std::function<void(int, const char*, const char*, std::unordered_map<std::string, const char*>&, char*, size_t)> m_http_svr;
    };

    template <typename CONN_TYPE>
    struct http_conn_t : public CONN_TYPE
    {
        http_xutil xutil;
    };

    template <typename IMPL_TYPE>
    struct http_impl_t : public IMPL_TYPE
    {
        http_funcs funcs;
    };

    struct simpack_xutil : public impl
    {
        server_info info;
        unsigned char token[16];

        //for registry
        int listen;
        std::unordered_set<std::string> clients;
    };

    class simpack_server_impl : public impl
    {
    public:
        simpack_server_impl()
                :m_registry_conn(-1)
                ,m_log_conn(-1)
                ,m_seria(true)
        {
            simp_header stub_header;
            m_simp_buf = std::string((const char*)&stub_header, sizeof(simp_header));
        }

        void stop();

        void simp_callback(int conn, const std::string& ip, uint16_t port, char *data, size_t len);

        void capture_sharding(bool registry, int conn, const std::string &ip, uint16_t port, char *data, size_t len);

        void handle_reg_name(int conn, unsigned char *token, std::unordered_map<std::string, mem_ref>& kvs);

        void handle_svr_online(unsigned char *token, std::unordered_map<std::string, mem_ref>& kvs);

        void handle_hello_request(int conn, const std::string &ip, uint16_t port, unsigned char *token,
                                  std::unordered_map<std::string, mem_ref>& kvs);

        void handle_hello_response(int conn, uint16_t result, unsigned char *token, std::unordered_map<std::string, mem_ref>& kvs);

        void say_goodbye(std::shared_ptr<simpack_xutil>& xutil);

        void handle_goodbye(int conn);

        void send_data(int type, int conn, const server_cmd& cmd, const char *data, size_t len);

        void send_package(int type, int conn, const server_cmd& cmd, bool lib_proc,
                          unsigned char *token, const char *data, size_t len);

        scheduler *m_sch;
        int m_registry_conn;
        std::string m_reg_str;
        std::unordered_set<int> m_ordinary_conn;

        int m_log_conn;
        std::list<std::string> m_cache_logs;

        seria m_seria;
        server_cmd m_app_cmd;
        std::string m_simp_buf;

        tcp_client m_client;
        tcp_server m_server;

        std::function<void(const server_info&)> m_on_connect;
        std::function<void(const server_info&)> m_on_disconnect;
        std::function<void(const server_info&, const server_cmd&, char*, size_t)> m_on_request;
        std::function<void(const server_info&, const server_cmd&, char*, size_t)> m_on_response;
        std::function<void(const server_info&, const server_cmd&, char*, size_t)> m_on_notify;
    };

    class log_impl : public impl
    {
    public:
        log_impl()
                :m_split_idx(0)
                ,m_last_sec(-1)
                ,m_fmt_buf(1024, 0)
                ,m_log_buf(65536, 0)
                ,m_fp(nullptr)
                ,m_seria(true)
                ,m_log_idx(0)
        {
            bzero(&m_cmd, sizeof(m_cmd));
        }

        virtual ~log_impl()
        {
            if (m_fp)
                fclose(m_fp);
        }

        std::string get_log_path(bool create);

        void create_log_file(const char *log_file);

        void rotate_log(bool create_dir);

        bool get_local_log(scheduler *sch, std::shared_ptr<scheduler_impl>& sch_impl);

        void write_local_log(const char *data, size_t len);

        bool get_remote_log(std::shared_ptr<scheduler_impl>& sch_impl);

        std::string m_prefix;
        std::string m_root_dir;
        int64_t m_max_size, m_curr_size;
        int m_split_idx;
        std::shared_ptr<simpack_server_impl> m_simp_impl;

        datetime m_now;
        int64_t m_last_sec, m_last_hour, m_last_day;
        std::string m_fmt_buf, m_fmt_tmp, m_log_buf;
        FILE *m_fp;

        server_cmd m_cmd;
        seria m_seria;
        size_t m_log_idx;
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
        void fs_monitory_callback();

        void recursive_monitor(const std::string& root_dir, bool add, uint32_t mask);

        void trigger_event(bool add, int watch_id, const std::string& path, bool recur_flag, uint32_t mask);

        struct stat m_st;
        std::unordered_map<std::string, std::shared_ptr<monitor_ev>> m_path_mev;
        std::unordered_map<int, std::shared_ptr<monitor_ev>> m_wd_mev;
        std::function<void(const char*, uint32_t)> m_monitor_f;
    };

    struct coroutine_impl : public coroutine
    {
        SUS_TYPE type;
        std::function<void(crx::scheduler *sch)> f;

        ucontext_t ctx;
        std::string stack;
        size_t size;

        coroutine_impl() : type(WAIT_EVENT), size(0)
        {
            co_id = 0;
            status = CO_UNKNOWN;
            is_share = false;
        }
    };

    enum
    {
        SIG_CTL = 0,
        TCP_CLI,
        TCP_SVR,
        HTTP_CLI,
        HTTP_SVR,
        SIMP_SVR,
        FS_MONI,
        EXT_DATA,
        IDX_MAX,
    };

    class scheduler_impl : public impl
    {
    public:
        scheduler_impl()
                :m_running_co(0)
                ,m_next_co(-1)
                ,m_epoll_fd(-1)
                ,m_remote_log(false)
        {
            m_util_impls.resize(IDX_MAX);
        }

        virtual ~scheduler_impl() = default;

    public:
        size_t co_create(std::function<void(crx::scheduler *sch)>& f, scheduler *sch,
                         bool is_main_co, bool is_share = false, const char *comment = nullptr);

        static void coroutine_wrap(uint32_t low32, uint32_t hi32);

        void main_coroutine(scheduler *sch);

        void save_stack(std::shared_ptr<coroutine_impl>& co_impl, const char *top);

        void add_event(std::shared_ptr<eth_event> ev, uint32_t event = EPOLLIN);

        void remove_event(int fd);

        /*
         * 添加epoll事件，每个事件都采用edge-trigger的模式，只要可读/写，就一直进行读/写，直到不再有数据或者
         * 文件不再可读/出错，因此在此函数中会将文件设置为非阻塞状态，fd应当保证是一个有效的文件描述符
         */
        void handle_event(int op, int fd, uint32_t event);

        void flush_log_buffer();

    public:
        std::string m_ini_file;
        int m_running_co, m_next_co;
        std::vector<std::shared_ptr<coroutine_impl>> m_cos;     //第一个协程为主协程，且在进程的生命周期内常驻内存
        std::vector<size_t> m_unused_cos;                       //those coroutines that unused

        bool m_go_done;
        int m_epoll_fd;		//epoll线程描述符及等待线程终止信号的事件描述符
        std::vector<std::shared_ptr<eth_event>> m_ev_array;

        bool m_remote_log;
        timer_wheel m_sec_wheel;
        std::vector<std::shared_ptr<impl>> m_util_impls;
    };

    template<typename CONN_TYPE>
    void handle_stream(int conn, CONN_TYPE conn_ins)
    {
        if (conn_ins->stream_buffer.empty())
            return;

        auto sch_impl = conn_ins->sch_impl.lock();
        if (conn_ins->tcp_impl->m_util.m_protocol_hook) {
            conn_ins->stream_buffer.push_back(0);
            char *start = &conn_ins->stream_buffer[0];
            size_t buf_len = conn_ins->stream_buffer.size()-1, read_len = 0;
            while (read_len < buf_len) {
                size_t remain_len = buf_len-read_len;
                int ret = conn_ins->tcp_impl->m_util.m_protocol_hook(conn, start, remain_len);
                if (0 == ret) {
                    conn_ins->stream_buffer.pop_back();
                    break;
                }

                int abs_ret = std::abs(ret);
                assert(abs_ret <= remain_len);
                if (ret > 0)
                    conn_ins->tcp_impl->m_util.m_f(conn_ins->fd, conn_ins->ip_addr,
                                                   conn_ins->conn_sock.m_port, start, ret);

                start += abs_ret;
                read_len += abs_ret;
            }

            if (read_len && sch_impl->m_ev_array[conn]) {
                if (read_len == buf_len) {
                    conn_ins->stream_buffer.clear();
                } else {
                    conn_ins->stream_buffer.erase(0, read_len);
                    conn_ins->stream_buffer.pop_back();
                }
            }
        } else {
            conn_ins->tcp_impl->m_util.m_f(conn_ins->fd, conn_ins->ip_addr, conn_ins->conn_sock.m_port,
                                           &conn_ins->stream_buffer[0], conn_ins->stream_buffer.size());
        }
    }

    template <typename IMPL_TYPE, typename CONN_TYPE>
    void tcp_callback_for_http(bool client, IMPL_TYPE impl, int fd, char *data, size_t len)
    {
        if (!data)      //tcp连接开启/关闭时同样将调用回调函数，此时直接返回
            return;

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(impl->m_util.m_sch->m_impl);
        auto conn = std::dynamic_pointer_cast<CONN_TYPE>(sch_impl->m_ev_array[fd]);

        char *cb_data = nullptr;
        size_t cb_len = 0;
        if (len != 1 || '\n' != *data) {
            cb_data = data;
            cb_len = (size_t) conn->xutil.content_len;
        }

        if (client)
            impl->funcs.m_http_cli(fd, conn->xutil.status, conn->xutil.headers, cb_data, cb_len);
        else
            impl->funcs.m_http_svr(fd, conn->xutil.method, conn->xutil.url, conn->xutil.headers, cb_data, cb_len);
        if (sch_impl->m_ev_array[fd]) {
            conn->xutil.content_len = -1;
            conn->xutil.headers.clear();
        }
    }

    /*
     * 一个HTTP响应/请求流例子如下所示：
     * [响应行-HTTP/1.1 200 OK][请求行-POST /request HTTP/1.1]
     * Accept-Ranges: bytes
     * Connection: close
     * Pragma: no-cache
     * Content-Type: text/plain
     * Content-Length: 12("Hello World!"字节长度)
     *
     * Hello World!
     */
    template <typename CONN_TYPE>
    int http_parser(bool client, CONN_TYPE conn, char* data, size_t len)
    {
        if (-1 == conn->xutil.content_len) {        //与之前的http响应流已完成分包处理，开始处理新的响应
            if (client) {           //client的响应行与server的请求行存在差异
                if (len < 4)        //首先验证流的开始4个字节是否为签名"HTTP"
                    return 0;

                if (strncmp(data, "HTTP", 4)) {     //签名出错，该流不是HTTP流
                    char *sig_pos = strstr(data, "HTTP");       //查找流中是否存在"HTTP"子串
                    if (!sig_pos || sig_pos > data+len-4) {     //未找到子串或找到的子串已超出查找范围，无效的流
                        return -(int)len;
                    } else {        //找到子串
                        return -(int)(sig_pos-data);    //先截断签名之前多余的数据
                    }
                }
            }

            //先判断是否已经获取到完整的请求/响应头
            char *delim_pos = strstr(data, "\r\n\r\n");
            if (!delim_pos || delim_pos > data+len-4) {     //还未取到分隔符或分隔符已超出查找范围
                if (len > 8192)
                    return -8192;      //在取到分隔符\r\n\r\n前已有超过8k的数据，截断保护缓冲
                else
                    return 0;           //等待更多数据到来
            }

            if (!client) {
                char *sig_pos = strstr(data, "HTTP");       //在完整的请求头中查找是否存在"HTTP"签名
                if (!sig_pos || sig_pos > delim_pos)        //未取到签名或已超出查找范围，截断该请求头
                    return -(int)(delim_pos-data+4);
            }

            size_t valid_header_len = delim_pos-data;
            auto str_vec = split(data, valid_header_len, "\r\n");
            if (str_vec.size() <= 1)        //HTTP流的头部中包含一个请求/响应行以及至少一个头部字段
                return -(int)(valid_header_len+4);

            bool header_err = false;
            for (size_t i = 0; i < str_vec.size(); ++i) {
                auto& line = str_vec[i];
                if (0 == i) {		//首先解析请求/响应行
                    auto line_elem = split(line.data, line.len, " ");
                    if (line_elem.size() < 3) {		//请求/响应行出错，无效的请求流
                        header_err = true;
                        break;
                    }

                    if (client) {
                        //响应行包含空格分隔的三个字段，例如：HTTP/1.1(协议/版本) 200(状态码) OK(简单描述)
                        conn->xutil.status = std::atoi(line_elem[1].data);		//记录中间的状态码
                    } else {
                        //请求行包含空格分隔的三个字段，例如：POST(请求方法) /request(url) HTTP/1.1(协议/版本)
                        conn->xutil.method = line_elem[0].data;          //记录请求方法
                        *(char*)(line_elem[0].data+line_elem[0].len) = 0;
                        conn->xutil.url = line_elem[1].data;             //记录请求的url(以"/"开始的部分)
                        *(char*)(line_elem[1].data+line_elem[1].len) = 0;
                    }
                } else {
                    auto header = split(line.data, line.len, ": ");		//头部字段键值对的分隔符为": "
                    if (header.size() >= 2) {       //无效的头部字段直接丢弃，不符合格式的值发生截断
                        *(char*)(header[1].data+header[1].len) = 0;
                        conn->xutil.headers[std::string(header[0].data, header[0].len)] = header[1].data;
                    }
                }
            }

            auto it = conn->xutil.headers.find("Content-Length");
            if (header_err || (client && conn->xutil.headers.end() == it)) {  //头部有误或响应行中未找到"Content-Length"字段，截断该头部
                conn->xutil.headers.clear();
                return -(int)(valid_header_len+4);
            }

            if (!client && conn->xutil.headers.end() == it) {     //有些请求流不存在请求体，此时头部中不包含"Content-Length"字段
                //just a trick，返回0表示等待更多的数据，如果需要上层执行m_f回调必须返回大于0的值，因此在完成一次分包之后，
                //若没有请求体，可以将content_len设置为1，但只截断分隔符"\r\n\r\n"中的前三个字符，而将最后一个字符作为请求体
                conn->xutil.content_len = 1;
                return -(int)(valid_header_len+3);
            }

            conn->xutil.content_len = std::stoi(it->second);
            if (client && conn->xutil.content_len == 0) {
                conn->xutil.content_len = 1;
                return -(int)(valid_header_len+3);
            }
            return -(int)(valid_header_len+4);
        }

        if (len < conn->xutil.content_len-1)      //请求/响应体中不包含足够的数据，等待该连接上更多的数据到来
            return 0;
        else        //已取到请求/响应体，通知上层执行m_f回调
            return (len > conn->xutil.content_len) ? conn->xutil.content_len : (int)len;
    }
}