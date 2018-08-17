#include "stdafx.h"

namespace crx
{
    simp_buffer::simp_buffer()
    {
        simp_header stub_header;
        Push(sizeof(simp_header));
        memcpy(stack_.Bottom<char>(), &stub_header, sizeof(stub_header));
    }

    void simp_buffer::append_zero()
    {
        *Push(1) = 0;
    }

    void simp_buffer::reset()
    {
        size_t size = GetSize();
        assert(size >= sizeof(simp_header));
        if (size > sizeof(simp_header))
            Pop(size-sizeof(simp_header));
    }

    simpack_server
    scheduler::get_simpack_server(
            std::function<void(const crx::server_info&)> on_connect,
            std::function<void(const crx::server_info&)> on_disconnect,
            std::function<void(const crx::server_info&, const crx::server_cmd&, char*, size_t)> on_request,
            std::function<void(const crx::server_info&, const crx::server_cmd&, char*, size_t)> on_response,
            std::function<void(const crx::server_info&, const crx::server_cmd&, char*, size_t)> on_notify)
    {
        simpack_server obj;
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[SIMP_SVR];
        if (impl) {
            obj.m_impl = impl;
            return obj;
        }

        if (sch_impl->m_ini_file.empty())   //不存在配置文件
            return obj;

        ini ini_parser;
        ini_parser.load(sch_impl->m_ini_file.c_str());

        if (!ini_parser.has_section("registry"))    //配置文件不存在 [registry] 节区
            return obj;

        auto simp_impl = std::make_shared<simpack_server_impl>();
        simp_impl->m_sch = this;
        simp_impl->m_on_connect = std::move(on_connect);
        simp_impl->m_on_disconnect = std::move(on_disconnect);
        simp_impl->m_on_request = std::move(on_request);
        simp_impl->m_on_response = std::move(on_response);
        simp_impl->m_on_notify = std::move(on_notify);

        //使用simpack_server组件的应用必须配置registry节区以及ip,port,name这三个字段
        auto xutil = std::make_shared<simpack_xutil>();
        ini_parser.set_section("registry");
        xutil->info.ip = ini_parser.get_str("ip");
        xutil->info.port = (uint16_t)ini_parser.get_int("port");
        xutil->info.name = ini_parser.get_str("name");
        xutil->listen = ini_parser.get_int("listen");

        //创建tcp_server用于被动连接
        simp_impl->m_server = get_tcp_server((uint16_t)xutil->listen,
                std::bind(&simpack_server_impl::simp_callback, simp_impl.get(), _1, _2, _3, _4, _5));
        if (!simp_impl->m_server.m_impl) return obj;
        auto svr_impl = std::dynamic_pointer_cast<tcp_server_impl>(simp_impl->m_server.m_impl);
        svr_impl->m_util.m_app_prt = PRT_SIMP;
        register_tcp_hook(false, [](int conn, char *data, size_t len) { return simpack_protocol(data, len); });
        xutil->listen = simp_impl->m_server.get_port();
        sch_impl->m_util_impls[TCP_SVR].reset();

        //创建tcp_client用于主动连接
        simp_impl->m_client = get_tcp_client(std::bind(&simpack_server_impl::simp_callback,
                                                       simp_impl.get(), _1, _2, _3, _4, _5));
        auto cli_impl = std::dynamic_pointer_cast<tcp_client_impl>(simp_impl->m_client.m_impl);
        cli_impl->m_util.m_app_prt = PRT_SIMP;
        register_tcp_hook(true, [](int conn, char *data, size_t len) { return simpack_protocol(data, len); });
        sch_impl->m_util_impls[TCP_CLI].reset();

        //只有配置了名字才连接registry，否则作为单点应用
        if (!xutil->info.name.empty()) {
            //若连接失败则每隔三秒尝试重连一次
            int conn = simp_impl->m_client.connect(xutil->info.ip.c_str(), xutil->info.port, -1, 3);
            if (conn < 0) {     //连接可能发生失败(原因可能是套接字创建失败,域名解析失败等)
                simp_impl->m_client.m_impl.reset();
                simp_impl->m_server.m_impl.reset();
                return obj;
            }

            simp_impl->m_reg_conn = xutil->info.conn = conn;
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            tcp_ev->ext_data = xutil;

            simp_impl->m_write_doc.SetObject();
            Document::AllocatorType& alloc = simp_impl->m_write_doc.GetAllocator();
            simp_impl->m_write_doc.AddMember("port", xutil->listen, alloc);
            simp_impl->m_write_doc.AddMember("name", Value().SetString(xutil->info.name.c_str(),
                    (unsigned)xutil->info.name.size()), alloc);
            simp_impl->m_write_doc.Accept(simp_impl->m_writer);

            simp_impl->m_write_buf.append_zero();
            const char *data = simp_impl->m_write_buf.GetString();
            size_t len = simp_impl->m_write_buf.GetSize();

            //setup header
            auto header = (simp_header*)data;
            header->length = htonl((uint32_t)(len-sizeof(simp_header)));
            header->cmd = htons(CMD_REG_NAME);

            simp_impl->m_reg_str = std::string(data, len);
            simp_impl->m_client.send_data(conn, data, len);
            simp_impl->m_write_buf.reset();
            simp_impl->m_writer.Reset(simp_impl->m_write_buf);
        }

        obj.m_impl = impl = simp_impl;
        return obj;
    }

