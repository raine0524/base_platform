#include "registry.h"

void registry::tcp_server_callback(int conn, const std::string& ip, uint16_t port, char *data, size_t len)
{
    if (data && len) {
        auto header_len = sizeof(crx::simp_header);
        if (len <= header_len) {
            printf("invalid request pkg_len=%lu, header_len=%lu\n", len, header_len);
            return;
        }

        auto header = (crx::simp_header*)data;          //协议头
        auto body = data+header_len;
        auto body_len = len-header_len;
        auto kvs = m_seria.dump(body, body_len);
        if (kvs.empty()) {
            printf("empty request body\n");
            return;
        }

        uint16_t cmd = ntohs(header->cmd);
        if (CMD_REG_NAME != cmd) {
            auto conn_it = m_conn_node.find(conn);
            if (m_conn_node.end() == conn_it) {
                printf("invalid connection: %d\n", conn);
                return;
            }

            auto& node = m_nodes[conn_it->second];
            if (memcmp(node->token, header->token, 16)) {
                printf("illegal request name=%s cmd=%u\n", node->info.name.c_str(), cmd);
                return;
            }
        }

        switch (cmd) {
            case CMD_REG_NAME:      register_server(conn, ip, port, kvs);           break;
            case CMD_SVR_ONLINE:    notify_server_online(conn, kvs);                break;
            case CMD_CONN_CON:      notify_connect_msg(true, kvs);                  break;
            case CMD_CONN_DES:      notify_connect_msg(false, kvs);                 break;
            case CMD_GOODBYE:       server_offline(conn, kvs);                      break;
            default:                printf("unknown cmd=%d\n", header->cmd);        break;
        }
    } else {
        auto conn_it = m_conn_node.find(conn);
        if (m_conn_node.end() == conn_it)      //正常下线会发送下线通知，此时已将node与conn之间的关联关系删除
            return;

        auto& node = m_nodes[conn_it->second];
        if (node) {
            node->info.conn = -1;       //offline
            printf("[ERROR]node %s[%s] offline\n", node->info.name.c_str(), node->info.role.c_str());
        }
        m_conn_node.erase(conn_it);
    }
}

void registry::setup_header(crx::mem_ref& ref, crx::simp_header *header, uint16_t cmd, uint16_t *result)
{
    header->length = htonl((uint32_t)(ref.len-sizeof(crx::simp_header)));
    header->cmd = htons(cmd);
    if (result)
        header->result = htons(*result);
    SET_BIT(header->ctl_flag, 0);       //由库处理
    SET_BIT(header->ctl_flag, 3);       //由registry发送
    header->ctl_flag = htonl(header->ctl_flag);
}

