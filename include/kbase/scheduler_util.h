#pragma once

namespace crx
{
    class scheduler;
    class CRX_SHARE sigctl : public kobj
    {
    public:
        void add_signo(int signo);

        void remove_signo(int signo);

        void clear_signo(int signo);

    protected:
        sigctl() = default;
        friend scheduler;
    };

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
        timer() = default;
        friend scheduler;
    };

    class CRX_SHARE event : public kobj
    {
    public:
        void send_signal(int signal);       //发送事件信号

        void release();     //释放事件对象

    protected:
        event() = default;
        friend scheduler;
    };

    class CRX_SHARE udp_ins : public kobj
    {
    public:
        //获取使用的端口
        uint16_t get_port();

        //发送udp数据包(包的大小上限为65536个字节)
        void send_data(const char *ip_addr, uint16_t port, const char *data, size_t len);

        //释放udp_ins对象
        void release();

    protected:
        udp_ins() = default;
        friend scheduler;
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

        //发送tcp数据流
        void send_data(int conn, const char *data, size_t len);

    protected:
        tcp_client() = default;
        friend scheduler;
    };

    class CRX_SHARE tcp_server : public kobj
    {
    public:
        //获取监听的端口
        uint16_t get_port();

        //发送tcp响应流，@conn表示指定的连接，@data表示响应数据
        void response(int conn, const char *data, size_t len);

    protected:
        tcp_server() = default;
        friend scheduler;
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
                     const char *ext_data, size_t ext_len, EXT_DST ed = DST_NONE);

        //发送一次GET请求
        void GET(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers);

        //发送一次POST请求
        void POST(int conn, const char *post_page, std::unordered_map<std::string, std::string> *extra_headers,
                  const char *ext_data, size_t ext_len, EXT_DST ed = DST_JSON);

    protected:
        http_client() = default;
        friend scheduler;
    };

    class CRX_SHARE http_server : public tcp_server
    {
    public:
        void response(int conn, const char *ext_data, size_t ext_len, EXT_DST ed = DST_JSON);

    protected:
        http_server() = default;
        friend scheduler;
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
        void add_watch(const char *path, uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY, bool recursive = true);

        /**
         * 移除正在监控的对象
         * @param path 文件系统对象
         * @param recursive 若path为目录，该参数指明是否递归移除子目录的监控
         */
        void rm_watch(const char *path, bool recursive = true);

    protected:
        fs_monitor() = default;
        friend scheduler;
    };
}
