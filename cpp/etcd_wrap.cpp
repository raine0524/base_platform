#include "stdafx.h"

namespace crx
{
    etcd_client scheduler::get_etcd_client(ETCD_STRATEGY strategy /*= ES_ROUNDROBIN*/)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto etcd_impl = std::dynamic_pointer_cast<etcd_client_impl>(sch_impl->m_util_impls[ETCD_CLI]);
        etcd_impl->m_strategy = strategy;
        etcd_client obj;
        obj.m_impl = etcd_impl;
        return obj;
    }

    endpoint etcd_client::get_worker(const std::string& svr_name)
    {
        auto impl = std::dynamic_pointer_cast<etcd_client_impl>(m_impl);
        int index = -1;
        for (int i = 0; i < impl->m_master_infos.size(); i++) {
            if (svr_name == impl->m_master_infos[i].service_name) {
                index = i;
                break;
            }
        }

        if (-1 == index)
            return endpoint();

        auto& workers = impl->m_workers[svr_name];
        auto& info = impl->m_master_infos[index];
        endpoint point = workers[info.rr_idx].node_addr;
        info.rr_idx++;
        if (info.rr_idx == workers.size())
            info.rr_idx = 0;
        return point;
    }

    void etcd_client_impl::periodic_heart_beat()
    {
        if (-1 == m_worker_conn) {
            size_t idx = rand()%m_endpoints.size();
            auto& point = m_endpoints[idx];
            m_worker_conn = m_http_client.connect(point.ip_addr, (uint16_t)point.port);
        }

        if (-1 != m_worker_conn) {
            if (1 == m_get_wsts)
                m_http_client.GET(m_worker_conn, m_worker_path.c_str(), nullptr);
            else if (2 == m_get_wsts)
                m_http_client.request(m_worker_conn, "PUT", m_worker_path.c_str(), nullptr,
                        m_worker_value.c_str(), m_worker_value.size(), crx::DST_QSTRING);
            else if (3 == m_get_wsts)
                m_http_client.request(m_worker_conn, "DELETE", m_worker_path.c_str(), nullptr, nullptr, 0);
        }

        m_sch_impl.lock()->m_wheel.add_handler(3*1000, std::bind(&etcd_client_impl::periodic_heart_beat, this));
    }

    void etcd_client_impl::periodic_wait_event(int index)
    {
        auto& info = m_master_infos[index];
        if (-1 == info.conn) {
            size_t idx = rand()%m_endpoints.size();
            auto& point = m_endpoints[idx];
            info.conn = m_http_client.connect(point.ip_addr, (uint16_t)point.port);
        }

        auto sch_impl = m_sch_impl.lock();
        if (-1 != info.conn) {      //连接成功
            info.get_result = false;
            if (info.valid)
                m_http_client.GET(info.conn, info.wait_path.c_str(), nullptr);
            else
                m_http_client.GET(info.conn, info.nonwait_path.c_str(), nullptr);

            sch_impl->m_wheel.add_handler(7*1000, [this, index](int64_t) {
                auto& this_info = m_master_infos[index];
                this_info.valid = this_info.get_result;
                auto this_sch_impl = m_sch_impl.lock();
                this_sch_impl->m_wheel.add_handler(3*1000, std::bind(&etcd_client_impl::periodic_wait_event, this, index));
            });
        } else {    //连接失败，尝试重连
            sch_impl->m_wheel.add_handler(5*1000, std::bind(&etcd_client_impl::periodic_wait_event, this, index));
        }
    }

    void etcd_client_impl::update_worker_info(rapidjson::Document& doc)
    {
        if (!doc.IsObject())
            return;

        etcd_worker_info winfo;
        winfo.valid = true;
        winfo.service_name = doc["service"].GetString();
        winfo.node_name = doc["node"].GetString();
        strcpy(winfo.node_addr.ip_addr, doc["ip"].GetString());
        winfo.node_addr.port = doc["port"].GetInt();
        auto& workers = m_workers[winfo.service_name];

        bool find = false;
        for (auto& worker_info : workers) {
            if (worker_info.node_name == winfo.node_name) {
                worker_info = winfo;
                find = true;
                break;
            }
        }

        if (!find)
            workers.emplace_back(winfo);
    }

    void etcd_client_impl::http_client_callback(int conn, int status,
            std::map<std::string, std::string>& ext_headers, char *data, size_t len)
    {
        bool close = false;     //判断是否需要关闭连接
        auto it = ext_headers.find("Connection");
        if (ext_headers.end() != it && "close" == it->second)
            close = true;

        int chunk = 0;      // 0-非chunk 1-有载荷的chunk 2-空的chunk
        it = ext_headers.find("Transfer-Encoding");
        if (ext_headers.end() != it && "chunked" == it->second) {
            chunk = 1;
            if (!strncmp(data, "0\r\n\r\n", 5))
                chunk = 2;
        }

        if (chunk < 2) {
            g_lib_log.printf(LVL_INFO, "conn=%d, status=%d\n", conn, status);
            for (auto& pair : ext_headers)
                g_lib_log.printf(LVL_INFO, "===> %s: %s\n", pair.first.c_str(), pair.second.c_str());
            g_lib_log.printf(LVL_INFO, "===> %s\n\n", data);
        }

        if (m_worker_conn == conn) {
            if (1 == m_get_wsts) {         //worker注册前首先获取本节点相关信息
                if ('\n' == data[len-1])
                    data[len-1] = 0;
                m_doc.Parse(data, len);

                if (404 == status) {
                    if (100 == m_doc["errorCode"].GetInt())     // not found
                        m_get_wsts = 2;
                    else
                        g_lib_log.printf(LVL_WARN, "worker get etcd error message: %s\n", m_doc["message"].GetString());
                } else if (200 == status) {
                    auto node = m_doc["node"].GetObject();
                    if (node.HasMember("key") && node.HasMember("value"))
                        g_lib_log.printf(LVL_FATAL, "worker=%s has been registered: %s\n",
                                         node["key"].GetString(), node["value"].GetString());
                } else {
                    g_lib_log.printf(LVL_WARN, "get etcd unexpected response: %s[%d]\n", data, status);
                }
            } else if (3 == m_get_wsts) {       // delete succ
                m_get_wsts = 4;
                m_sch_impl.lock()->m_go_done = false;
            }

            if (close) {
                g_lib_log.printf(LVL_INFO, "worker connection=%d for etcd closed\n", m_worker_conn);
                m_http_client.release(m_worker_conn);
                m_worker_conn = -1;
            }
            return;
        }

        //master
        int index = -1;
        for (int i = 0; i < m_master_infos.size(); i++) {
            if (m_master_infos[i].conn == conn) {
                index = i;
                break;
            }
        }
        if (-1 == index)    // unknown master response
            return;

        auto& info = m_master_infos[index];
        if (close) {
            g_lib_log.printf(LVL_INFO, "master connection=%d for etcd closed\n", info.conn);
            m_http_client.release(info.conn);
            info.conn = -1;
        }

        if (2 == chunk)     // 不处理没有body块的响应
            return;

        if (200 == status) {
            if (chunk) {
                const char *json_start = strstr(data, "\r\n")+2;
                const char *json_end = strstr(json_start, "\r\n");
                m_doc.Parse(json_start, json_end-json_start);
            } else {
                m_doc.Parse(data, len);
            }

            info.get_result = true;
            rapidjson::Document sub_doc;
            const char *action = m_doc["action"].GetString();
            if (!strcmp(action, "get")) {
                auto node = m_doc["node"].GetObject();
                auto& node_key = node["key"];
                const char *last_slash_pos = strrchr(node_key.GetString(), '/');
                const char *last_char_pos = node_key.GetString()+node_key.GetStringLength();
                std::string service_name = std::string(last_slash_pos+1, last_char_pos-last_slash_pos-1);
                m_workers[service_name].clear();

                if (node.HasMember("nodes")) {
                    for(auto& sub_node : node["nodes"].GetArray()) {
                        sub_doc.Parse(sub_node["value"].GetString(), sub_node["value"].GetStringLength());
                        update_worker_info(sub_doc);
                    }
                    info.get_result = true;
                } else {
                    info.get_result = false;
                }
            } else if (!strcmp(action, "set")) {
                auto node = m_doc["node"].GetObject();
                sub_doc.Parse(node["value"].GetString(), node["value"].GetStringLength());
                update_worker_info(sub_doc);
            } else if (!strcmp(action, "expire") || !strcmp(action, "delete")) {
                auto node = m_doc["prevNode"].GetObject();
                sub_doc.Parse(node["value"].GetString(), node["value"].GetStringLength());
                const char *service_name = sub_doc["service"].GetString();
                const char *node_name = sub_doc["node"].GetString();

                auto& workers = m_workers[service_name];
                for (auto& worker_info : workers) {
                    if (!strcmp(worker_info.node_name.c_str(), node_name)) {
                        worker_info.valid = false;
                        break;
                    }
                }
            }
        } else {
            if ('\n' == data[len-1])
                data[len-1] = 0;
            g_lib_log.printf(LVL_WARN, "master watch etcd unexpected event: %s\n", data);

            m_doc.Parse(data, len);
            if (100 == m_doc["errorCode"]) {        // key not found
                auto& cause_kv = m_doc["cause"];
                auto last_slash_pos = strrchr(cause_kv.GetString(), '/');
                const char *last_char_pos = cause_kv.GetString()+cause_kv.GetStringLength();
                std::string service_name = std::string(last_slash_pos+1, last_char_pos-last_slash_pos-1);
                m_workers[service_name].clear();
            }
        }
    }
}