void registry::register_server(int conn, const std::string& ip, uint16_t port,
                               std::unordered_map<std::string, crx::mem_ref>& kvs)
{
    auto ip_it = kvs.find("ip");
    if (kvs.end() == ip_it) {
        printf("pronounce without ip\n");
        return;
    }
    std::string node_ip(ip_it->second.data, ip_it->second.len);

    auto port_it = kvs.find("port");
    if (kvs.end() == port_it) {
        printf("pronounce without port\n");
        return;
    }
    uint16_t listen = ntohs(*(uint16_t*)port_it->second.data);

    auto name_it = kvs.find("name");
    if (kvs.end() == name_it) {
        printf("pronounce without name");
        return;
    }
    std::string node_name(name_it->second.data, name_it->second.len);
    printf("ip=%s, listen=%u, name=%s\n", node_ip.c_str(), listen, node_name.c_str());

    auto node_it = m_node_idx.find(node_name);
    uint16_t result = 0;
    bool node_legal = false;
    if (m_node_idx.end() == node_it) {      //node illegal
        result = 1;
        std::string error_info = "unknown name";
        error_info.push_back(0);
        m_seria.insert("error_info", error_info.c_str(), error_info.size());
        printf("node %s illegal\n", node_name.c_str());
    } else {
        //node legal
        auto& node = m_nodes[node_it->second];
        if (-1 == node->info.conn) {
            node_legal = true;
            node->info.conn = conn;
            node->info.ip = std::move(node_ip);
            node->info.port = listen;
            m_conn_node[conn] = node_it->second;        //建立连接与节点信息的对应关系
            m_seria.insert("role", node->info.role.c_str(), node->info.role.size());

            std::string clients;
            for (auto& client : node->clients)
                clients.append(std::string(m_nodes[client]->info.name)+",");

            if (',' == clients.back())
                clients.pop_back();

            if (!clients.empty())
                m_seria.insert("clients", clients.c_str(), clients.size());
            printf("%s pronounce succ\n", node_name.c_str());
        } else {        //node register ever
            result = 2;
            std::string error_info = node_name;
            error_info.append(" repeat register").push_back(0);
            m_seria.insert("error_info", error_info.c_str(), error_info.size());
            printf("node %s repeat resiter\n", node_name.c_str());
        }
    }

    auto ref = m_seria.get_string();
    auto header = (crx::simp_header*)ref.data;
    setup_header(ref, header, CMD_REG_NAME, &result);

    if (node_legal) {
        crx::datetime dt = crx::get_datetime();
        std::string time_with_name = std::to_string(dt.date)+std::to_string(dt.time)+"-"+node_name;
        MD5((const unsigned char*)time_with_name.c_str(), time_with_name.size(), header->token);
        memcpy(m_nodes[node_it->second]->token, header->token, 16);
    }
    m_tcp_server.send_data(conn, ref.data, ref.len);
    m_seria.reset();
}

void registry::notify_server_online(int conn, std::unordered_map<std::string, crx::mem_ref>& kvs)
{
    auto name_it = kvs.find("name");
    if (kvs.end() == name_it) {
        printf("notify online without name\n");
        return;
    }
    std::string node_name(name_it->second.data, name_it->second.len);

    auto node_it = m_node_idx.find(node_name);
    if (m_node_idx.end() == node_it) {
        printf("server online: unknown name %s\n", node_name.c_str());
        return;
    }

    auto& node = m_nodes[node_it->second];
    uint16_t net_port = htons(node->info.port);
    m_seria.insert("name", node->info.name.c_str(), node->info.name.size());
    m_seria.insert("ip", node->info.ip.c_str(), node->info.ip.size());
    m_seria.insert("port", (const char*)&net_port, sizeof(net_port));

    auto ref = m_seria.get_string();
    auto header = (crx::simp_header*)ref.data;
    setup_header(ref, header, CMD_SVR_ONLINE, nullptr);
    memcpy(header->token, node->token, 16);

    for (auto& client : node->clients) {        //通知所有已在线的主动连接方发起连接
        auto& cli_node = m_nodes[client];
        if (cli_node && -1 != cli_node->info.conn)
            m_tcp_server.send_data(cli_node->info.conn, ref.data, ref.len);
    }
    m_seria.reset();

    for (auto& server : node->servers) {        //通知该服务连接所有已在线的被动方
        auto& svr_node = m_nodes[server];
        if (!svr_node || -1 == svr_node->info.conn)
            continue;

        uint16_t net_port = htons(svr_node->info.port);
        m_seria.insert("name", svr_node->info.name.c_str(), svr_node->info.name.size());
        m_seria.insert("ip", svr_node->info.ip.c_str(), svr_node->info.ip.size());
        m_seria.insert("port", (const char*)&net_port, sizeof(net_port));

        auto ref = m_seria.get_string();
        auto header = (crx::simp_header*)ref.data;
        setup_header(ref, header, CMD_SVR_ONLINE, nullptr);
        memcpy(header->token, svr_node->token, 16);
        m_tcp_server.send_data(conn, ref.data, ref.len);
        m_seria.reset();
    }
}

