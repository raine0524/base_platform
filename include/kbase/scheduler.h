#pragma once

namespace crx
{
    enum CO_STATUS
    {
        CO_UNKNOWN = 0,
        CO_READY,
        CO_RUNNING,
        CO_SUSPEND,
    };

    enum SUS_STATUS
    {
        STS_WAIT_EVENT = 0,     //等待注册事件触发后回切本协程
        STS_HAVE_REST,          //短暂的休息一段时间，若无其他事件处理，那么主协程会在一个时间片之后切回本协程
    };

    struct coroutine
    {
        size_t co_id;       //协程id
        CO_STATUS status;   //当前运行状态
        bool is_share;      //是否使用共享栈
        char comment[64];   //简述
    };

    //NOTE：手动释放的资源可同时申请多个，自动释放的资源多次申请将返回同一个实例
    class CRX_SHARE scheduler : public kobj
    {
    public:
        scheduler();
        virtual ~scheduler();

        //创建一个协程, @share参数表示是否使用共享栈，创建成功返回该协程的id(> 0)
        size_t co_create(std::function<void(scheduler *sch, void *arg)> f, void *arg,
                         bool is_share = false, const char *comment = nullptr);

        /*
         * 切换执行流，@co_id表示切换至哪个协程，若其为0则代表切换回主协程
         * 切换成功返回true，失败则返回false，表明待切换的协程已失效(还未创建或已执行完退出)
         */
        bool co_yield(size_t co_id, SUS_STATUS sts = STS_WAIT_EVENT);

        //获取当前调度器中所有可用的协程，可用指协程状态为CO_READY, CO_RUNNING, CO_SUSPEND之一
        std::vector<coroutine*> get_avail_cos();

        /*
         * 获取日志实例(自动释放，但多次申请将返回多个不同的实例)
         * @prefix 日志文件的前缀
         * @root_dir 设置日志的根目录，将在根目录基础上按照年/月/日逐级构造子目录
         * @max_size 文件切割大小(单位为MB)
         * @print_screen 是否在写文件的同时将日志打印在屏幕上
         */
        log* get_log(const char *prefix, const char *root_dir = "log_files",
                     int max_size = 2, bool print_screen = true);

        //获取signal实例(自动释放)，回调函数中的3个参数依次为信号量、信号量关联参数以及回调参数
        sigctl* get_sigctl(std::function<void(int, uint64_t, void*)> f, void *arg = nullptr);

        //获取timer实例(需手动释放)
        timer* get_timer(std::function<void(void*)> f, void *arg = nullptr);

        //获取event实例(需手动释放)
        event* get_event(std::function<void(int, void*)> f, void *arg = nullptr);

        /*
         * 获取udp实例(需手动释放)
         * @is_server：为true表明创建的是服务端使用的udp套接字，反之则为客户端使用的套接字
         * @port：udp是无连接的传输层协议，因此在创建套接字时不需要显示指定ip地址，但udp服务器端在接收请求时
         * 				需要显示指定监听的端口，若port为0，则系统将为该套接字选择一个随机的端口
         * 	@f：回调函数，函数的5个参数分别为对端的ip地址、端口、接收到的udp包和大小，以及回调参数
         * 	@arg：回调参数
         */
        udp_ins* get_udp_ins(bool is_server, uint16_t port,
                             std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> f,
                             void *arg = nullptr);

        /*
         * 注册tcp钩子，这个函数将在收到tcp流之后回调，主要用于定制应用层协议，并将协议与原始的tcp流进行解耦
         * @param client 若为true，则为tcp_client注册该钩子，只有收到tcp响应流时才会触发该回掉，否则为tcp_server注册
         * @param f
         *          --> @param① int    指定的连接，不同的连接有不同的流上下文，因此应用层需要针对每个连接分别维护其上下文
         *          --> @param② char*  当前可用于解析的tcp流的首地址
         *          --> @param③ size_t 可用于解析的tcp流的长度，上层应在解析时判断是否在安全范围内
         *          --> @param④ arg   回掉参数
         *          --> @return ret 返回 =0 时，通知下层需要有跟多的数据才能完整的解析一次协议请求
         *                          返回 <0 时，通知下层只需要截断以data为首地址，abs(ret)长度的流即可
         *                          返回 >0 时，通知下层可以执行get_tcp_client/server这两个函数中注册的回掉函数，在执行完
         *                                     回调之后将截断data为首地址，ret长度的流
         *                          ** 注意这里有一个重要的trick，在执行get_tcp_client/server函数中的回掉时，tcp数据流中的长度
         *                             即为此处的返回值
         * @param arg 回掉参数
         */
        void register_tcp_hook(bool client, std::function<int(int, char*, size_t, void*)> f, void *arg = nullptr);

        //获取tcp客户端实例(自动释放)，回调函数的3个参数分别为指定的连接，收到的tcp数据流以及回调参数
        tcp_client* get_tcp_client(std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> f,
                                   void *arg = nullptr);

        /*
         * 获取tcp服务实例(自动释放)
         * @port：指示tcp服务将在哪个端口上进行监听，若port为0，则系统将随机选择一个可用端口
         * @f：回调函数，函数的6个参数分别为指定的连接，连接的ip地址/端口，收到的tcp数据流以及回调参数
         * @arg：回调参数
         */
        tcp_server* get_tcp_server(uint16_t port,
                                   std::function<void(int, const std::string&, uint16_t, char*, size_t, void*)> f,
                                   void *arg = nullptr);

        /*
         * 获取http客户端实例(自动释放)，回调函数中的5个参数依次为
         * ①指定的连接
         * ②响应标志(200, 404等等)
         * ③头部键值对
         * ④响应体
         * ⑤回调参数
         */
        http_client* get_http_client(std::function<void(int, int, std::unordered_map<std::string, const char*>&, const char*, size_t, void*)> f,
                                     void *arg = nullptr);

        /*
         * 获取http服务实例(自动释放)，回调函数中的6个参数依次为
         * ①指定的连接
         * ②请求方法(例如"GET", "POST"等等)
         * ③url(以"/"起始的字符串，例如"/index.html")
         * ④头部键值对
         * ⑤请求体(可能不存在)
         * ⑥回调参数
         */
        http_server* get_http_server(uint16_t port,
                                     std::function<void(int, const char*, const char*, std::unordered_map<std::string, const char*>&, const char*, size_t, void*)> f,
                                     void *arg = nullptr);

        //获取simpack服务实例(自动释放)，主要用于分布式系统中可控服务节点之间的通信
        simpack_server* get_simpack_server(void *arg);

        /*
         * 获取文件系统监控实例(自动释放)，回调函数中的6个参数依次为
         * ①触发监控事件的文件
         * ②监控文件的掩码，用于确定触发事件的类型
         * ③回调参数
         */
        fs_monitor* get_fs_monitor(std::function<void(const char*, uint32_t, void *arg)> f, void *arg = nullptr);
    };
}