#include "stdafx.h"

namespace crx
{
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
        if (!impl) {
            auto simp_impl = std::make_shared<simpack_server_impl>();
            obj.m_impl = impl = simp_impl;

            simp_impl->m_sch = this;
            simp_impl->m_on_connect = std::move(on_connect);
            simp_impl->m_on_disconnect = std::move(on_disconnect);
            simp_impl->m_on_request = std::move(on_request);
            simp_impl->m_on_response = std::move(on_response);
            simp_impl->m_on_notify = std::move(on_notify);

            if (sch_impl->m_ini_file.empty()) {
                printf("不存在配置文件\n");
                return obj;
            }

            ini ini_parser;
            ini_parser.load(sch_impl->m_ini_file.c_str());

            if (!ini_parser.has_section("registry")) {
                printf("配置文件 %s 没有 [registry] 节区\n", sch_impl->m_ini_file.c_str());
                return obj;
            }

            //使用simpack_server组件的应用必须配置registry节区以及ip,port,name这三个字段
            auto xutil = std::make_shared<simpack_xutil>();
            ini_parser.set_section("registry");
            xutil->info.ip = ini_parser.get_str("ip");
            xutil->info.port = (uint16_t)ini_parser.get_int("port");
            xutil->info.name = ini_parser.get_str("name");
            xutil->listen = ini_parser.get_int("listen");

            //创建tcp_client用于主动连接
            simp_impl->m_client = get_tcp_client(std::bind(&simpack_server_impl::simp_callback, simp_impl.get(), _1, _2, _3, _4, _5));
            auto cli_impl = std::dynamic_pointer_cast<tcp_client_impl>(simp_impl->m_client.m_impl);
            cli_impl->m_util.m_app_prt = PRT_SIMP;
            register_tcp_hook(true, [](int conn, char *data, size_t len) {
                return simpack_protocol(data, len); });
            sch_impl->m_util_impls[TCP_CLI].reset();

            //创建tcp_server用于被动连接
            simp_impl->m_server = get_tcp_server((uint16_t)xutil->listen,
                                                 std::bind(&simpack_server_impl::simp_callback, simp_impl.get(), _1, _2, _3, _4, _5));
            auto svr_impl = std::dynamic_pointer_cast<tcp_server_impl>(simp_impl->m_server.m_impl);
            svr_impl->m_util.m_app_prt = PRT_SIMP;
            register_tcp_hook(false, [](int conn, char *data, size_t len) {
                return simpack_protocol(data, len); });
            xutil->listen = simp_impl->m_server.get_port();
            sch_impl->m_util_impls[TCP_SVR].reset();

            //只有配置了名字才连接registry，否则作为单点应用
            if (!xutil->info.name.empty()) {
                //若连接失败则每隔三秒尝试重连一次
                int conn = simp_impl->m_client.connect(xutil->info.ip.c_str(), xutil->info.port, -1, 3);
                simp_impl->m_registry_conn = xutil->info.conn = conn;
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                tcp_ev->ext_data = xutil;

                uint16_t net_port = htons((uint16_t)xutil->listen);
                simp_impl->m_seria.insert("port", (const char*)&net_port, sizeof(net_port));
                simp_impl->m_seria.insert("name", xutil->info.name.c_str(), xutil->info.name.size());
                auto ref = simp_impl->m_seria.get_string();

                //setup header
                auto header = (simp_header*)ref.data;
                header->length = htonl((uint32_t)(ref.len-sizeof(simp_header)));
                header->cmd = htons(CMD_REG_NAME);

                simp_impl->m_reg_str = std::string(ref.data, ref.len);
                simp_impl->m_client.send_data(conn, ref.data, ref.len);
                simp_impl->m_seria.reset();
            }
        }
        return obj;
    }

    void simpack_server::request(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(1, conn, cmd, data, len);
    }

    void simpack_server::response(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(2, conn, cmd, data, len);
    }

    void simpack_server::notify(int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto simp_impl = std::dynamic_pointer_cast<simpack_server_impl>(m_impl);
        simp_impl->send_data(3, conn, cmd, data, len);
    }

    void simpack_server_impl::send_data(int type, int conn, const server_cmd& cmd, const char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        if (0 < conn && conn < sch_impl->m_ev_array.size()) {
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            send_package(type, conn, cmd, false, xutil->token, data, len);
        }
    }

    void simpack_server_impl::send_package(int type, int conn, const server_cmd& cmd, bool lib_proc,
                                           unsigned char *token, const char *data, size_t len)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        simp_header *header = nullptr;
        size_t total_len = len;
        if (len >= 4 && htonl(0x5f3759df) == *(uint32_t*)data) {    //上层使用的是基于simp协议的序列化组件seria
            header = (simp_header*)data;
            header->length = htonl((uint32_t)(len-sizeof(simp_header)));
        } else {
            m_simp_buf.resize(sizeof(simp_header));
            m_simp_buf.append(data, len);
            header = (simp_header*)&m_simp_buf[0];
            header->length = htonl((uint32_t)len);
            total_len = len+sizeof(simp_header);
        }

        header->ses_id = htonl(cmd.ses_id);
        header->req_id = htonl(cmd.req_id);
        header->type = htons(cmd.type);
        header->cmd = htons(cmd.cmd);
        header->result = htons(cmd.result);

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
            if (conn < sch_impl->m_ev_array.size() && sch_impl->m_ev_array[conn]) {
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
                if ("__log_server__" != xutil->info.role)
                    m_on_disconnect(xutil->info);
                say_goodbye(xutil);
            }
        }

        if (-1 != m_registry_conn && m_registry_conn < sch_impl->m_ev_array.size() &&
            sch_impl->m_ev_array[m_registry_conn]) {
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            say_goodbye(xutil);
            m_registry_conn = -1;
        }
    }

    void simpack_server_impl::simp_callback(int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        if (!data) {            //连接开启/关闭回调
            if (len == m_registry_conn) {      //此时为重连
                printf("连接重新建立 %d->%d\n", m_registry_conn, conn);
                m_registry_conn = conn;
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
                xutil->info.conn = conn;
                m_client.send_data(conn, m_reg_str.c_str(), m_reg_str.size());
            }
            return;
        }

        auto header = (simp_header*)data;
        auto header_len = sizeof(simp_header);
        if (len <= header_len)
            return;

        m_app_cmd.ses_id = ntohl(header->ses_id);
        m_app_cmd.req_id = ntohl(header->req_id);
        m_app_cmd.type = ntohs(header->type);
        m_app_cmd.cmd = ntohs(header->cmd);
        m_app_cmd.result = ntohs(header->result);

        uint32_t ctl_flag = ntohl(header->ctl_flag);
        bool is_registry = GET_BIT(ctl_flag, 3) != 0;
        if (GET_BIT(ctl_flag, 0)) {     //捕获控制请求
            capture_sharding(is_registry, conn, ip, port, data, len);
        } else {    //路由上层请求
            auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
            if (memcmp(header->token, xutil->token, 16)) {
                printf("illegal request since token is mismatch\n");
                return;
            }

            if (GET_BIT(ctl_flag, 1)) {     //推送
                m_on_notify(xutil->info, m_app_cmd, data+header_len, len-header_len);
            } else {        //非推送
                if (GET_BIT(ctl_flag, 2))
                    m_on_request(xutil->info, m_app_cmd, data+header_len, len-header_len);
                else
                    m_on_response(xutil->info, m_app_cmd, data+header_len, len-header_len);
            }
        }
    }

    void simpack_server_impl::capture_sharding(bool registry, int conn, const std::string &ip, uint16_t port, char *data, size_t len)
    {
        auto header = (simp_header*)data;
        auto kvs = m_seria.dump(data+sizeof(simp_header), len-sizeof(simp_header));
        if (registry) {       //与registry建立的连接
            switch (m_app_cmd.cmd) {
                case CMD_REG_NAME:      handle_reg_name(conn, header->token, kvs);      break;
                case CMD_SVR_ONLINE:    handle_svr_online(header->token, kvs);          break;
                default:                printf("unknown cmd=%d\n", m_app_cmd.cmd);      break;
            }
        } else {        //其他连接
            //除了首个建立信道的命令之外，后续所有的请求/响应都需要验证token
            if (CMD_HELLO != m_app_cmd.cmd) {
                auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
                auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
                auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
                if (memcmp(header->token, xutil->token, 16)) {
                    printf("illegal request since token is mismatch\n");
                    return;
                }
            }

            switch (m_app_cmd.cmd) {
                case CMD_HELLO: {
                    if (1 == m_app_cmd.type)
                        handle_hello_request(conn, ip, port, header->token, kvs);
                    else if (2 == m_app_cmd.type)
                        handle_hello_response(conn, m_app_cmd.result, header->token, kvs);
                    break;
                }
                case CMD_GOODBYE:       handle_goodbye(conn);                               break;
                default:                printf("unknown cmd=%d\n", m_app_cmd.cmd);          break;
            }
        }
    }

    void simpack_server_impl::handle_reg_name(int conn, unsigned char *token, std::map<std::string, mem_ref>& kvs)
    {
        if (m_app_cmd.result) {    //失败
            printf("pronounce failed: %s\n", kvs["error_info"].data);
            m_client.release(conn);
            return;
        }

        //成功
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        if (m_registry_conn != conn) {
            if (-1 != m_registry_conn) {
                printf("repeat connection with registry old=%d new=%d\n", m_registry_conn, conn);
                say_goodbye(xutil);
            }
            m_registry_conn = conn;
        }

        auto role_it = kvs.find("role");        //registry一定会返回当前节点的role
        std::string role(role_it->second.data, role_it->second.len);
        printf("pronounce succ: role=%s\n", role.c_str());
        xutil->info.role = std::move(role);

        auto clients_it = kvs.find("clients");
        if (kvs.end() != clients_it) {
            std::string clients(clients_it->second.data, clients_it->second.len);
            printf("pronounce succ: clients=%s\n", clients.c_str());
            auto client_ref = split(clients.c_str(), clients.size(), ",");
            for (auto& ref : client_ref)
                xutil->clients.insert(std::string(ref.data, ref.len));
        }
        memcpy(xutil->token, token, 16);    //保存该token用于与其他服务之间的通信

        //让registry通知所有连接该服务的主动方发起连接或是让本服务连接所有的被动方
        m_seria.insert("name", xutil->info.name.c_str(), xutil->info.name.size());
        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_SVR_ONLINE;
        send_package(3, m_registry_conn, m_app_cmd, true, xutil->token, ref.data, ref.len);
        m_seria.reset();
    }

    void simpack_server_impl::handle_svr_online(unsigned char *token, std::map<std::string, mem_ref>& kvs)
    {
        auto name_it = kvs.find("name");
        std::string svr_name(name_it->second.data, name_it->second.len);

        auto ip_it = kvs.find("ip");
        std::string ip(ip_it->second.data, ip_it->second.len);

        uint16_t port = ntohs(*(uint16_t*)kvs["port"].data);

        //握手
        int conn = m_client.connect(ip.c_str(), port);
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto xutil = std::make_shared<simpack_xutil>();
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        tcp_ev->ext_data = xutil;
        xutil->info.conn = conn;
        xutil->info.ip = std::move(ip);
        xutil->info.port = port;

        auto reg_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
        auto reg_xutil = std::dynamic_pointer_cast<simpack_xutil>(reg_ev->ext_data);
        m_seria.insert("name", reg_xutil->info.name.c_str(), reg_xutil->info.name.size());
        m_seria.insert("role", reg_xutil->info.role.c_str(), reg_xutil->info.role.size());

        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_HELLO;
        m_app_cmd.type = 1;
        send_package(1, conn, m_app_cmd, true, token, ref.data, ref.len);
        m_seria.reset();
    }

    void simpack_server_impl::handle_hello_request(int conn, const std::string &ip, uint16_t port, unsigned char *token,
                                                   std::map<std::string, mem_ref>& kvs)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto reg_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[m_registry_conn]);
        auto reg_xutil = std::dynamic_pointer_cast<simpack_xutil>(reg_ev->ext_data);

        if (memcmp(token, reg_xutil->token, 16)) {
            printf("hello cmd with illegal token\n");
            return;
        }

        auto name_it = kvs.find("name");
        std::string cli_name(name_it->second.data, name_it->second.len);

        auto role_it = kvs.find("role");
        std::string cli_role(role_it->second.data, role_it->second.len);

        bool client_valid = reg_xutil->clients.end() != reg_xutil->clients.find(cli_name);
        printf("client %s[%s], conn=%d, ip=%s, port=%u say hello to me, valid=%d\n", cli_name.c_str(),
               cli_role.c_str(), conn, ip.c_str(), port, client_valid);

        unsigned char *new_token = nullptr;
        std::shared_ptr<simpack_xutil> ord_xutil;
        if (client_valid) {
            ord_xutil = std::make_shared<simpack_xutil>();
            auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
            tcp_ev->ext_data = ord_xutil;
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
            m_seria.insert("name", reg_xutil->info.name.c_str(), reg_xutil->info.name.size());
            m_seria.insert("role", reg_xutil->info.role.c_str(), reg_xutil->info.role.size());
        } else {
            std::string error_info = "unknown name ";
            error_info.append(cli_name).push_back(0);
            m_seria.insert("error_info", error_info.c_str(), error_info.size());
        }

        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_HELLO;
        m_app_cmd.type = 2;     //响应
        m_app_cmd.result = (uint16_t)(client_valid ? 0 : 1);
        send_package(2, conn, m_app_cmd, true, new_token, ref.data, ref.len);
        m_seria.reset();

        if (client_valid) {     //通知registry连接建立
            m_seria.insert("client", ord_xutil->info.name.c_str(), ord_xutil->info.name.size());
            m_seria.insert("server", reg_xutil->info.name.c_str(), reg_xutil->info.name.size());

            auto nfy_ref = m_seria.get_string();
            bzero(&m_app_cmd, sizeof(m_app_cmd));
            m_app_cmd.cmd = CMD_CONN_CON;
            send_package(1, reg_xutil->info.conn, m_app_cmd, true, reg_xutil->token, nfy_ref.data, nfy_ref.len);
            m_seria.reset();
        }
    }

    void simpack_server_impl::handle_hello_response(int conn, uint16_t result, unsigned char *token,
                                                    std::map<std::string, mem_ref>& kvs)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_sch->m_impl);
        auto tcp_ev = std::dynamic_pointer_cast<tcp_event>(sch_impl->m_ev_array[conn]);
        auto xutil = std::dynamic_pointer_cast<simpack_xutil>(tcp_ev->ext_data);
        if (result) {
            printf("say hello response error: %s\n", kvs["error_info"].data);
            sch_impl->remove_event(conn);
            return;
        }

        m_ordinary_conn.insert(conn);
        auto name_it = kvs.find("name");
        xutil->info.name = std::string(name_it->second.data, name_it->second.len);
        auto role_it = kvs.find("role");
        xutil->info.role = std::string(role_it->second.data, role_it->second.len);
        memcpy(xutil->token, token, 16);
        if ("__log_server__" == xutil->info.role) {
            m_log_conn = conn;
            for (auto& data : m_cache_logs) {
                auto header = (simp_header*)data.c_str();
                m_app_cmd.cmd = header->cmd;
                send_data(header->type, m_log_conn, m_app_cmd, data.c_str(), data.size());
            }
            m_cache_logs.clear();
        } else {
            m_on_connect(xutil->info);
        }
    }

    void simpack_server_impl::say_goodbye(std::shared_ptr<simpack_xutil>& xutil)
    {
        //挥手
        m_seria.insert("name", xutil->info.name.c_str(), xutil->info.name.size());
        auto ref = m_seria.get_string();
        bzero(&m_app_cmd, sizeof(m_app_cmd));
        m_app_cmd.cmd = CMD_GOODBYE;
        send_package(3, xutil->info.conn, m_app_cmd, true, xutil->token, ref.data, ref.len);
        m_seria.reset();

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