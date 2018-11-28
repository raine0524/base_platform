#include "stdafx.h"

class SysTimeTest : public MockSystemTime
{
public:
    void measure_callback(int calc_cnt)
    {
        for (int i = 0; i < calc_cnt; i++)
            m_count++;
    }

protected:
    virtual void SetUp()
    {
        g_mock_st = this;
        m_count = 0;
        m_curr_time = {g_rand()%10000, g_rand()%10000};
    }

    virtual void TearDown()
    {
        g_mock_st = nullptr;    //set null to avoid core dump
    }

    int m_count;
};

TEST_F(SysTimeTest, MeasureTime)
{
    for (int i = 0; i < 16; i++) {
        int calc_cnt = g_rand() % 10000, previous = m_count;
        m_interval = {g_rand()%10000, g_rand()%10000};
        int64_t meas_time = crx::measure_time(std::bind(&SysTimeTest::measure_callback, this, calc_cnt));
        int next = m_count;
        ASSERT_EQ(meas_time, m_interval.tv_sec*1000000+m_interval.tv_usec);
        ASSERT_EQ(calc_cnt, next-previous);
    }
}

TEST_F(SysTimeTest, GetDateTime)
{
    for (int i = 0; i < 16; i++) {
        timeval tv = {g_rand()%10000, g_rand()%10000};
        crx::datetime dt = crx::get_datetime(&tv);
        ASSERT_EQ(dt.time_stamp, tv.tv_sec*1000+tv.tv_usec/1000);

        m_interval = {g_rand()%10000, g_rand()%10000};
        dt = crx::get_datetime(nullptr);
        ASSERT_EQ(dt.time_stamp, (m_curr_time.tv_sec-m_interval.tv_sec)*1000+(m_curr_time.tv_usec-m_interval.tv_usec)/1000);
    }
}

TEST_F(SysTimeTest, GetWeekDay)
{
    ASSERT_EQ(3, crx::get_week_day(20170621));
    ASSERT_EQ(4, crx::get_week_day(20181108));
    ASSERT_EQ(1, crx::get_week_day(20180604));
    ASSERT_EQ(7, crx::get_week_day(20180225));
    ASSERT_EQ(5, crx::get_week_day(20190712));
    ASSERT_EQ(2, crx::get_week_day(20200505));
    ASSERT_EQ(6, crx::get_week_day(20181222));
}

TEST_F(SysTimeTest, GetNthDay)
{
    ASSERT_EQ(20181115, crx::get_Nth_day(20181108, 7));
    ASSERT_EQ(20180421, crx::get_Nth_day(20180223, 57));
    ASSERT_EQ(20171223, crx::get_Nth_day(20180308, -75));
    ASSERT_EQ(20170730, crx::get_Nth_day(20171213, -136));
    ASSERT_EQ(20180302, crx::get_Nth_day(20170606, 269));
    ASSERT_EQ(20180824, crx::get_Nth_day(20170915, 343));
    ASSERT_EQ(20181216, crx::get_Nth_day(20200202, -413));
    ASSERT_EQ(20000729, crx::get_Nth_day(20000202, 178));
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
