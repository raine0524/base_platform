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
        m_curr_time = {m_rand()%10000, m_rand()%10000};
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
        int calc_cnt = m_rand() % 10000, previous = m_count;
        m_interval = {m_rand()%10000, m_rand()%10000};
        int64_t meas_time = crx::measure_time(std::bind(&SysTimeTest::measure_callback, this, calc_cnt));
        int next = m_count;
        ASSERT_EQ(meas_time, m_interval.tv_sec*1000000+m_interval.tv_usec);
        ASSERT_EQ(calc_cnt, next-previous);
    }
}

TEST_F(SysTimeTest, GetDateTime)
{
    for (int i = 0; i < 16; i++) {
        timeval tv = {m_rand()%10000, m_rand()%10000};
        crx::datetime dt = crx::get_datetime(&tv);
        ASSERT_EQ(dt.time_stamp, tv.tv_sec*1000+tv.tv_usec/1000);

        m_interval = {m_rand()%10000, m_rand()%10000};
        dt = crx::get_datetime(nullptr);
        ASSERT_EQ(dt.time_stamp, (m_curr_time.tv_sec-m_interval.tv_sec)*1000+(m_curr_time.tv_usec-m_interval.tv_usec)/1000);
    }
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}