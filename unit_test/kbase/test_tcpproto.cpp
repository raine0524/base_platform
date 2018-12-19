#include "stdafx.h"

class TcpProtoTest : public MockFileSystem
{
public:
    void tcp_test_helper(bool client, int conn, const std::string& ip, uint16_t port, char *data, size_t len);

protected:
    void SetUp() override
    {
        g_mock_fs = this;
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
        m_tcp_client = m_sch.get_tcp_client(std::bind(&TcpProtoTest::tcp_test_helper, this, true, _1, _2, _3, _4, _5));
        m_tcp_server = m_sch.get_tcp_server(0, std::bind(&TcpProtoTest::tcp_test_helper, this, false, _1, _2, _3, _4, _5));
        m_send_data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 1234567890 ";
    }

    void TearDown() override
    {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        close(impl->m_epoll_fd);
    }

    crx::scheduler m_sch;
    crx::tcp_client m_tcp_client;
    crx::tcp_server m_tcp_server;

    int m_send_cnt;
    std::string m_send_data;
    int m_cli_conn, m_svr_conn;
};

void TcpProtoTest::tcp_test_helper(bool client, int conn, const std::string& ip, uint16_t port, char *data, size_t len)
{
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    if (!data && !len) {        //连接关闭同样将通知上层
        impl->m_go_done = false;
        return;
    }

    auto recv_data = m_send_data+std::to_string(m_send_cnt);
    ASSERT_STREQ(recv_data.c_str(), data);
    ASSERT_EQ(recv_data.size(), len);
    ASSERT_STREQ(ip.c_str(), "127.0.0.1");
    if (!client) {
        m_svr_conn = conn;
        auto echo_data = m_send_data+std::to_string(++m_send_cnt);
        m_tcp_server.send_data(m_svr_conn, echo_data.c_str(), echo_data.size());
    }
    impl->m_go_done = false;
}

TEST_F(TcpProtoTest, TestTCP)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    uint16_t svr_port = m_tcp_server.get_port();
    for (int i = 0; i < 32; i++) {
        m_send_cnt = g_rand()%10000;
        m_cli_conn = m_tcp_client.connect("127.0.0.1", svr_port);
        for (int j = 0; j < 4096; j++) {
            auto send_data = m_send_data+std::to_string(++m_send_cnt);
            m_tcp_client.send_data(m_cli_conn, send_data.c_str(), send_data.size());
            sch_impl->main_coroutine(&m_sch);
            sch_impl->main_coroutine(&m_sch);
        }

        m_tcp_client.release(m_cli_conn);
        ASSERT_TRUE(sch_impl->m_ev_array.size() <= m_cli_conn || !sch_impl->m_ev_array[m_cli_conn].get());
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