void registry::notify_connect_msg(bool online, std::unordered_map<std::string, crx::mem_ref>& kvs)
{
    auto cli_it = kvs.find("client");
    if (kvs.end() == cli_it) {
        printf("(dis)connect message without client\n");
        return;
    }
    std::string cli_name(cli_it->second.data, cli_it->second.len);

    auto svr_it = kvs.find("server");
    if (kvs.end() == svr_it) {
        printf("(dis)connect message without server\n");
        return;
    }
    std::string svr_name(svr_it->second.data, svr_it->second.len);
    std::string conn_mangle = cli_name+"-"+svr_name;

    auto conn_it = m_conn_idx.find(conn_mangle);
    if (m_conn_idx.end() == conn_it) {
        printf("unknown connect mangle: %s\n", conn_mangle.c_str());
        return;
    }
    auto& conn = m_conns[conn_it->second];
    conn->online = online;
}

void registry::server_offline(int conn, std::unordered_map<std::string, crx::mem_ref>& kvs)
{
    auto name_it = kvs.find("name");
    if (kvs.end() == name_it) {
        printf("server offline without name\n");
        return;
    }
    std::string node_name(name_it->second.data, name_it->second.len);

    auto node_it = m_node_idx.find(node_name);
    if (m_node_idx.end() == node_it) {
        printf("server offline with known name\n");
        return;
    }

    auto& node = m_nodes[node_it->second];
    if (node) {
        node->info.conn = -1;       //offline
        printf("node %s[%s] offline %d\n", node->info.name.c_str(), node->info.role.c_str(), conn);

        //与该节点相关的所有连接都应处于断开状态
        for (auto& client : node->clients) {
            std::string conn_mangle = m_nodes[client]->info.name+"-"+node_name;
            auto conn_it = m_conn_idx.find(conn_mangle);
            if (m_conn_idx.end() != conn_it)
                m_conns[conn_it->second]->online = false;
        }

        for (auto& server : node->servers) {
            std::string conn_mangle = node_name+"-"+m_nodes[server]->info.name;
            auto conn_it = m_conn_idx.find(conn_mangle);
            if (m_conn_idx.end() != conn_it)
                m_conns[conn_it->second]->online = false;
        }
    }
    m_conn_node.erase(conn);
}

bool registry::init(int argc, char *argv[])
{
    crx::ini ini;
    ini.load("ini/server.ini");
    ini.set_section("registry");
    int listen = ini.get_int("listen");
    m_tcp_server = get_tcp_server((uint16_t)listen, std::bind(&registry::tcp_server_callback, this, _1, _2, _3, _4, _5));
    register_tcp_hook(false, [](int conn, char *data, size_t len) { return crx::simpack_protocol(data, len); });

    const char *xml_conf = "ini/node_conf.xml";
    if (access(xml_conf, F_OK)) {        //不存在则创建
        FILE *fp = fopen(xml_conf, "w");
        fputs("<config><node></node><conn></conn></config>", fp);
        fclose(fp);
    }

    m_xml.load(xml_conf, "config");
    if (m_xml.find_child("node")) {
        m_xml.switch_child("node");
        m_xml.for_each_child([&](std::string& name, std::string& value, std::unordered_map<std::string, std::string>& attrs, void *arg) {
            if (m_node_idx.end() != m_node_idx.find(name)) {
                std::cout<<"[init]节点 "<<name<<" 已存在，不再重复添加"<<std::endl;
                return;
            }

            auto node = std::make_shared<node_info>();
            node->info.name = std::move(name);
            node->info.role = std::move(attrs["role"]);
            node->info.conn = -1;       //-1表示节点还未上线
            m_node_idx[node->info.name] = m_nodes.size();
            m_nodes.push_back(std::move(node));
        });
        m_xml.switch_parent();
    }

    if (m_xml.find_child("conn")) {
        m_xml.switch_child("conn");
        m_xml.for_each_child([&](std::string& name, std::string& value, std::unordered_map<std::string, std::string>& attrs, void *arg) {
            if (2 != attrs.size()) {
                std::cout<<"[init]连接 "<<name<<" 属性非法，过滤该连接信息"<<std::endl;
                return;
            }

            auto& cli = attrs["cli"];
            auto cli_it = m_node_idx.find(cli);
            if (m_node_idx.end() == cli_it) {
                std::cout<<"[init]连接 "<<name<<" 指定的client节点不存在"<<std::endl;
                return;
            }

            auto& svr = attrs["svr"];
            auto svr_it = m_node_idx.find(svr);
            if (m_node_idx.end() == svr_it) {
                std::cout<<"[init]连接 "<<name<<" 指定的server节点不存在"<<std::endl;
                return;
            }

            auto rev_mangle = svr+"-"+cli;
            if (m_conn_idx.end() != m_conn_idx.find(rev_mangle)) {
                std::cout<<"[init]反向连接 "<<svr<<"(主动) <-----> "<<cli<<"(被动) 已存在"<<std::endl;
                return;
            }

            auto conn = std::make_shared<conn_info>();
            conn->cli_idx = cli_it->second;
            conn->svr_idx = svr_it->second;
            conn->online = false;
            m_conn_idx[name] = m_conns.size();
            m_conns.push_back(std::move(conn));
            m_nodes[cli_it->second]->servers.insert(svr_it->second);
            m_nodes[svr_it->second]->clients.insert(cli_it->second);
        });
        m_xml.switch_parent();
    }
    return true;
}

