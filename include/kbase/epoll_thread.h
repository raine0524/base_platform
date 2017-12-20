#pragma once

namespace crx
{
    //NOTE：手动释放的资源可同时申请多个，自动释放的资源多次申请将返回同一个实例
    class CRX_SHARE epoll_thread : public kobj
    {
    public:
        epoll_thread();
        virtual ~epoll_thread();

        //启动epoll线程
        void start();

        //终止epoll线程
        void stop();

        //获取timer实例(需手动释放)
        timer* get_timer(std::function<void(void*)> f, void *args = nullptr);

        //获取event实例(需手动释放)
        event* get_event(std::function<void(std::string&, void*)> f, void *args = nullptr);

        /**
         * 获取udp实例(需手动释放)
         * @is_server：为true表明创建的是服务端使用的udp套接字，反之则为客户端使用的套接字
         * @port：udp是无连接的传输层协议，因此在创建套接字时不需要显示指定ip地址，但udp服务器端在接收请求时
         * 				需要显示指定监听的端口，若port为0，则系统将为该套接字选择一个随机的端口
         * 	@f：回调函数，函数的5个参数分别为对端的ip地址、端口、接收到的udp包和大小，以及回调参数
         * 	@args：回调参数
         */
        udp_ins* get_udp_ins(bool is_server, uint16_t port,
                             std::function<void(const std::string&, uint16_t, const char*, size_t, void*)> f, void *args = nullptr);

        //获取tcp客户端实例(自动释放)，回调函数的3个参数分别为指定的连接，收到的tcp数据流以及回调参数
        tcp_client* get_tcp_client(std::function<void(int, std::string&, void*)> f, void *args = nullptr);

        /**
         * 获取tcp服务端实例(自动释放)
         * @port：指示tcp服务将在哪个端口上进行监听，若port为0，则系统将随机选择一个可用端口
         * @f：回调函数，函数的3个参数分别为指定的连接，连接的ip地址/端口，收到的tcp数据流以及回调参数
         * @args：回调参数
         */
        tcp_server* get_tcp_server(uint16_t port, std::function<void(int, const std::string&, uint16_t, std::string&, void*)> f, void *args = nullptr);

        /**
         * 获取http客户端实例(自动释放)，回调函数中的5个参数依次为
         * ①指定的连接
         * ②响应标志(200, 404等等)
         * ③头部键值对
         * ④响应体
         * ⑤回调参数
         */
        http_client* get_http_client(std::function<void(int, int, std::unordered_map<std::string, std::string>&, std::string&, void*)> f, void *args = nullptr);

        /**
         * 获取http服务端实例(自动释放)，回调函数中的6个参数依次为
         * ①指定的连接
         * ②请求方法(例如"GET", "POST"等等)
         * ③url(以"/"起始的字符串，例如"/index.html")
         * ④头部键值对
         * ⑤请求体(可能不存在)
         * ⑥回调参数
         */
        http_server* get_http_server(uint16_t port, std::function<void(int, const std::string&, const std::string&,
                                                                       std::unordered_map<std::string, std::string>&, std::string*, void*)> f, void *args = nullptr);

        /**
         * 获取文件系统监控实例(自动释放)，回调函数中的6个参数依次为
         * ①触发监控事件的文件
         * ②监控文件的掩码，用于确定触发事件的类型
         * ③回调参数
         */
        fs_monitor* get_fs_monitor(std::function<void(const char*, uint32_t, void *args)> f, void *args = nullptr);
    };
}