    void simpack_server::request(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        if (!m_impl) return;
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(1, conn, cmd, data, len);
    }

    void simpack_server::response(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        if (!m_impl) return;
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(2, conn, cmd, data, len);
    }

    void simpack_server::notify(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        if (!m_impl) return;
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(3, conn, cmd, data, len);
    }

    void simpack_server_impl::send_data(int type, int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        send_package(type, conn, cmd, false, xutil->token, data, len);
    }

    void simpack_server_impl::send_package(int type, int conn, const server_cmd& cmd, bool lib_proc,
                                           unsigned char *token, const char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        simp_header *header = nullptr;
        size_t total_len = len;
        if (len >= 4 && htonl(0x5f3759df) == *(uint32_t*)data) {    //上层使用的是基于simp协议的序列化组件simp_buffer
            header = (simp_header*)data;
            header->length = htonl((uint32_t)(len-sizeof(simp_header)));
        } else {
            m_write_buf.reset();
            char *body = m_write_buf.stack_.End<char>();
            m_write_buf.Push(len);
            memcpy(body, data, len);
            m_write_buf.append_zero();

            header = (simp_header*)m_write_buf.GetString();
            header->length = htonl((uint32_t)len+1);        //包含最后一个0字节
            total_len = len+sizeof(simp_header)+1;
        }

        header->type = htons(cmd.type);
        header->cmd = htons(cmd.cmd);
        uint32_t ctl_flag = 0;
        if (lib_proc)
            SET_BIT(ctl_flag, 0);       //由库这一层处理

        if (1 == type) {            //request
            CLR_BIT(ctl_flag, 1);
            SET_BIT(ctl_flag, 2);
        } else if (2 == type) {     //response
            CLR_BIT(ctl_flag, 1);
            CLR_BIT(ctl_flag, 2);
        } else if (3 == type) {     //notify
            SET_BIT(ctl_flag, 1);
        } else {
            return;
        }
        header->ctl_flag = htonl(ctl_flag);

        if (token)      //带上token表示是合法的package
            memcpy(header->token, token, 16);

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        tcp_ev->async_write((const char*)header, total_len);
    }

    void simpack_server_impl::stop()
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        for (auto conn : m_ordinary_conn) {
            if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
                continue;

            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            if ("__log_server__" != xutil->info.role)
                m_on_disconnect(xutil->info);
            say_goodbye(xutil);
        }

