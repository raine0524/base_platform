#include "stdafx.h"

struct co_sates
{
    bool go, is_share;
    std::string comment;
    int main_rand, copy_rand;
};

class SchedulerTest : public MockFileSystem
{
public:
    void co_test_helper(crx::scheduler *sch, size_t co_id);

    void check_satellites(std::shared_ptr<crx::scheduler_impl>& impl);

    void co_test_adder(crx::scheduler *sch, size_t co_id);

protected:
    void SetUp() override
    {
        g_mock_fs = this;
        g_mock_fs->m_hook_ewait = false;
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        std::function<void(crx::scheduler *sch, size_t co_id)> stub;
        impl->co_create(stub, &m_sch, true, false, "main_coroutine");
    }

    void TearDown() override
    {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
        impl->m_cos.clear();
        impl->m_unused_cos.clear();
    }

    crx::scheduler m_sch;
    std::map<size_t, co_sates> m_co_sates;
    int m_test_num, m_accu_sum;
    int m_adder_cnt, m_adder_comp;
};

void SchedulerTest::co_test_helper(crx::scheduler *sch, size_t co_id)
{
    auto& sates = m_co_sates[co_id];
    sates.go = true;
    sates.copy_rand = sates.main_rand;

    while (sates.go) {
        sch->co_yield(0);
    }
}

void SchedulerTest::check_satellites(std::shared_ptr<crx::scheduler_impl>& impl)
{
    for (auto& pair : m_co_sates) {     // 验证创建的co附带的卫星数据是否正确
        auto& sates = m_co_sates[pair.first];
        auto& co_impl = impl->m_cos[pair.first];

        std::string co_comment = "co_helper_"+std::to_string(pair.first);
        ASSERT_STREQ(co_comment.c_str(), co_impl->comment);
        ASSERT_TRUE(sates.is_share == co_impl->is_share);
        ASSERT_EQ(sates.main_rand, sates.copy_rand);
    }

    auto avail_cos = m_sch.get_avail_cos();
    for (auto& co : avail_cos) {
        if (0 != co->co_id) {
            ASSERT_EQ(crx::CO_SUSPEND, co->status);
            std::string co_comment = "co_helper_"+std::to_string(co->co_id);
            ASSERT_STREQ(co_comment.c_str(), co->comment);
        } else {        // main co
            ASSERT_EQ(crx::CO_RUNNING, co->status);
            ASSERT_STREQ("main_coroutine", co->comment);
        }
    }
}

TEST_F(SchedulerTest, CoCreateYield)
{
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    int test_cos = 2500+g_rand()%1000;
    for (int i = 1; i < test_cos; i++) {        // 首先创建一批co
        bool is_share = g_rand()%2 == 1;
        std::string co_comment = "co_helper_"+std::to_string(i);
        size_t co_id = m_sch.co_create(std::bind(&SchedulerTest::co_test_helper, this, _1, _2), is_share, co_comment.c_str());

        auto& sates = m_co_sates[co_id];
        sates.is_share = is_share;
        sates.comment = std::move(co_comment);
        sates.main_rand = g_rand()%10000;
        m_sch.co_yield(co_id);
    }
    check_satellites(impl);

    for (int i = 0; i < 10; i++) {      // 来回测试，共测5次
        int operate_cnt = g_rand()%500+1000;
        size_t total_cos = impl->m_cos.size()-1;        // 去掉main co
        for (int j = 0; j < operate_cnt; j++) {
            if (0 == i%2) {     // 移除一些co
                size_t select_id = INT_MAX;
                while (m_co_sates.end() == m_co_sates.find(select_id)) {
                    select_id = g_rand()%total_cos+1;        // 找到存在的co
                }

                m_co_sates[select_id].go = false;
                m_sch.co_yield(select_id);
                m_co_sates.erase(select_id);
            } else {        // 添加一些co
                bool is_share = g_rand()%2 == 1;
                std::string co_comment = "co_helper_";
                if (impl->m_unused_cos.empty())
                    co_comment += std::to_string(impl->m_cos.size());
                else
                    co_comment += std::to_string(impl->m_unused_cos.front());
                size_t co_id = m_sch.co_create(std::bind(&SchedulerTest::co_test_helper, this, _1, _2), is_share, co_comment.c_str());

                auto& sates = m_co_sates[co_id];
                sates.is_share = is_share;
                sates.comment = std::move(co_comment);
                sates.main_rand = g_rand()%10000;
                m_sch.co_yield(co_id);
            }
        }
        check_satellites(impl);
    }
}

void SchedulerTest::co_test_adder(crx::scheduler *sch, size_t co_id)
{
    int calc_num = m_test_num/m_adder_cnt;
    int yield_cnt = g_rand()%5+20;       // 尝试切20次左右
    int yield_calc = calc_num/yield_cnt, j = 0;
    for (int i = 0; i < calc_num; i++) {
        m_accu_sum++;
        j++;
        if (j == yield_calc) {      // 切回main co，have a rest
            j = 0;
            m_sch.co_yield(0, crx::HAVE_REST);
        }
    }

    m_adder_comp++;
    if (m_adder_cnt == m_adder_comp) {
        auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(sch->m_impl);
        impl->m_go_done = false;
    }
}

TEST_F(SchedulerTest, CoAdder)
{
    auto impl = std::dynamic_pointer_cast<crx::scheduler_impl>(m_sch.m_impl);
    impl->m_epoll_fd = epoll_create(crx::EPOLL_SIZE);
    for (int i = 0; i < 10; i++) {      // 循环测试10次
        m_accu_sum = 0;
        m_adder_comp = 0;
        m_adder_cnt = g_rand()%5+20;       // 创建20个左右的加法器adder
        m_test_num = g_rand()%100000+1000000;     // 计算1百万次左右
        m_test_num = m_test_num/m_adder_cnt*m_adder_cnt;

        for (int j = 0; j < m_adder_cnt; j++) {
            size_t co_id = m_sch.co_create(std::bind(&SchedulerTest::co_test_adder, this, _1, _2), g_rand()%5 <= 2);
            m_sch.co_yield(co_id);
        }

        // 等待所有co退出
        impl->main_coroutine(&m_sch);
        ASSERT_EQ(m_accu_sum, m_test_num);
    }
    close(impl->m_epoll_fd);
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
