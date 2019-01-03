#pragma once

namespace crx
{
    //tcp_client实例支持同时连接多个tcp server
    class CRX_SHARE tcp_client : public kobj
    {
    public:
        /*
         * 发起tcp连接请求，该接口是线程安全的
         * @server: 服务器主机地址，同时支持点分十进制格式的ip以及域名形式的主机地址
         * @port: 服务器的端口
         * @return value(conn): 唯一标识与指定主机的连接，< 0 表示连接失败
         */
        int connect(const char *server, uint16_t port);

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

        //发送tcp响应流，@conn表示指定的连接，@data表示响应数据
        void send_data(int conn, const char *data, size_t len);
    };
}