        if (0 <= m_reg_conn && m_reg_conn < sch_impl->m_ev_array.size() && sch_impl->m_ev_array[m_reg_conn]) {
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_reg_conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            say_goodbye(xutil);
            m_reg_conn = -1;
        }
    }

    void simpack_server_impl::simp_callback(int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        if (conn < 0 || conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[conn])
            return;

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);

        if (!data) {            //连接开启/关闭回调
            if (len == m_reg_conn) {      //此时为重连
                log_info(g_lib_log, "connection rebuild %d --> %d\n", m_reg_conn, conn);
                xutil->info.conn = m_reg_conn = conn;
                m_client.send_data(m_reg_conn, m_reg_str.c_str(), m_reg_str.size());
            }
            return;
        }

        auto header = (simp_header*)data;
        m_app_cmd.type = ntohs(header->type);
        m_app_cmd.cmd = ntohs(header->cmd);
        uint32_t ctl_flag = ntohl(header->ctl_flag);
        bool is_registry = GET_BIT(ctl_flag, 3) != 0;
        if (GET_BIT(ctl_flag, 0)) {     //捕获控制请求
            capture_sharding(is_registry, conn, xutil, ip, port, data, len);
            return;
        }

        //路由上层请求
        if (memcmp(header->token, xutil->token, 16)) {
            log_error(g_lib_log, "illegal request with invalid token(%d)\n", conn);
            return;
        }

        auto header_len = sizeof(simp_header);
        if (GET_BIT(ctl_flag, 1)) {     //推送
            m_on_notify(xutil->info, m_app_cmd, data+header_len, len-header_len);
        } else {        //非推送
            if (GET_BIT(ctl_flag, 2))
                m_on_request(xutil->info, m_app_cmd, data+header_len, len-header_len);
            else
                m_on_response(xutil->info, m_app_cmd, data+header_len, len-header_len);
        }
    }

    void simpack_server_impl::capture_sharding(bool registry, int conn,  std::shared_ptr<simpack_xutil>& xutil,
            const std::string &ip, uint16_t port, char *data, size_t len)
    {
        auto header = (simp_header*)data;
        m_read_doc.ParseInsitu(data+sizeof(simp_header));
        if (registry) {       //与registry建立的连接
            switch (m_app_cmd.cmd) {
                case CMD_REG_NAME:      handle_reg_name(conn, header->token, xutil);   break;
                case CMD_SVR_ONLINE:    handle_svr_online(header->token);              break;
                default: {
                    log_error(g_lib_log, "invalid cmd = %d\n", m_app_cmd.cmd);
                    break;
                }
            }
            return;
        }

        //[其他连接]除了首个建立信道的命令之外，后续所有的请求/响应都需要验证token
        if (CMD_HELLO != m_app_cmd.cmd && memcmp(header->token, xutil->token, 16)) {
            log_error(g_lib_log, "illegal request with invalid token(%d)\n", conn);
            return;
        }

        switch (m_app_cmd.cmd) {
            case CMD_HELLO: {
                if (1 == m_app_cmd.type)
                    handle_hello_request(conn, ip, port, header->token);
                else if (2 == m_app_cmd.type)
                    handle_hello_response(conn, header->token);
                break;
            }
            case CMD_GOODBYE:       handle_goodbye(conn);                               break;
            default: {
                log_error(g_lib_log, "invalid cmd = %d\n", m_app_cmd.cmd);
                break;
            }
        }
    }

    void simpack_server_impl::handle_reg_name(int conn, unsigned char *token, std::shared_ptr<simpack_xutil>& xutil)
    {
        int result = m_read_doc["result"].GetInt();
        if (result) {    //失败
            log_error(g_lib_log, "pronounce failed: %s\n", m_read_doc["error_info"].GetString());
            m_client.release(conn);
            return;
        }

        //成功
        if (m_reg_conn != conn) {
            if (-1 != m_reg_conn) {
                log_warn(g_lib_log, "repeat connection with registry old(%d)-->new(%d)\n", m_reg_conn, conn);
                say_goodbye(xutil);
            }
            m_reg_conn = conn;
        }

        const char *role = m_read_doc["role"].GetString();      //r当前节点的role
        log_info(g_lib_log, "pronounce succ: role=%s\n", role);
        xutil->info.role = role;

        for (auto& cli : m_read_doc["clients"].GetArray())
            xutil->clients.emplace(cli.GetString());
        memcpy(xutil->token, token, 16);    //保存该token用于与其他服务之间的通信

        //让registry通知所有连接该服务的主动方发起连接或是让本服务连接所有的被动方
        m_write_doc.SetObject();
        Document::AllocatorType& alloc = m_write_doc.GetAllocator();
        m_write_doc.AddMember("name", Value().SetString(xutil->info.name.c_str(),
                (unsigned)xutil->info.name.size()), alloc);
        m_write_doc.Accept(m_writer);

        m_write_buf.append_zero();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_SVR_ONLINE;
        send_package(3, m_reg_conn, m_app_cmd, true, xutil->token, m_write_buf.GetString(), m_write_buf.GetSize());
        m_write_buf.reset();
        m_writer.Reset(m_write_buf);
    }

    void simpack_server_impl::handle_svr_online(unsigned char *token)
    {
        const char *ip = m_read_doc["ip"].GetString();
        uint16_t port = (uint16_t)m_read_doc["port"].GetInt();

        //握手
        int conn = m_client.connect(ip, port);
        if (conn < 0) {
            log_error(g_lib_log, "connect to other service failed: %d\n", conn);
            return;
        }

        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::make_shared<simpack_xutil>();
        tcp_ev->ext_data = xutil;
        xutil->info.conn = conn;
        xutil->info.ip = ip;
        xutil->info.port = port;

        if (m_reg_conn < 0 || m_reg_conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[m_reg_conn])
            return;

        auto reg_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_reg_conn]);
        auto reg_xutil = std::dynamic_pointer_cast<simpack_xutil>(reg_ev->ext_data);

        m_write_doc.SetObject();
        Document::AllocatorType& alloc = m_write_doc.GetAllocator();
        m_write_doc.AddMember("name", Value().SetString(reg_xutil->info.name.c_str(),
                (unsigned)reg_xutil->info.name.size()), alloc);
        m_write_doc.AddMember("role", Value().SetString(reg_xutil->info.role.c_str(),
                (unsigned)reg_xutil->info.role.size()), alloc);
        m_write_doc.Accept(m_writer);

        m_write_buf.append_zero();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_HELLO;
        m_app_cmd.type = 1;
        send_package(1, conn, m_app_cmd, true, token, m_write_buf.GetString(), m_write_buf.GetSize());
        m_write_buf.reset();
        m_writer.Reset(m_write_buf);
    }

    void simpack_server_impl::handle_hello_request(int conn, const std::string &ip, uint16_t port, unsigned char *token)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        if (m_reg_conn < 0 || m_reg_conn >= sch_impl->m_ev_array.size() || !sch_impl->m_ev_array[m_reg_conn])
            return;

        auto reg_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_reg_conn]);
        auto reg_xutil = std::dynamic_pointer_cast<simpack_xutil>(reg_ev->ext_data);
        if (memcmp(token, reg_xutil->token, 16)) {
            log_error(g_lib_log, "illegal hello request with invalid token(%d)\n", conn);
            return;
        }

        const char *cli_name = m_read_doc["name"].GetString();
        const char *cli_role = m_read_doc["role"].GetString();

        bool client_valid = reg_xutil->clients.end() != reg_xutil->clients.find(cli_name);
        log_info(g_lib_log, "client %s[%s], conn=%d, ip=%s, port=%u say hello to me, valid=%d\n",
                cli_name, cli_role, conn, ip.c_str(), port, client_valid);

        unsigned char *new_token = nullptr;
        std::shared_ptr<simpack_xutil> ord_xutil;
        m_write_doc.SetObject();
        Document::AllocatorType& alloc = m_write_doc.GetAllocator();

        if (client_valid) {
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            tcp_ev->ext_data = ord_xutil = std::make_shared<simpack_xutil>();
            m_ordinary_conn.insert(conn);

            ord_xutil->info.conn = conn;
            ord_xutil->info.ip = std::move(ip);
            ord_xutil->info.port = port;
            ord_xutil->info.name = std::move(cli_name);
            ord_xutil->info.role = std::move(cli_role);

            datetime dt = get_datetime();
            std::string text = ord_xutil->info.name+"#"+reg_xutil->info.name+"-";
            text += std::to_string(dt.date)+std::to_string(dt.time);
            MD5((const unsigned char*)text.c_str(), text.size(), ord_xutil->token);
            new_token = ord_xutil->token;

            m_on_connect(ord_xutil->info);
            m_write_doc.AddMember("name", Value().SetString(reg_xutil->info.name.c_str(),
                    (unsigned)reg_xutil->info.name.size()), alloc);
            m_write_doc.AddMember("role", Value().SetString(reg_xutil->info.role.c_str(),
                    (unsigned)reg_xutil->info.role.size()), alloc);
        } else {
            std::string error_info = std::string("unknown name ")+cli_name;
            m_write_doc.AddMember("error_info", Value().SetString(error_info.c_str(),
                    (unsigned)error_info.size()), alloc);
        }

        m_write_doc.AddMember("result", client_valid ? 0 : 1, alloc);
        m_write_doc.Accept(m_writer);
        m_write_buf.append_zero();

        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_HELLO;
        m_app_cmd.type = 2;     //响应
        send_package(2, conn, m_app_cmd, true, new_token, m_write_buf.GetString(), m_write_buf.GetSize());
        m_write_buf.reset();
        m_writer.Reset(m_write_buf);

        if (client_valid) {     //通知registry连接建立
            m_write_doc.SetObject();
            m_write_doc.AddMember("client", Value().SetString(ord_xutil->info.name.c_str(),
                    (unsigned)ord_xutil->info.name.size()), alloc);
            m_write_doc.AddMember("server", Value().SetString(reg_xutil->info.name.c_str(),
                    (unsigned)reg_xutil->info.name.size()), alloc);
            m_write_doc.Accept(m_writer);
            m_write_buf.append_zero();

            bzero(&m_app_cmd, sizeof(m_app_cmd));
            m_app_cmd.cmd = CMD_CONN_CON;
            send_package(1, reg_xutil->info.conn, m_app_cmd, true, reg_xutil->token,
                    m_write_buf.GetString(), m_write_buf.GetSize());
            m_write_buf.reset();
            m_writer.Reset(m_write_buf);
        }
    }

    void simpack_server_impl::handle_hello_response(int conn, unsigned char *token)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        int result = m_read_doc["result"].GetInt();
        if (result) {
            log_error(g_lib_log, "say hello with error response: %s\n", m_read_doc["error_info"].GetString());
            sch_impl->remove_event(conn);
            return;
        }

        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        m_ordinary_conn.insert(conn);
        xutil->info.name = m_read_doc["name"].GetString();
        xutil->info.role = m_read_doc["role"].GetString();
        memcpy(xutil->token, token, 16);

        if ("__log_server__" == xutil->info.role) {
            m_log_conn = conn;
            if (m_log_cache.empty())
                return;

            const char *start = m_log_cache.data(), *end = &m_log_cache.back()+1;
            while (start < end) {
                size_t remain = end-start;
                if (remain < sizeof(simp_header))
                    break;

                auto header = (simp_header*)start;
                size_t log_len = sizeof(simp_header)+header->length;
                if (remain < log_len)
                    break;

                m_app_cmd.cmd = header->cmd;
                send_data(header->type, m_log_conn, m_app_cmd, start, log_len);
                start += log_len;
            }
            m_log_cache.clear();
        } else {
            m_on_connect(xutil->info);
        }
    }

    void simpack_server_impl::say_goodbye(std::shared_ptr<simpack_xutil>& xutil)
    {
        //挥手
        m_write_doc.SetObject();
        Document::AllocatorType& alloc = m_write_doc.GetAllocator();
        m_write_doc.AddMember("name", Value().SetString(xutil->info.name.c_str(),
                (unsigned)xutil->info.name.size()), alloc);
        m_write_doc.Accept(m_writer);
        m_write_buf.append_zero();

        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_GOODBYE;
        send_package(3, xutil->info.conn, m_app_cmd, true, xutil->token,
                m_write_buf.GetString(), m_write_buf.GetSize());
        m_write_buf.reset();
        m_writer.Reset(m_write_buf);

        //通知服务下线之后断开对应连接
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        sch_impl->remove_event(xutil->info.conn);
    }

    void simpack_server_impl::handle_goodbye(int conn)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        if ("__log_server__" != xutil->info.role)
            m_on_disconnect(xutil->info);
        sch_impl->remove_event(conn);
    }

    int simpack_protocol(char *data, size_t len)
    {
        if (len < sizeof(simp_header))      //还未取到simp协议头
            return 0;

        uint32_t magic_num = ntohl(*(uint32_t *)data);
        if (0x5f3759df != magic_num) {
            size_t i = 1;
            for (; i < len; ++i) {
                magic_num = ntohl(*(uint32_t*)(data+i));
                if (0x5f3759df == magic_num)
                    break;
            }

            if (i < len)        //在后续流中找到该魔数，截断之前的无效流
                return -(int)i;
            else        //未找到，截断整个无效流
                return -(int)len;
        }

        auto header = (simp_header*)data;
        int ctx_len = ntohl(header->length)+sizeof(simp_header);

        if (len < ctx_len)
            return 0;
        else
            return ctx_len;
    }
}