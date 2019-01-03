#pragma once

namespace crx
{
    struct etcd_worker_info
    {
        bool valid;
        std::string service_name;
        std::string node_name;
        endpoint node_addr;
    };

    struct etcd_master_info
    {
        bool valid, get_result;
        std::string nonwait_path, wait_path;
        int conn;

        std::string service_name;
        int rr_idx;
    };

    class etcd_client_impl : public impl
    {
    public:
        etcd_client_impl() : m_worker_conn(-1), m_get_wsts(1) {}

        void periodic_heart_beat();     // worker

        void periodic_wait_event(int index);     // master

        void http_client_callback(int conn, int status, std::map<std::string, std::string>& ext_headers, char *data, size_t len);

        void update_worker_info(rapidjson::Document& doc);

        std::weak_ptr<scheduler_impl> m_sch_impl;
        http_client m_http_client;
        std::vector<endpoint> m_endpoints;
        rapidjson::Document m_doc;

        // etcd worker
        int m_worker_conn, m_get_wsts;      // wsts: 1-not get 2-get 3-bye
        std::string m_worker_path, m_worker_value;
        endpoint m_worker_addr;

        // etcd master
        ETCD_STRATEGY m_strategy;
        std::vector<etcd_master_info> m_master_infos;
        std::map<std::string, std::vector<etcd_worker_info>> m_workers;   // it->first: service_name
    };
}
