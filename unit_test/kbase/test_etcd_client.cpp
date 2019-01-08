#include "stdafx.h"

class EtcdClientTest : public MockFileSystem
{
public:
    void SetUp() override;

    void TearDown() override
    {
        auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        close(sch_impl->m_epoll_fd);
    }

    void etcd_server_callback(int conn, const std::string& method, const std::string& router,
            std::map<std::string, std::string>& ext_headers, char *data, size_t len);

    crx::scheduler m_sch;
    crx::endpoint m_endpoint;
    crx::etcd_client m_etcd_client;
    crx::http_server m_etcd_server;
    std::string m_value;
    int m_test_sts, m_event_sts;     // test_sts: 1-test heart beat 2-test wait event
};

void EtcdClientTest::SetUp()
{
    g_mock_fs = this;
    auto sch_impl = std::make_shared<crx::scheduler_impl>();
    sch_impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
    m_sch.m_impl = sch_impl;

    auto etcd_impl = std::make_shared<crx::etcd_client_impl>();
    etcd_impl->m_test = true;
    etcd_impl->m_sch_impl = sch_impl;
    etcd_impl->m_http_client = m_sch.get_http_client(std::bind(&crx::etcd_client_impl::http_client_callback,
            etcd_impl.get(), _1, _2, _3, _4, _5));
    auto http_impl = std::dynamic_pointer_cast<crx::http_impl_t<crx::tcp_client_impl>>(etcd_impl->m_http_client.m_impl);
    http_impl->m_util.m_f = [this](int fd, const std::string& ip_addr, uint16_t port, char *data, size_t len) {
        auto this_etcd_impl = std::dynamic_pointer_cast<crx::etcd_client_impl>(m_etcd_client.m_impl);
        auto this_http_impl = std::dynamic_pointer_cast<crx::http_impl_t<crx::tcp_client_impl>>(this_etcd_impl->m_http_client.m_impl);
        crx::tcp_callback_for_http<std::shared_ptr<crx::http_impl_t<crx::tcp_client_impl>>, crx::tcp_client_conn>(
                true, this_http_impl, fd, data, len);
    };

    strcpy(m_endpoint.ip_addr, "127.0.0.1");
    m_endpoint.port = 10001;
    etcd_impl->m_endpoints.push_back(m_endpoint);
    m_etcd_server = m_sch.get_http_server(m_endpoint.port, std::bind(&EtcdClientTest::etcd_server_callback,
            this, _1, _2, _3, _4, _5, _6));

    m_endpoint.port = rand()%10000+10000;
    etcd_impl->m_worker_addr = m_endpoint;
    etcd_impl->m_worker_path = "/v2/keys/test/foo/01";

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    doc.SetObject();
    auto& alloc = doc.GetAllocator();
    doc.AddMember("service", "foo", alloc);
    doc.AddMember("node", "01", alloc);
    doc.AddMember("ip", rapidjson::Value().SetString(m_endpoint.ip_addr, (int)strlen(m_endpoint.ip_addr)), alloc);
    doc.AddMember("port", m_endpoint.port, alloc);

    doc.Accept(writer);
    m_value = buffer.GetString();
    etcd_impl->m_worker_value = std::string("value=")+m_value+"&&ttl=5";

    crx::etcd_master_info info;
    info.valid = false;
    info.nonwait_path = "/v2/keys/test/foo";
    info.wait_path = info.nonwait_path+"?wait=true&recursive=true";
    info.conn = -1;

    info.service_name = "foo";
    info.rr_idx = 0;
    etcd_impl->m_master_infos.emplace_back(info);
    sch_impl->m_util_impls[crx::ETCD_CLI] = etcd_impl;
}

