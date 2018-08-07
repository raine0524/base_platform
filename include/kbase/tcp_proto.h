#pragma once

#include "crx_pch.h"

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
}