void registry::add_node(std::vector<std::string>& args)
{
    std::map<std::string, std::string> kvs;
    for (auto& arg : args) {
        auto kv = crx::split(arg.data(), arg.length(), "=");
        if (2 != kv.size())
            continue;

        std::string key = std::string(kv[0].data, kv[0].len);
        std::string value = std::string(kv[1].data, kv[1].len);
        kvs[std::move(key)] = std::move(value);
    }

    auto name_it = kvs.find("name");
    if (kvs.end() == name_it) {
        std::cout<<"[add_node] 未指定节点名称，请带选项 name={名称} 添加节点"<<std::endl;
        return;
    }

    auto role_it = kvs.find("role");
    if (kvs.end() == role_it) {
        std::cout<<"[add_node] 未指定节点角色，请带选项 role={角色} 添加节点\n"<<std::endl;
        return;
    }

    if (m_node_idx.end() != m_node_idx.find(name_it->second)) {
        std::cout<<"[add_node] 节点 "<<name_it->second<<" 已存在，不再添加"<<std::endl;
        return;
    }

    auto node = std::make_shared<node_info>();
    node->info.name = name_it->second;
    node->info.role = role_it->second;
    node->info.conn = -1;
    if (m_node_uslots.empty()) {
        m_node_idx[name_it->second] = m_nodes.size();
        m_nodes.push_back(std::move(node));
    } else {
        auto slot = m_node_uslots.front();
        m_node_uslots.pop_front();
        m_node_idx[name_it->second] = slot;
        m_nodes[slot] = std::move(node);
    }
    std::cout<<"[add_node] 节点 "<<name_it->second<<" 创建成功！"<<std::endl;

    m_xml.switch_child("node");
    m_xml.set_child(name_it->second.c_str(), nullptr, false);
    m_xml.switch_child(name_it->second.c_str());
    m_xml.set_attribute("role", role_it->second.c_str());
    m_xml.switch_parent().switch_parent();
}

