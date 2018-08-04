#pragma once

#include "crx_pch.h"

namespace crx
{
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
        void request(int conn, const char *method, const char *post_page, std::map<std::string, std::string> *extra_headers,
                     const char *ext_data, size_t ext_len, EXT_DST ed = DST_NONE);

        //发送一次GET请求
        void GET(int conn, const char *post_page, std::map<std::string, std::string> *extra_headers);

        //发送一次POST请求
        void POST(int conn, const char *post_page, std::map<std::string, std::string> *extra_headers,
                  const char *ext_data, size_t ext_len, EXT_DST ed = DST_JSON);
    };

    class CRX_SHARE http_server : public tcp_server
    {
    public:
        void response(int conn, const char *ext_data, size_t ext_len, EXT_DST ed = DST_JSON);
    };
}