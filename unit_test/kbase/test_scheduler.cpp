#include "stdafx.h"

class SchedulerTest : public testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}

    crx::scheduler m_sch;
};

TEST_F(SchedulerTest, CoCreate)
{

}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