void registry::del_node(std::vector<std::string>& args)
{
    bool del_occur = false;
    for (auto& arg : args) {
        auto idx_it = m_node_idx.find(arg);
        if (m_node_idx.end() == m_node_idx.find(arg)) {
            std::cout<<"[del_node] 未知节点 "<<arg<<std::endl;
            continue;
        }

        //删除该节点前把所有与该节点相关的连接都断开
        auto& node = m_nodes[idx_it->second];
        m_xml.switch_child("conn");
        for (auto& cli : node->clients) {
            auto conn_mangle = m_nodes[cli]->info.name+"-"+arg;
            auto conn_it = m_conn_idx.find(conn_mangle);
            if (m_conn_idx.end() != conn_it) {
                m_conns[conn_it->second].reset();
                m_conn_idx.erase(conn_it);
                m_conn_uslots.push_back(conn_it->second);
            }
            if (m_xml.find_child(conn_mangle.c_str()))
                m_xml.delete_child(conn_mangle.c_str(), false);
            std::cout<<"[del_node] 连接 "<<m_nodes[cli]->info.name<<"(主动) <-----> "<<arg<<"(被动) 断开！"<<std::endl;
        }

        for (auto& svr : node->servers) {
            auto conn_mangle = arg+"-"+m_nodes[svr]->info.name;
            auto conn_it = m_conn_idx.find(conn_mangle);
            if (m_conn_idx.end() != conn_it) {
                m_conns[conn_it->second].reset();
                m_conn_idx.erase(conn_it);
                m_conn_uslots.push_back(conn_it->second);
            }
            if (m_xml.find_child(conn_mangle.c_str()))
                m_xml.delete_child(conn_mangle.c_str(), false);
            std::cout<<"[del_node] 连接 "<<arg<<"(主动) <-----> "<<m_nodes[svr]->info.name<<"(被动) 断开！"<<std::endl;
        }
        m_xml.switch_parent();
        m_nodes[idx_it->second].reset();
        m_node_idx.erase(idx_it);
        m_node_uslots.push_back(idx_it->second);

        m_xml.switch_child("node");
        if (m_xml.find_child(arg.c_str()))
            m_xml.delete_child(arg.c_str(), false);
        m_xml.switch_parent();

        std::cout<<"[del_node] 节点 "<<arg<<" 删除成功！"<<std::endl;
        del_occur = true;
    }
    if (del_occur)
        m_xml.flush();
}

bool registry::check_connect_cmd(std::vector<std::string>& args)
{
    if (2 != args.size()) {
        std::cout<<"构造/析构一对连接的参数为2个节点的名称"<<std::endl;
        return false;
    }

    for (auto& arg : args) {
        if (m_node_idx.end() == m_node_idx.find(arg)) {
            std::cout<<"[cst_conn] 未知节点 "<<arg<<std::endl;
            return false;
        }
    }
    return true;
}

void registry::cst_conn(std::vector<std::string>& args)
{
    if (!check_connect_cmd(args))
        return;

    auto conn_mangle1 = args[0]+"-"+args[1];
    if (m_conn_idx.end() != m_conn_idx.find(conn_mangle1)) {
        std::cout<<"[cst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 已存在"<<std::endl;
        return;
    }

    auto conn_mangle2 = args[1]+"-"+args[0];
    if (m_conn_idx.end() != m_conn_idx.find(conn_mangle2)) {
        std::cout<<"[cst_conn] 连接 "<<args[1]<<"(主动) <-----> "<<args[0]<<"(被动) 已存在，忽略反向连接"<<std::endl;
        return;
    }

    auto cli_idx = m_node_idx[args[0]];
    auto svr_idx = m_node_idx[args[1]];
    m_nodes[cli_idx]->servers.insert(svr_idx);
    m_nodes[svr_idx]->clients.insert(cli_idx);

    auto conn = std::make_shared<conn_info>();
    conn->cli_idx = cli_idx;
    conn->svr_idx = svr_idx;
    conn->online = false;
    if (m_conn_uslots.empty()) {
        m_conn_idx[conn_mangle1] = m_conns.size();
        m_conns.push_back(std::move(conn));
    } else {
        auto slot = m_conn_uslots.front();
        m_conn_uslots.pop_front();
        m_conn_idx[conn_mangle1] = slot;
        m_conns[slot] = std::move(conn);
    }

    m_xml.switch_child("conn");
    m_xml.set_child(conn_mangle1.c_str(), nullptr, false);
    m_xml.switch_child(conn_mangle1.c_str());
    m_xml.set_attribute("cli", args[0].c_str(), false);
    m_xml.set_attribute("svr", args[1].c_str());
    m_xml.switch_parent().switch_parent();
    std::cout<<"[cst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 创建成功！"<<std::endl;
}

