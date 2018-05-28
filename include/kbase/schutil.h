#pragma once

namespace crx
{
    class CRX_SHARE sigctl : public kobj
    {
    public:
        //增加监听信号及其回调函数
        void add_sig(int signo, std::function<void(uint64_t)> callback);

        //移除监听信号
        void remove_sig(int signo);
    };

    class CRX_SHARE timer : public kobj
    {
    public:
        /*
         * 启动定时器 (终止定时器只需要将timer类销毁即可)
         * @delay: 初始延迟
         * @interval: 间隔
         * 单位均为毫秒
         */
        void start(uint64_t delay, uint64_t interval);

        //重置定时器回到初始启动状态
        void reset();

        //分离定时器
        void detach();
    };

    class CRX_SHARE timer_wheel : public kobj
    {
    public:
        /*
         * 增加定时轮处理器，定时轮将根据延迟以及一个tick的间隔时间选择适当的槽，将回调函数放在这个槽内，等到
         * 指针指向这个槽之后，执行相应的回调函数
         *
         * @delay 延迟时间，单位为毫秒
         * @callback 回调函数
         * @return 若延迟时间>interval*slot，那么处理器添加失败
         */
        bool add_handler(uint64_t delay, std::function<void()> callback);
    };

    class CRX_SHARE event : public kobj
    {
    public:
        void send_signal(int signal);       //发送事件信号

        void detach();      //分离事件
    };

    class CRX_SHARE udp_ins : public kobj
    {
    public:
        //获取使用的端口
        uint16_t get_port();

        //发送udp数据包(包的大小上限为65536个字节)
        void send_data(const char *ip_addr, uint16_t port, const char *data, size_t len);

        void detach();      //分离udp实例
    };

    //tcp_client实例支持同时连接多个tcp server
    class CRX_SHARE tcp_client : public kobj
    {
    public:
        /*
         * 发起tcp连接请求，该接口是线程安全的
         * @server: 服务器主机地址，同时支持点分十进制格式的ip以及域名形式的主机地址
         * @port: 服务器的端口
         * @retry: 是否尝试重连, -1:不断重连 0:不重连 n(>0):重连n次
         * @timeout: 若尝试重连，timeout指明重连间隔，单位为秒且timeout<=60，因为使用的定时轮为秒盘
         * @return value(conn): 唯一标识与指定主机的连接，-1表示连接失败
         */
        int connect(const char *server, uint16_t port, int retry = 0, int timeout = 0);

        //关闭tcp连接并释放已申请的资源
        void release(int conn);

        //发送tcp数据流
        void send_data(int conn, const char *data, size_t len);
    };

    class CRX_SHARE tcp_server : public kobj
    {
    public:
        //获取监听的端口
        uint16_t get_port();

        //服务端主动断开与客户端建立的连接
        void release(int conn);

        //发送tcp响应流，@conn表示指定的连接，@data表示响应数据
        void send_data(int conn, const char *data, size_t len);
    };

    enum EXT_DST
    {
        DST_NONE = 0,
        DST_JSON,			//json格式的body数据
        DST_QSTRING,		//query string格式
    };

    class CRX_SHARE http_client : public tcp_client
    {
    public:
        /*
         * 向指定http server 发送指定的method请求，一次请求的示例如下：
         * method(GET/POST...) / HTTP/1.1 (常用的就是GET以及POST方法)
         * Host: localhost
         * 请求中body部分的数据类型由Content-Type字段指明(application/json#json格式，application/x-www-form-urlencoded#query_string格式)
         * 其中GET方法没有body部分，POST方法带有body
         *
         * @conn: 指定的http连接
         * @method: "GET"/"POST"...
         * @post_page: 请求的资源定位符，只需要从 / 开始的部分
         * @extra_headers: 额外的请求头部
         * @ext_data/ext_len: 附带的数据，数据类型由Content-Type字段指明
         */
        void request(int conn, const char *method, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                     const char *ext_data, size_t ext_len, EXT_DST ed = DST_NONE);

        //发送一次GET请求
        void GET(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers);

        //发送一次POST请求
        void POST(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                  const char *ext_data, size_t ext_len, EXT_DST ed = DST_JSON);
    };

    class CRX_SHARE http_server : public tcp_server
    {
    public:
        void response(int conn, const char *ext_data, size_t ext_len, EXT_DST ed = DST_JSON);
    };

    class CRX_SHARE fs_monitor : public kobj
    {
    public:
        /*
         * 监控指定的文件系统对象，path可以是目录或是文件，也可以是已挂载的移动介质中的对象(可多次调用同时监控多个目标)
         * @param path 文件系统对象
         * @param mask 监控事件，默认的监控事件为创建/删除/修改
         * @param recursive 若path为目录，则该参数指明是否对该目录下的所有子目录进行递归监控
         */
        void add_watch(const char *path, uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY, bool recursive = true);

        /*
         * 移除正在监控的对象
         * @param path 文件系统对象
         * @param recursive 若path为目录，该参数指明是否递归移除子目录的监控
         */
        void rm_watch(const char *path, bool recursive = true);
    };

    class CRX_SHARE log : public kobj
    {
    public:
        void printf(const char *fmt, ...);

        //分离日志
        void detach();
    };

#define log_error(log_ins, fmt, args...)    log_ins.printf("[%s|%s|%d] [ERROR] " fmt, __FILENAME__, __func__, __LINE__, ##args)

#define log_warn(log_ins, fmt, args...)     log_ins.printf("[%s|%s|%d] [WARN] "  fmt, __FILENAME__, __func__, __LINE__, ##args)

#define log_info(log_ins, fmt, args...)     log_ins.printf("[%s|%s|%d] [INFO] "  fmt, __FILENAME__, __func__, __LINE__, ##args)

#define log_debug(log_ins, fmt, args...)    log_ins.printf("[%s|%s|%d] [DEBUG] " fmt, __FILENAME__, __func__, __LINE__, ##args)
}