void EtcdClientTest::etcd_server_callback(int conn, const std::string& method, const std::string& router,
        std::map<std::string, std::string>& ext_headers, char *data, size_t len)
{
    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    std::map<std::string, std::string> resp_headers = {
            {"Connection", "close"},
            {"Date", "Thu, 03 Jan 2019 07:58:50 GMT"},
            {"X-Etcd-Cluster-Id", "792ac9dca95a9d52"},
            {"X-Etcd-Index", "14696"},
            {"X-Raft-Index", "113073"},
            {"X-Raft-Term", "3882"},
    };

    int http_status = 200;
    auto etcd_impl = std::dynamic_pointer_cast<crx::etcd_client_impl>(m_etcd_client.m_impl);

    if (1 == m_test_sts) {      // test heart beat
        if (1 == etcd_impl->m_get_wsts) {      // not found
            http_status = 404;
            doc.AddMember("errorCode", 100, alloc);
            doc.AddMember("message", "Key not found", alloc);
            doc.AddMember("cause", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            doc.AddMember("index", 14695, alloc);
        } else if (3 == etcd_impl->m_get_wsts) {        // delete worker info
            doc.AddMember("action", "delete", alloc);
            rapidjson::Value node(rapidjson::kObjectType);
            node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node.AddMember("modifiedIndex", 14730, alloc);
            node.AddMember("createdIndex", 14729, alloc);
            doc.AddMember("node", node, alloc);
            rapidjson::Value prev_node(rapidjson::kObjectType);
            prev_node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            prev_node.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            prev_node.AddMember("expiration", "2019-01-03T09:11:29.000736777Z", alloc);
            prev_node.AddMember("ttl", 5, alloc);
            prev_node.AddMember("modifiedIndex", 14729, alloc);
            prev_node.AddMember("createdIndex", 14729, alloc);
            doc.AddMember("prevNode", prev_node, alloc);
        } else if (2 == etcd_impl->m_get_wsts) {        // set worker info
            const char *value_sig = "value=";
            const char *value_start = strstr(data, value_sig)+strlen(value_sig);
            const char *value_end = strstr(data, "&&");
            std::string value = std::string(value_start, value_end-value_start);
            ASSERT_STREQ(value.c_str(), m_value.c_str());

            doc.AddMember("action", "set", alloc);
            rapidjson::Value node(rapidjson::kObjectType);
            node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            node.AddMember("expiration", "2019-01-03T07:58:58.866437782Z", alloc);
            node.AddMember("ttl", 5, alloc);
            node.AddMember("modifiedIndex", 14697, alloc);
            node.AddMember("createdIndex", 14697, alloc);
            doc.AddMember("node", node, alloc);
            rapidjson::Value prev_node(rapidjson::kObjectType);
            prev_node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            prev_node.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            prev_node.AddMember("expiration", "2019-01-03T07:58:55.866844988Z", alloc);
            prev_node.AddMember("ttl", 2, alloc);
            prev_node.AddMember("modifiedIndex", 14696, alloc);
            prev_node.AddMember("createdIndex", 14696, alloc);
            doc.AddMember("prevNode", prev_node, alloc);
        }
    } else if (2 == m_test_sts) {       // test wait event
        if (1 == m_event_sts) {     // not found
            http_status = 404;
            doc.AddMember("errorCode", 100, alloc);
            doc.AddMember("message", "Key not found", alloc);
            doc.AddMember("cause", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            doc.AddMember("index", 14733, alloc);
        } else if (2 == m_event_sts) {      // dir is empty
            doc.AddMember("action", "get", alloc);
            rapidjson::Value node(rapidjson::kObjectType);
            node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node.AddMember("dir", true, alloc);
            node.AddMember("modifiedIndex", 14696, alloc);
            node.AddMember("createdIndex", 14696, alloc);
            doc.AddMember("node", node, alloc);
        } else if (3 == m_event_sts) {      // dir not empty
            doc.AddMember("action", "get", alloc);
            rapidjson::Value node(rapidjson::kObjectType);
            node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node.AddMember("dir", true, alloc);
            node.AddMember("modifiedIndex", 14696, alloc);
            node.AddMember("createdIndex", 14696, alloc);
            rapidjson::Value nodes(rapidjson::kArrayType);
            rapidjson::Value node01(rapidjson::kObjectType);
            node01.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node01.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            node01.AddMember("expiration", "2019-01-03T07:59:31.866711758Z", alloc);
            node01.AddMember("ttl", 5, alloc);
            node01.AddMember("modifiedIndex", 14708, alloc);
            node01.AddMember("createdIndex", 14708, alloc);
            nodes.PushBack(node01, alloc);
            node.AddMember("nodes", nodes, alloc);
            doc.AddMember("node", node, alloc);
        } else if (4 == m_event_sts) {      // expire or delete event
            resp_headers["Transfer-Encoding"] = "chunked";
            if (1 == rand()%2)
                doc.AddMember("action", "expire", alloc);
            else
                doc.AddMember("action", "delete", alloc);
            rapidjson::Value node(rapidjson::kObjectType);
            node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node.AddMember("modifiedIndex", 14725, alloc);
            node.AddMember("createdIndex", 14724, alloc);
            doc.AddMember("node", node, alloc);
            rapidjson::Value prev_node(rapidjson::kObjectType);
            prev_node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            prev_node.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            prev_node.AddMember("expiration", "2019-01-03T08:00:20.066793792Z", alloc);
            prev_node.AddMember("modifiedIndex", 14724, alloc);
            prev_node.AddMember("createdIndex", 14724, alloc);
            doc.AddMember("prevNode", prev_node, alloc);

            doc.Accept(writer);
            std::string resp_str = std::string("130\r\n")+buffer.GetString()+"\r\n0\r\n\r\n";
            m_etcd_server.response(conn, resp_str.c_str(), resp_str.size(), crx::DST_JSON, http_status, &resp_headers);
            return;
        } else if (5 == m_event_sts) {      // set event
            resp_headers["Transfer-Encoding"] = "chunked";
            doc.AddMember("action", "set", alloc);
            rapidjson::Value node(rapidjson::kObjectType);
            node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            node.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            node.AddMember("expiration", "2019-01-03T07:59:52.966712169Z", alloc);
            node.AddMember("ttl", 5, alloc);
            node.AddMember("modifiedIndex", 14715, alloc);
            node.AddMember("createdIndex", 14715, alloc);
            doc.AddMember("node", node, alloc);
            rapidjson::Value prev_node(rapidjson::kObjectType);
            prev_node.AddMember("key", rapidjson::Value().SetString(router.c_str(), (int)router.size()), alloc);
            prev_node.AddMember("value", rapidjson::Value().SetString(m_value.c_str(), (int)m_value.size()), alloc);
            prev_node.AddMember("expiration", "2019-01-03T07:59:49.966529131Z", alloc);
            prev_node.AddMember("ttl", 2, alloc);
            prev_node.AddMember("modifiedIndex", 14714, alloc);
            prev_node.AddMember("createdIndex", 14714, alloc);
            doc.AddMember("prevNode", prev_node, alloc);

            doc.Accept(writer);
            std::string resp_str = std::string("1c3\r\n")+buffer.GetString()+"\r\n0\r\n\r\n";
            m_etcd_server.response(conn, resp_str.c_str(), resp_str.size(), crx::DST_JSON, http_status, &resp_headers);
            return;
        }
    }

    doc.Accept(writer);
    m_etcd_server.response(conn, buffer.GetString(), buffer.GetSize(), crx::DST_JSON, http_status, &resp_headers);
}

TEST_F(EtcdClientTest, TestPeriodicHeartBeat)
{
    m_test_sts = 1;
    m_etcd_client = m_sch.get_etcd_client();
    auto etcd_impl = std::dynamic_pointer_cast<crx::etcd_client_impl>(m_etcd_client.m_impl);
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    crx::endpoint point = m_etcd_client.get_listen_addr();
    ASSERT_STREQ(point.ip_addr, m_endpoint.ip_addr);
    ASSERT_EQ(point.port, m_endpoint.port);

    etcd_impl->m_get_wsts = 1;      // test not found
    etcd_impl->periodic_heart_beat();
    sch_impl->main_coroutine();
    ASSERT_EQ(2, etcd_impl->m_get_wsts);

    etcd_impl->m_get_wsts = 3;      // test delete worker info
    etcd_impl->periodic_heart_beat();
    sch_impl->main_coroutine();
    ASSERT_EQ(4, etcd_impl->m_get_wsts);

    etcd_impl->m_get_wsts = 2;      // test set worker info
    for (int i = 0; i < 4096; i++) {
        etcd_impl->periodic_heart_beat();
        sch_impl->main_coroutine();
        ASSERT_EQ(2, etcd_impl->m_get_wsts);
    }
}

TEST_F(EtcdClientTest, TestPeriodicWaitEvent)
{
    m_test_sts = 2;
    m_etcd_client = m_sch.get_etcd_client();
    auto etcd_impl = std::dynamic_pointer_cast<crx::etcd_client_impl>(m_etcd_client.m_impl);
    ASSERT_EQ(etcd_impl->m_strategy, crx::ES_ROUNDROBIN);
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);

    auto& info = etcd_impl->m_master_infos.front();
    info.valid = false;
    m_event_sts = 1;        // test not found
    etcd_impl->m_workers[info.service_name].emplace_back();
    ASSERT_FALSE(etcd_impl->m_workers[info.service_name].empty());
    etcd_impl->periodic_wait_event(0);
    sch_impl->main_coroutine();
    ASSERT_TRUE(etcd_impl->m_workers[info.service_name].empty());

    info.valid = false;
    m_event_sts = 2;        // test dir is empty
    etcd_impl->m_workers[info.service_name].emplace_back();
    ASSERT_FALSE(etcd_impl->m_workers[info.service_name].empty());
    etcd_impl->periodic_wait_event(0);
    sch_impl->main_coroutine();
    ASSERT_TRUE(etcd_impl->m_workers[info.service_name].empty());
    ASSERT_FALSE(info.get_result);

    info.valid = false;
    m_event_sts = 3;        // test dir not empty
    etcd_impl->periodic_wait_event(0);
    sch_impl->main_coroutine();
    ASSERT_FALSE(etcd_impl->m_workers[info.service_name].empty());
    ASSERT_TRUE(info.get_result);
    ASSERT_TRUE(etcd_impl->m_workers[info.service_name].front().valid);

    info.valid = true;
    m_event_sts = 4;        // test expire or delete event
    for (int i = 0; i < 128; i++) {
        etcd_impl->periodic_wait_event(0);
        sch_impl->main_coroutine();
        ASSERT_FALSE(etcd_impl->m_workers[info.service_name].empty());
        ASSERT_TRUE(info.get_result);
        ASSERT_FALSE(etcd_impl->m_workers[info.service_name].front().valid);
    }

    info.valid = true;
    m_event_sts = 5;        // test set event
    for (int i = 0; i < 4096; i++) {
        etcd_impl->periodic_wait_event(0);
        sch_impl->main_coroutine();
        ASSERT_FALSE(etcd_impl->m_workers[info.service_name].empty());
        ASSERT_TRUE(info.get_result);
        ASSERT_TRUE(etcd_impl->m_workers[info.service_name].front().valid);
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
