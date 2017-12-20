#pragma once

namespace crx
{
    class epoll_thread;
    class CRX_SHARE timer : public kobj
    {
    public:
        /**
         * 启动定时器 (终止定时器只需要将timer类销毁即可)
         * @delay: 初始延迟
         * @interval: 间隔
         * 单位均为毫秒
         */
        void start(int64_t delay, int64_t interval);

        //重置定时器回到初始启动状态
        void reset();

        //释放定时器
        void release();

    protected:
        timer();
        friend epoll_thread;
    };

    class CRX_SHARE event : public kobj
    {
    public:
        //发送事件信号
        void send_signal(const char *signal, int len);

        void send_signal(std::string& signal);

        //释放事件对象
        void release();

    protected:
        event();
        friend epoll_thread;
    };

    class CRX_SHARE udp_ins : public kobj
    {
    public:
        //获取使用的端口
        uint16_t get_port();

        //发送udp数据包(包的大小上限为65536个字节)
        void send_data(const char *ip_addr, uint16_t port, const char *data, int len);

        //释放udp_ins对象
        void release();

    protected:
        udp_ins();
        virtual ~udp_ins() {}
        friend epoll_thread;
    };

    //tcp_client实例支持同时连接多个tcp server
    class CRX_SHARE tcp_client : public kobj
    {
    public:
        /**
         * 发起tcp连接请求，该接口是线程安全的
         * @server: 服务器主机地址，同时支持点分十进制格式的ip以及域名形式的主机地址
         * @port: 服务器的端口
         * @return value(conn): 唯一标识与指定主机的连接，-1表示连接失败
         */
        int connect(const char *server, uint16_t port);

        //关闭tcp连接并释放已申请的资源
        void release(int conn);

        //发送tcp数据流(流的大小没有限制，但在应用层应设置一个每次发送的上限比如8192)
        void send_data(int conn, const char *data, int len);

        void send_data(int conn, std::string& data);

    protected:
        tcp_client(bool expose);
        virtual ~tcp_client();
        friend epoll_thread;
    };

    class CRX_SHARE tcp_server : public kobj
    {
    public:
        //获取监听的端口
        uint16_t get_port();

        //发送tcp响应流(流的大小没有限制，要求如上)，函数的两个参数依次为指定的连接及响应数据
        void response(int conn, const char *data, int len);

        void response(int conn, std::string& data);

    protected:
        tcp_server(bool expose);
        virtual ~tcp_server() {}
        friend epoll_thread;
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
        /**
         * 向指定http server 发送指定的method请求，一次请求的示例如下：
         * method(GET/POST...) / HTTP/1.1 (常用的就是GET以及POST方法)
         * Host: localhost
         * User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/46.0.2490.71 Safari/537.36
         * 请求中body部分的数据类型由Content-Type字段指明(application/json#json格式，application/x-www-form-urlencoded#query_string格式)
         * 其中GET方法没有body部分，POST方法带有body
         *
         * @conn: 指定的http连接
         * @method: "GET"/"POST"...
         * @url: 请求的资源定位符，只需要从 / 开始的部分
         * @extra_headers: 额外的请求头部
         * @body: 附带的数据，数据类型由Content-Type字段指明
         * @on_response: http请求的回调响应
         * @args: 响应的参数
         */
        void request(int conn, const char *method, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                     const char *ext_data, int ext_len, EXT_DST ed = DST_NONE);

        //发送一次GET请求
        void GET(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers);

        //发送一次POST请求
        void POST(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                  const char *ext_data, int ext_len, EXT_DST ed = DST_JSON);

    protected:
        http_client();
        virtual ~http_client();
        friend epoll_thread;
    };

    class CRX_SHARE http_server : public tcp_server
    {
    public:
        void response(int conn, const char *ext_data, int ext_len, EXT_DST ed = DST_JSON);

    protected:
        http_server();
        virtual ~http_server() {}
        friend epoll_thread;
    };

    class CRX_SHARE fs_monitor : public kobj
    {
    public:
        /**
         * 监控指定的文件系统对象，path可以是目录或是文件，也可以是已挂载的移动介质中的对象(可多次调用同时监控多个目标)
         * @param path 文件系统对象
         * @param mask 监控事件，默认的监控事件为创建/删除/修改
         * @param recursive 若path为目录，则该参数指明是否对该目录下的所有子目录进行递归监控
         */
        void add_watch(const char *path, int mask = IN_CREATE | IN_DELETE | IN_MODIFY, bool recursive = true);

        /**
         * 移除正在监控的对象
         * @param path 文件系统对象
         * @param recursive 若path为目录，该参数指明是否递归移除子目录的监控
         */
        void rm_watch(const char *path, bool recursive = true);

    protected:
        fs_monitor();
        virtual ~fs_monitor() {}
        friend epoll_thread;
    };
}
