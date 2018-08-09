#pragma once

#include "stdafx.h"

namespace crx
{
    class http_xutil
    {
    public:
        int status, content_len;
        const char *method, *url;		//请求方法/url(以"/"开始的字符串)
        std::map<std::string, const char*> headers;

        http_xutil() : content_len(-1) {}
    };

    class http_funcs
    {
    public:
        std::function<void(int, int, std::map<std::string, const char*>&, char*, size_t)> m_http_cli;
        std::function<void(int, const char*, const char*, std::map<std::string, const char*>&, char*, size_t)> m_http_svr;
    };

    template <typename CONN_TYPE>
    class http_conn_t : public CONN_TYPE
    {
    public:
        http_conn_t(SOCK_TYPE type) : CONN_TYPE(type) {}

        http_xutil xutil;
    };

    template <typename IMPL_TYPE>
    class http_impl_t : public IMPL_TYPE
    {
    public:
        http_impl_t(SOCK_TYPE type) : IMPL_TYPE(type) {}

        http_funcs funcs;
    };

    template <typename IMPL_TYPE, typename CONN_TYPE>
    void tcp_callback_for_http(bool client, IMPL_TYPE impl, int fd, char *data, size_t len)
    {
        if (!data || !len) return;      //tcp连接开启/关闭时同样将调用回调函数
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(impl->m_util.m_sch->m_impl);
        if (fd < 0 || fd >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[fd])
            return;

        size_t cb_len = 0;
        char *cb_data = nullptr;
        auto conn = std::dynamic_pointer_cast<CONN_TYPE>(sch_impl->m_ev_array[fd]);
        if (len != 1 || '\n' != *data) {
            cb_data = data;
            cb_len = (size_t) conn->xutil.content_len;
        }

        if (client)
            impl->funcs.m_http_cli(fd, conn->xutil.status, conn->xutil.headers,
                    cb_data, cb_len);
        else
            impl->funcs.m_http_svr(fd, conn->xutil.method, conn->xutil.url,
                    conn->xutil.headers, cb_data, cb_len);
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

        if (len < conn->xutil.content_len)      //请求/响应体中不包含足够的数据，等待该连接上更多的数据到来
            return 0;
        else        //已取到请求/响应体，通知上层执行m_f回调
            return conn->xutil.content_len;
    }
}