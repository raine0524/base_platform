#include "stdafx.h"

class SchedUtilTest : public MockFileSystem
{
public:
    void sig_test_helper(int signo, uint64_t sigval);

    void timer_test_helper(int64_t arg);

    void timer_wheel_helper(int64_t arg);

    void event_test_helper(int64_t signal);

    void udp_test_helper(bool client, const std::string& ip, uint16_t port, char *data, size_t len);

protected:
    void SetUp() override
    {
        g_mock_fs = this;
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
        m_send_data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 1234567890 ";
    }

    void TearDown() override
    {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        close(impl->m_epoll_fd);
    }

    crx::scheduler m_sch;
    std::map<int, int> m_sig_num;

    crx::timer m_tmr;
    int m_start_seed, m_rand_adder;
    uint64_t m_delay, m_interval;

    int m_send_cnt;
    std::string m_send_data;
    crx::udp_ins m_udp_client, m_udp_server;
};

void SchedUtilTest::sig_test_helper(int signo, uint64_t sigval)
{
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    auto& sig_num = m_sig_num[signo];
    sig_num += sigval;
    impl->m_go_done = false;
}

TEST_F(SchedUtilTest, TestSigCtl)
{
    crx::sigctl sc = m_sch.get_sigctl();
    ASSERT_TRUE(sc.m_impl.get());

    for (int i = 0; i < 8; i++) {
        int signo = __SIGRTMIN+g_rand()%(_NSIG-__SIGRTMIN);     // 对实时信号进行测试
        sc.add_sig(signo, std::bind(&SchedUtilTest::sig_test_helper, this, _1, _2));
        m_sig_num[signo] = g_rand()%100;
    }

    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    for (auto& pair : m_sig_num) {
        int origin = pair.second;
        int send_cnt = g_rand()%5+5;

        for (int i = 0; i < send_cnt; i++) {
            sigval_t sv;
            sv.sival_int = g_rand()%100;
            origin += sv.sival_int;
            sigqueue(getpid(), pair.first, sv);
        }

        impl->main_coroutine(&m_sch);
        ASSERT_EQ(origin, pair.second);
    }

    for (auto it = m_sig_num.begin(); it != m_sig_num.end(); ) {
        if (0 == g_rand()%2) {
            sc.remove_sig(it->first);
            it = m_sig_num.erase(it);
        } else {
            it++;
        }
    }

    auto sc_impl = std::dynamic_pointer_cast<crx::sigctl_impl>(sc.m_impl);
    for (auto& pair : m_sig_num)
        ASSERT_TRUE(sc_impl->m_sig_cb.end() != sc_impl->m_sig_cb.find(pair.first));
    ASSERT_EQ(sc_impl->m_sig_cb.size(), m_sig_num.size());
}

void SchedUtilTest::timer_test_helper(int64_t arg)
{
    auto tmr_impl = std::dynamic_pointer_cast<crx::timer_impl>(m_tmr.m_impl);
    m_start_seed += m_rand_adder;
    ASSERT_EQ(m_delay, tmr_impl->m_delay);
    ASSERT_EQ(m_interval, tmr_impl->m_interval);

    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    sch_impl->m_go_done = false;
}

TEST_F(SchedUtilTest, TestTimer)
{
    m_hook_ewait = true;
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    m_tmr = m_sch.get_timer(std::bind(&SchedUtilTest::timer_test_helper, this, _1));
    auto tmr_impl = std::dynamic_pointer_cast<crx::timer_impl>(m_tmr.m_impl);
    m_efd_cnt.emplace_back(std::make_pair(tmr_impl->fd, 1));
    for (int i = 0; i < 256; i++) {
        m_rand_adder = g_rand()%100;
        int step_result = m_start_seed+m_rand_adder;
        m_delay = g_rand()%10000+10000, m_interval = g_rand()%10000+10000;
        m_tmr.start(m_delay, m_interval);
        sch_impl->main_coroutine(&m_sch);
        ASSERT_EQ(m_start_seed, step_result);
    }

    m_tmr.detach();
    ASSERT_TRUE(sch_impl->m_ev_array.size() <= tmr_impl->fd || !sch_impl->m_ev_array[tmr_impl->fd].get());
    m_efd_cnt.clear();
}

void SchedUtilTest::timer_wheel_helper(int64_t arg)
{
    m_start_seed += m_rand_adder;
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    sch_impl->m_go_done = false;
}

