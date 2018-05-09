#include "registry.h"

void registry::tcp_server_callback(int conn, const std::string& ip, uint16_t port, char *data, size_t len, void *arg)
{
    auto reg = (registry*)arg;
}

bool registry::init(int argc, char **argv)
{
    crx::ini ini;
    ini.load("ini/server.ini");
    ini.set_section("registry");
    int listen = ini.get_int("listen");
    m_tcp_server = get_tcp_server((uint16_t)listen, tcp_server_callback, this);

    const char *xml_conf = "ini/node_conf.xml";
    if (access(xml_conf, F_OK)) {        //不存在则创建
        FILE *fp = fopen(xml_conf, "w");
        fputs("<config><node></node><conn></conn></config>", fp);
        fclose(fp);
    }

    m_xml.load("ini/node_conf.xml", "config");
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

void registry::destroy()
{

}

void registry::add_node(std::vector<std::string>& args, crx::console *c)
{
    auto reg = dynamic_cast<registry*>(c);
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

    if (reg->m_node_idx.end() != reg->m_node_idx.find(name_it->second)) {
        std::cout<<"[add_node] 节点 "<<name_it->second<<" 已存在，不再添加"<<std::endl;
        return;
    }

    auto node = std::make_shared<node_info>();
    node->info.name = name_it->second;
    node->info.role = role_it->second;
    node->info.conn = -1;
    if (reg->m_node_uslots.empty()) {
        reg->m_node_idx[name_it->second] = reg->m_nodes.size();
        reg->m_nodes.push_back(std::move(node));
    } else {
        auto slot = reg->m_node_uslots.front();
        reg->m_node_uslots.pop_front();
        reg->m_node_idx[name_it->second] = slot;
        reg->m_nodes[slot] = std::move(node);
    }
    std::cout<<"[add_node] 节点 "<<name_it->second<<" 创建成功！"<<std::endl;

    reg->m_xml.switch_child("node");
    reg->m_xml.set_child(name_it->second.c_str(), nullptr, false);
    reg->m_xml.switch_child(name_it->second.c_str());
    reg->m_xml.set_attribute("role", role_it->second.c_str());
    reg->m_xml.switch_parent();
    reg->m_xml.switch_parent();
}

void registry::del_node(std::vector<std::string>& args, crx::console *c)
{
    auto reg = dynamic_cast<registry*>(c);
    bool del_occur = false;
    for (auto& arg : args) {
        auto idx_it = reg->m_node_idx.find(arg);
        if (reg->m_node_idx.end() == reg->m_node_idx.find(arg)) {
            std::cout<<"[del_node] 未知节点 "<<arg<<std::endl;
            continue;
        }

        //删除该节点前把所有与该节点相关的连接都断开
        auto& node = reg->m_nodes[idx_it->second];
        reg->m_xml.switch_child("conn");
        for (auto& cli : node->clients) {
            auto conn_mangle = reg->m_nodes[cli]->info.name+"-"+arg;
            auto conn_it = reg->m_conn_idx.find(conn_mangle);
            if (reg->m_conn_idx.end() != conn_it) {
                reg->m_conns[conn_it->second].reset();
                reg->m_conn_idx.erase(conn_it);
                reg->m_conn_uslots.push_back(conn_it->second);
            }
            if (reg->m_xml.find_child(conn_mangle.c_str()))
                reg->m_xml.delete_child(conn_mangle.c_str(), false);
            std::cout<<"[del_node] 连接 "<<reg->m_nodes[cli]->info.name<<"(主动) <-----> "<<arg<<"(被动) 断开！"<<std::endl;
        }

        for (auto& svr : node->servers) {
            auto conn_mangle = arg+"-"+reg->m_nodes[svr]->info.name;
            auto conn_it = reg->m_conn_idx.find(conn_mangle);
            if (reg->m_conn_idx.end() != conn_it) {
                reg->m_conns[conn_it->second].reset();
                reg->m_conn_idx.erase(conn_it);
                reg->m_conn_uslots.push_back(conn_it->second);
            }
            if (reg->m_xml.find_child(conn_mangle.c_str()))
                reg->m_xml.delete_child(conn_mangle.c_str(), false);
            std::cout<<"[del_node] 连接 "<<arg<<"(主动) <-----> "<<reg->m_nodes[svr]->info.name<<"(被动) 断开！"<<std::endl;
        }
        reg->m_xml.switch_parent();
        reg->m_nodes[idx_it->second].reset();
        reg->m_node_idx.erase(idx_it);
        reg->m_node_uslots.push_back(idx_it->second);

        reg->m_xml.switch_child("node");
        if (reg->m_xml.find_child(arg.c_str()))
            reg->m_xml.delete_child(arg.c_str(), false);
        reg->m_xml.switch_parent();

        std::cout<<"[del_node] 节点 "<<arg<<" 删除成功！"<<std::endl;
        del_occur = true;
    }
    if (del_occur)
        reg->m_xml.flush();
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

void registry::cst_conn(std::vector<std::string>& args, crx::console *c)
{
    auto reg = dynamic_cast<registry*>(c);
    if (!reg->check_connect_cmd(args))
        return;

    auto conn_mangle1 = args[0]+"-"+args[1];
    if (reg->m_conn_idx.end() != reg->m_conn_idx.find(conn_mangle1)) {
        std::cout<<"[cst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 已存在"<<std::endl;
        return;
    }

    auto conn_mangle2 = args[1]+"-"+args[0];
    if (reg->m_conn_idx.end() != reg->m_conn_idx.find(conn_mangle2)) {
        std::cout<<"[cst_conn] 连接 "<<args[1]<<"(主动) <-----> "<<args[0]<<"(被动) 已存在，忽略反向连接"<<std::endl;
        return;
    }

    auto cli_idx = reg->m_node_idx[args[0]];
    auto svr_idx = reg->m_node_idx[args[1]];
    reg->m_nodes[cli_idx]->servers.insert(svr_idx);
    reg->m_nodes[svr_idx]->clients.insert(cli_idx);

    auto conn = std::make_shared<conn_info>();
    conn->cli_idx = cli_idx;
    conn->svr_idx = svr_idx;
    conn->online = false;
    if (reg->m_conn_uslots.empty()) {
        reg->m_conn_idx[conn_mangle1] = reg->m_conns.size();
        reg->m_conns.push_back(std::move(conn));
    } else {
        auto slot = reg->m_conn_uslots.front();
        reg->m_conn_uslots.pop_front();
        reg->m_conn_idx[conn_mangle1] = slot;
        reg->m_conns[slot] = std::move(conn);
    }

    reg->m_xml.switch_child("conn");
    reg->m_xml.set_child(conn_mangle1.c_str(), nullptr, false);
    reg->m_xml.switch_child(conn_mangle1.c_str());
    reg->m_xml.set_attribute("cli", args[0].c_str(), false);
    reg->m_xml.set_attribute("svr", args[1].c_str());
    reg->m_xml.switch_parent();
    reg->m_xml.switch_parent();
    std::cout<<"[cst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 创建成功！"<<std::endl;
}

void registry::dst_conn(std::vector<std::string>& args, crx::console *c)
{
    auto reg = dynamic_cast<registry*>(c);
    if (!reg->check_connect_cmd(args))
        return;

    auto conn_mangle = args[0]+"-"+args[1];
    auto conn_it = reg->m_conn_idx.find(conn_mangle);
    if (reg->m_conn_idx.end() == conn_it) {
        std::cout<<"[dst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 不存在"<<std::endl;
        return;
    }

    auto cli_idx = reg->m_node_idx[args[0]];
    auto svr_idx = reg->m_node_idx[args[1]];
    reg->m_nodes[cli_idx]->servers.erase(svr_idx);
    reg->m_nodes[svr_idx]->clients.erase(cli_idx);

    reg->m_conns[conn_it->second].reset();
    reg->m_conn_idx.erase(conn_it);
    reg->m_conn_uslots.push_back(conn_it->second);

    reg->m_xml.switch_child("conn");
    reg->m_xml.delete_child(conn_mangle.c_str());
    reg->m_xml.switch_parent();
    std::cout<<"[dst_conn] 连接 "<<args[0]<<"(主动) <-----> "<<args[1]<<"(被动) 断开！"<<std::endl;
}

void registry::display_nodes(std::vector<std::string>& args, crx::console *c)
{
    auto reg = dynamic_cast<registry*>(c);
    std::cout<<'\n'<<std::setw(10)<<' '<<std::setw(0)<<"[名字]"<<std::setw(10)<<' '<<std::setw(0)<<"[角色]"<<
             std::setw(18)<<' '<<std::setw(0)<<"[IP地址:端口]\n"<<std::setw(8)<<' '<<std::setw(0)
             <<"-------------------------------------------------------"<<std::endl;
    for (auto& node : reg->m_nodes) {
        if (!node)
            continue;

        std::string addr_info = "     off";
        if (node->info.conn != -1)
            addr_info = node->info.ip+":"+std::to_string(node->info.port);
        std::cout<<std::setw(10)<<' '<<std::setw(16)<<node->info.name<<std::setw(24)<<node->info.role<<addr_info<<std::endl;
    }
    std::cout<<std::endl;
}

void registry::display_conns(std::vector<std::string>& args, crx::console *c)
{
    auto reg = dynamic_cast<registry*>(c);
    std::cout<<'\n'<<std::setw(10)<<' '<<std::setw(0)<<"[主动节点]"<<std::setw(12)<<' '<<std::setw(0)<<"[状态]"<<
             std::setw(12)<<' '<<std::setw(0)<<"[被动节点]\n"<<std::setw(8)<<' '<<std::setw(0)
             <<"----------------------------------------------------------"<<std::endl;
    for (auto& conn : reg->m_conns) {
        if (!conn)
            continue;

        std::string sts = conn->online ? "<----->" : "---x---";
        std::cout<<std::setw(10)<<' '<<std::setw(22)<<reg->m_nodes[conn->cli_idx]->info.name<<std::setw(18)
                 <<sts<<reg->m_nodes[conn->svr_idx]->info.name<<std::endl;
    }
    std::cout<<std::endl;
}

int main(int argc, char *argv[])
{
    registry reg;
    reg.add_cmd("a", reg.add_node, "增加一个节点@usage: a name={名称} role={角色}");
    reg.add_cmd("d", reg.del_node, "删除一个节点@usage: d {节点名称} ...");
    reg.add_cmd("c", reg.cst_conn, "构造一对连接@usage: c {主动节点} {被动节点}");
    reg.add_cmd("b", reg.dst_conn, "析构一对连接@usage: b {主动节点} {被动节点}");
    reg.add_cmd("in", reg.display_nodes, "显示所有节点信息");
    reg.add_cmd("ic", reg.display_conns, "显示所有连接信息");
    return reg.run(argc, argv);
}