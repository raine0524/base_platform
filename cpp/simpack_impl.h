#pragma once

#include "stdafx.h"

#define GET_BIT(field, n)   (field & 1<<n)

#define SET_BIT(field, n)   (field |= 1<<n)

#define CLR_BIT(field, n)   (field &= ~(1<<n))

namespace crx
{
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
        uint16_t type;              //表明数据的类型
        uint16_t cmd;               //若是请求类型的数据,指明哪一个请求
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

    struct simpack_xutil : public impl
    {
        server_info info;
        unsigned char token[16];

        //for registry
        int listen;
        std::set<std::string> clients;
    };

    class simpack_server_impl : public impl
    {
    public:
        simpack_server_impl()
        :m_reg_conn(-1)
        ,m_log_conn(-1)
        ,m_seria(true)
        {
            simp_header stub_header;
            m_simp_buf = std::string((const char*)&stub_header, sizeof(simp_header));
        }

        void stop();

        void simp_callback(int conn, const std::string& ip, uint16_t port, char *data, size_t len);

        void capture_sharding(bool registry, int conn, std::shared_ptr<simpack_xutil>& xutil,
                const std::string &ip, uint16_t port, char *data, size_t len);

        void handle_reg_name(int conn, unsigned char *token, std::map<std::string, mem_ref>& kvs,
                std::shared_ptr<simpack_xutil>& xutil);

        void handle_svr_online(unsigned char *token, std::map<std::string, mem_ref>& kvs);

        void handle_hello_request(int conn, const std::string &ip, uint16_t port, unsigned char *token,
                                  std::map<std::string, mem_ref>& kvs);

        void handle_hello_response(int conn, unsigned char *token, std::map<std::string, mem_ref>& kvs);

        void say_goodbye(std::shared_ptr<simpack_xutil>& xutil);

        void handle_goodbye(int conn);

        void send_data(int type, int conn, const server_cmd& cmd, const char *data, size_t len);

        void send_package(int type, int conn, const server_cmd& cmd, bool lib_proc,
                          unsigned char *token, const char *data, size_t len);

        scheduler *m_sch;
        int m_reg_conn;
        std::string m_reg_str;
        std::set<int> m_ordinary_conn;

        int m_log_conn;
        std::string m_log_req, m_log_cache;

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
}