TEST_F(SchedUtilTest, TestTimerWheel)
{
    m_hook_ewait = true;
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    auto tw = m_sch.get_timer_wheel();
    auto tw_impl = std::dynamic_pointer_cast<crx::timer_wheel_impl>(tw.m_impl);

    std::vector<int> tw_fds(tw_impl->m_timer_vec.size(), 0);
    for (int i = 0; i < tw_impl->m_timer_vec.size(); i++) {     // 0-24hour 1-60minutes 2-60seconds 3-10#100millis
        auto& slot = tw_impl->m_timer_vec[i];
        slot.slot_idx = g_rand()%slot.elems.size();
        tw_fds[i] = std::dynamic_pointer_cast<crx::timer_impl>(slot.tmr.m_impl)->fd;
    }

    for (int i = 0; i < tw_fds.size(); i++) {       // 0-测试小时级别 1-测试分钟级别 2-测试秒级别 3-测试微妙级别
        for (int j = 0; j < 16; j++) {
            m_rand_adder = g_rand()%100;
            int step_result = m_start_seed+m_rand_adder;

            size_t delay = 0;
            for (int k = i; k < tw_fds.size(); k++) {
                auto& slot = tw_impl->m_timer_vec[k];
                if (0 == k)
                    delay += (g_rand()%(slot.elems.size()-2)+1)*slot.tick;      // 避免delay被正则化为24小时这种极端情形
                else
                    delay += (g_rand()%(slot.elems.size()-1)+1)*slot.tick;
            }
            delay += g_rand()%100;

            for (int k = 0; k < tw_fds.size(); k++)
                m_efd_cnt.emplace_back(std::make_pair(tw_fds[k], 0));

            size_t new_delay = (delay/100+1)*100;
            int inter_start = -1;
            for (int k = 0; k < tw_fds.size(); k++) {
                auto& slot = tw_impl->m_timer_vec[k];
                if (new_delay < slot.tick)
                    continue;

                if (-1 == inter_start)
                    inter_start = k;

                int quotient = (int)(new_delay/slot.tick);
                m_efd_cnt[k].second += quotient;
                new_delay -= slot.tick*quotient;
            }

            for (int k = inter_start+1; k < tw_fds.size(); k++) {
                auto& nslot = tw_impl->m_timer_vec[k];
                m_efd_cnt[k].second += nslot.elems.size()-nslot.slot_idx-1;
            }

            tw.add_handler(delay, std::bind(&SchedUtilTest::timer_wheel_helper, this, _1));
            sch_impl->main_coroutine(&m_sch);
            ASSERT_EQ(m_start_seed, step_result);
            m_efd_cnt.clear();
        }
    }
}

void SchedUtilTest::event_test_helper(int64_t signal)
{
    ASSERT_EQ(signal, m_rand_adder);
    m_start_seed += signal;
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    impl->m_go_done = false;
}

TEST_F(SchedUtilTest, TestEvent)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    for (int i = 0; i < 16; i++) {
        auto ev = m_sch.get_event(std::bind(&SchedUtilTest::event_test_helper, this, _1));
        int ev_fd = std::dynamic_pointer_cast<crx::event_impl>(ev.m_impl)->fd;
        for (int j = 0; j < 256; j++) {
            m_rand_adder = g_rand()%100;
            int step_result = m_start_seed+m_rand_adder;
            ev.send_signal(m_rand_adder);

            sch_impl->main_coroutine(&m_sch);
            ASSERT_EQ(m_start_seed, step_result);
        }
        ev.detach();
        ASSERT_TRUE(sch_impl->m_ev_array.size() <= ev_fd || !sch_impl->m_ev_array[ev_fd].get());
    }
}

void SchedUtilTest::udp_test_helper(bool client, const std::string& ip, uint16_t port, char *data, size_t len)
{
    auto recv_data = m_send_data+std::to_string(m_send_cnt);
    ASSERT_STREQ(recv_data.c_str(), data);
    ASSERT_EQ(recv_data.size(), len);
    ASSERT_STREQ(ip.c_str(), "127.0.0.1");
    if (!client) {      // server echo
        auto echo_data = m_send_data+std::to_string(++m_send_cnt);
        m_udp_server.send_data("127.0.0.1", port, echo_data.c_str(), echo_data.size());
    }
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    impl->m_go_done = false;
}

TEST_F(SchedUtilTest, TestUDP)
{
    auto sch_impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    for (int i = 0; i < 32; i++) {
        m_send_cnt = g_rand()%10000;
        m_udp_client = m_sch.get_udp_ins(false, 0, std::bind(&SchedUtilTest::udp_test_helper, this, true, _1, _2, _3, _4));
        int cli_fd = std::dynamic_pointer_cast<crx::udp_ins_impl>(m_udp_client.m_impl)->fd;
        m_udp_server = m_sch.get_udp_ins(true, 0, std::bind(&SchedUtilTest::udp_test_helper, this, false, _1, _2, _3, _4));
        int svr_fd = std::dynamic_pointer_cast<crx::udp_ins_impl>(m_udp_server.m_impl)->fd;
        uint16_t svr_port = m_udp_server.get_port();
        for (int j = 0; j < 4096; j++) {
            auto send_data = m_send_data+std::to_string(++m_send_cnt);
            m_udp_client.send_data("127.0.0.1", svr_port, send_data.c_str(), send_data.size());
            sch_impl->main_coroutine(&m_sch);
            sch_impl->main_coroutine(&m_sch);
        }
        m_udp_client.detach();
        ASSERT_TRUE(sch_impl->m_ev_array.size() <= cli_fd || !sch_impl->m_ev_array[cli_fd].get());
        m_udp_server.detach();
        ASSERT_TRUE(sch_impl->m_ev_array.size() <= svr_fd || !sch_impl->m_ev_array[svr_fd].get());
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