void registry::dst_conn(std::vector<std::string>& args)
{
    if (!check_connect_cmd(args))
        return;

    auto conn_mangle = args[0]+"-"+args[1];
    auto conn_it = m_conn_idx.find(conn_mangle);
    if (m_conn_idx.end() == conn_it) {
        std::cout<<"[dst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 不存在"<<std::endl;
        return;
    }

    auto cli_idx = m_node_idx[args[0]];
    auto svr_idx = m_node_idx[args[1]];
    m_nodes[cli_idx]->servers.erase(svr_idx);
    m_nodes[svr_idx]->clients.erase(cli_idx);

    m_conns[conn_it->second].reset();
    m_conn_idx.erase(conn_it);
    m_conn_uslots.push_back(conn_it->second);

    m_xml.switch_child("conn");
    m_xml.delete_child(conn_mangle.c_str());
    m_xml.switch_parent();
    std::cout<<"[dst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 断开！"<<std::endl;
}

void registry::display_nodes(std::vector<std::string>& args)
{
    std::cout<<'\n'<<std::setw(10)<<' '<<std::setw(0)<<"[名字]"<<std::setw(10)<<' '<<std::setw(0)<<"[角色]"<<
             std::setw(18)<<' '<<std::setw(0)<<"[IP地址:端口]\n"<<std::setw(8)<<' '<<std::setw(0)
             <<"-------------------------------------------------------"<<std::endl;
    for (auto& node : m_nodes) {
        if (!node)
            continue;

        std::string addr_info = "     off";
        if (node->info.conn != -1)
            addr_info = node->info.ip+":"+std::to_string(node->info.port);
        std::cout<<std::setw(10)<<' '<<std::setw(16)<<node->info.name<<std::setw(24)<<node->info.role<<addr_info<<std::endl;
    }
    std::cout<<std::endl;
}

void registry::display_conns(std::vector<std::string>& args)
{
    std::cout<<'\n'<<std::setw(10)<<' '<<std::setw(0)<<"[主动节点]"<<std::setw(12)<<' '<<std::setw(0)<<"[状态]"<<
             std::setw(12)<<' '<<std::setw(0)<<"[被动节点]\n"<<std::setw(8)<<' '<<std::setw(0)
             <<"----------------------------------------------------------"<<std::endl;
    for (auto& conn : m_conns) {
        if (!conn)
            continue;

        std::string sts = conn->online ? "<----->" : "---x---";
        std::cout<<std::setw(10)<<' '<<std::setw(22)<<m_nodes[conn->cli_idx]->info.name<<std::setw(18)
                 <<sts<<m_nodes[conn->svr_idx]->info.name<<std::endl;
    }
    std::cout<<std::endl;
}

int main(int argc, char *argv[])
{
    registry reg;
    reg.add_cmd("a", std::bind(&registry::add_node, &reg, _1), "增加一个节点@usage: a name={名称} role={角色}");
    reg.add_cmd("d", std::bind(&registry::del_node, &reg, _1), "删除一个节点@usage: d {节点名称} ...");
    reg.add_cmd("c", std::bind(&registry::cst_conn, &reg, _1), "构造一对连接@usage: c {主动节点} {被动节点}");
    reg.add_cmd("b", std::bind(&registry::dst_conn, &reg, _1), "析构一对连接@usage: b {主动节点} {被动节点}");
    reg.add_cmd("in", std::bind(&registry::display_nodes, &reg, _1), "显示所有节点信息");
    reg.add_cmd("ic", std::bind(&registry::display_conns, &reg, _1), "显示所有连接信息");
    return reg.run(argc, argv);
}