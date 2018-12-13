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

    enum SUS_TYPE
    {
        WAIT_EVENT = 0,     //等待注册事件触发后回切本协程
        HAVE_REST,          //短暂的休息一段时间，若无其他事件处理，那么主协程会在一个时间片之后切回本协程
    };

    struct coroutine
    {
        size_t co_id;       //协程id
        CO_STATUS status;   //当前运行状态
        bool is_share;      //是否使用共享栈
        char comment[64];   //简述
    };

    class CRX_SHARE scheduler : public kobj
    {
    public:
        scheduler();
        virtual ~scheduler();

        //创建一个协程, @share参数表示是否使用共享栈，创建成功返回该协程的id(> 0)
        size_t co_create(std::function<void(scheduler *sch, size_t co_id)> f,
                bool is_share = false, const char *comment = nullptr);

        /*
         * 切换执行流，@co_id表示切换至哪个协程，若其为0则代表切换回主协程
         * 切换成功返回true，失败则返回false，表明待切换的协程已失效(还未创建或已执行完退出)
         */
        void co_yield(size_t co_id, SUS_TYPE type = WAIT_EVENT);

        /*
         * 在当前协程中睡眠seconds秒,该接口对于主协程无效,也即应用层必须创建一个协程
         * seconds的取值范围为[0, 60)
         */
        void co_sleep(int seconds);

        //获取当前调度器中所有可用的协程，可用指协程状态为CO_READY, CO_RUNNING, CO_SUSPEND之一
        std::vector<std::shared_ptr<coroutine>> get_avail_cos();

        //[单例] 获取系统信号监听及处理实例(自动释放)
        sigctl get_sigctl();

        //获取timer实例(手动释放,多次申请将返回多个不同的实例)
        timer get_timer(std::function<void(int64_t)> f, int64_t cb_arg = 0);

        //[单例] 获取定时轮timer_wheel实例(自动释放)
        timer_wheel get_timer_wheel();

        //获取event实例(手动释放,多次申请将返回多个不同实例)
        event get_event(std::function<void(int)> f);

        /*
         * 获取udp实例(手动释放,多次申请将返回多个不同实例)
         * @is_server：为true表明创建的是服务端使用的udp套接字，反之则为客户端使用的套接字
         * @port：udp是无连接的传输层协议，因此在创建套接字时不需要显示指定ip地址，但udp服务器端在接收请求时
         * 				需要显示指定监听的端口，若port为0，则系统将为该套接字选择一个随机的端口
         * 	@f：回调函数，函数的4个参数分别为对端的ip地址、端口、接收到的udp包和大小
         */
        udp_ins get_udp_ins(bool is_server, uint16_t port,
                std::function<void(const std::string&, uint16_t, char*, size_t)> f);

        /*
         * 注册tcp钩子，这个函数将在收到tcp流之后回调，主要用于定制应用层协议，并将协议与原始的tcp流进行解耦
         * @param client 若为true，则为tcp_client注册该钩子，只有收到tcp响应流时才会触发该回掉，否则为tcp_server注册
         * @param f
         *          --> @param① int    指定的连接，不同的连接有不同的流上下文，因此应用层需要针对每个连接分别维护其上下文
         *          --> @param② char*  当前可用于解析的tcp流的首地址
         *          --> @param③ size_t 可用于解析的tcp流的长度，上层应在解析时判断是否在安全范围内
         *          --> @return ret 返回 =0 时，通知下层需要有跟多的数据才能完整的解析一次协议请求
         *                          返回 <0 时，通知下层只需要截断以data为首地址，abs(ret)长度的流即可
         *                          返回 >0 时，通知下层可以执行get_tcp_client/server这两个函数中注册的回掉函数，在执行完
         *                                     回调之后将截断data为首地址，ret长度的流
         *                          ** 注意这里有一个重要的trick，在执行get_tcp_client/server函数中的回掉时，tcp数据流中的长度
         *                             即为此处的返回值
         */
        void register_tcp_hook(bool client, std::function<int(int, char*, size_t)> f);

        /*
         * [单例] 获取tcp客户端实例(自动释放),回调函数中的参数依次为
         * ①指定的连接
         * ②连接的ip地址/端口
         * ③tcp数据流
         */
        tcp_client get_tcp_client(std::function<void(int, const std::string&, uint16_t, char*, size_t)> f);

        /*
         * [单例] 获取tcp服务实例(自动释放)
         * @port：指示tcp服务将在哪个端口上进行监听
         *        --> 当port >= 0时,将创建一个tcp套接字. 其中若port为0，系统将随机选择一个可用端口
         *        --> 当port <  0时,将创建一个unix域套接字.
         * @f：回调函数，函数参数分别为指定的连接,连接的ip地址/端口,收到的tcp数据流
         */
        tcp_server get_tcp_server(int port, std::function<void(int, const std::string&, uint16_t, char*, size_t)> f);

        /*
         * [单例] 获取http客户端实例(自动释放)，回调函数中的参数依次为
         * ①指定的连接
         * ②响应标志(200, 404等等)
         * ③头部键值对
         * ④响应体
         */
        http_client get_http_client(std::function<void(int, int, std::map<std::string, const char*>&, char*, size_t)> f);

        /*
         * [单例] 获取http服务实例(自动释放),此处的port与tcp_server中的port含义相同,回调函数中的参数依次为
         * ①指定的连接
         * ②请求方法(例如"GET", "POST"等等)
         * ③url(以"/"起始的字符串，例如"/index.html")
         * ④头部键值对
         * ⑤请求体(可能不存在)
         */
        http_server get_http_server(int port,
                std::function<void(int, const char*, const char*, std::map<std::string, const char*>&, char*, size_t)> f);
    };
}
