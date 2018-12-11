#include "stdafx.h"

class SchedUtilTest : public MockFileSystem
{
public:
    void sig_test_helper(int signo, uint64_t sigval);

    void timer_test_help(int64_t arg);

protected:
    void SetUp() override
    {
        g_mock_fs = this;
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
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

void SchedUtilTest::timer_test_help(int64_t arg)
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
    m_tmr = m_sch.get_timer(std::bind(&SchedUtilTest::timer_test_help, this, _1));
    auto tmr_impl = std::dynamic_pointer_cast<crx::timer_impl>(m_tmr.m_impl);
    m_ewait_fd = tmr_impl->fd;
    for (int i = 0; i < 256; i++) {
        m_rand_adder = g_rand()%100;
        int step_result = m_start_seed+m_rand_adder;
        m_delay = g_rand()%10000+10000, m_interval = g_rand()%10000+10000;
        m_tmr.start(m_delay, m_interval);
        sch_impl->main_coroutine(&m_sch);
        ASSERT_EQ(m_start_seed, step_result);
    }

    m_tmr.detach();
    ASSERT_TRUE(sch_impl->m_ev_array.size() <= m_ewait_fd || !sch_impl->m_ev_array[m_ewait_fd].get());
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
