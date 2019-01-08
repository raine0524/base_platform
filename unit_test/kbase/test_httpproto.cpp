#include "stdafx.h"

class HttpProtoTest : public MockFileSystem
{
public:
    void http_client_helper(int conn, int status, std::map<std::string, std::string>& ext_headers, char *data, size_t len);

    void http_server_helper(int conn, const std::string& method, const std::string& router,
            std::map<std::string, std::string>& ext_headers, char *data, size_t len);

    void ws_test_helper(bool client, int conn, char *data, size_t len);

protected:
    void SetUp() override
    {
        g_mock_fs = this;
        srand((unsigned int)time(nullptr));
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
        m_http_client = m_sch.get_http_client(std::bind(&HttpProtoTest::http_client_helper, this, _1, _2, _3, _4, _5));
        m_http_server = m_sch.get_http_server(0, std::bind(&HttpProtoTest::http_server_helper, this, _1, _2, _3, _4, _5, _6));
        m_ws_client = m_sch.get_ws_client(std::bind(&HttpProtoTest::ws_test_helper, this, true, _1, _2, _3));
        m_ws_server = m_sch.get_ws_server(0, std::bind(&HttpProtoTest::ws_test_helper, this, false, _1, _2, _3));
        m_send_data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 1234567890 ";
    }

    void TearDown() override
    {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        close(impl->m_epoll_fd);
    }

    crx::scheduler m_sch;
    crx::http_client m_http_client;
    crx::http_server m_http_server;

    crx::ws_client m_ws_client;
    crx::ws_server m_ws_server;

    int m_send_cnt;
    std::string m_send_data;
    std::map<std::string, std::string> m_ext_headers;
};

void HttpProtoTest::http_client_helper(int conn, int status, std::map<std::string, std::string>& ext_headers, char *data, size_t len)
{
    if (!data && !len)
        return;

    ASSERT_EQ(status, 200);
    auto recv_data = m_send_data+std::to_string(m_send_cnt);
    ASSERT_STREQ(recv_data.c_str(), data);
    ASSERT_EQ(recv_data.size(), len);
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    impl->m_go_done = false;
}

void HttpProtoTest::http_server_helper(int conn, const std::string& method, const std::string& router,
        std::map<std::string, std::string>& ext_headers, char *data, size_t len)
{
    auto recv_data = m_send_data+std::to_string(m_send_cnt);
    ASSERT_STREQ(recv_data.c_str(), data);
    ASSERT_EQ(recv_data.size(), len);
    for (auto& pair : m_ext_headers)
        ASSERT_STREQ(pair.second.c_str(), ext_headers[pair.first].c_str());

    if ("/echo" == router) {
        auto echo_data = m_send_data+std::to_string(++m_send_cnt);
        m_http_server.response(conn, echo_data.c_str(), echo_data.size(), crx::DST_NONE);
    }
}

TEST_F(HttpProtoTest, TestHTTP)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    uint16_t svr_port = m_http_server.get_port();
    for (int i = 0; i < 16; i++) {
        m_send_cnt = rand()%10000;
        int conn = m_http_client.connect("127.0.0.1", svr_port);
        for (int j = 0; j < 4096; j++) {
            m_ext_headers.clear();
            for (int k = 0; k < 5; k++) {
                auto key = std::to_string(rand()%100);
                auto value = std::to_string(rand()%100);
                m_ext_headers[key] = value;
            }

            auto send_data = m_send_data+std::to_string(++m_send_cnt);
            m_http_client.POST(conn, "/echo", &m_ext_headers, send_data.c_str(), send_data.size(), crx::DST_NONE);
            sch_impl->main_coroutine();
        }

        m_http_client.release(conn);
        ASSERT_TRUE(sch_impl->m_ev_array.size() <= conn || !sch_impl->m_ev_array[conn].get());
    }
}

void HttpProtoTest::ws_test_helper(bool client, int conn, char *data, size_t len)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    auto recv_data = m_send_data+std::to_string(m_send_cnt);
    ASSERT_STREQ(recv_data.c_str(), data);
    ASSERT_EQ(recv_data.size(), len);

    if (!client) {
        for (int j = 0; j < 4096; j++) {
            auto notify_data = m_send_data+std::to_string(++m_send_cnt);
            m_ws_server.send_data(conn, notify_data.c_str(), notify_data.size());
            sch_impl->main_coroutine();
        }
    }
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    impl->m_go_done = false;
}

TEST_F(HttpProtoTest, TestWebsocket)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    uint16_t svr_port = m_ws_server.get_port();
    for (int i = 0; i < 16; i++) {
        m_send_cnt = rand()%10000;
        int conn = m_ws_client.connect_with_upgrade("127.0.0.1", svr_port);

        auto send_data = m_send_data+std::to_string(++m_send_cnt);
        m_ws_client.send_data(conn, send_data.c_str(), send_data.size());
        sch_impl->main_coroutine();
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
