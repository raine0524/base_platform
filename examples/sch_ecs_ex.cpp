#include "crx_pch.h"

struct test_arg
{
    int key;
    int value;
};

class sch_ecs : public crx::console
{
public:
    virtual bool init(int argc, char *argv[]);

    virtual void destroy(){}

    void simulate_callback();

private:
    crx::ecs_trans *m_trans;
    std::unordered_map<int, int> m_kvs;

    size_t m_get_cnt;
    std::random_device m_random;
};

void sch_ecs::simulate_callback()
{
    m_kvs.clear();
    m_get_cnt = 0;
    size_t arg_cnt = (m_random()%10)+10;
    for (size_t i = 0; i < arg_cnt; ++i)
        m_kvs[m_random()%100] = m_random()%100;
    printf("总共生成 %ld 对随机数\n", m_kvs.size());

    std::thread([&]{
        std::vector<crx::mem_ref> cb_args;
        for (auto& pair : m_kvs) {
            test_arg arg = {.key = pair.first, .value = pair.second};
            cb_args.push_back(crx::mem_ref((const char*)&arg, sizeof(arg)));
            m_trans->feed_args(cb_args);
            cb_args.clear();
        }
    }).detach();
}

bool sch_ecs::init(int argc, char *argv[])
{
    m_trans = get_ecs_trans([&](std::vector<crx::mem_ref>& cb_args, void *args) {
        assert(1 == cb_args.size());
        auto arg = (test_arg*)cb_args.front().data;
        m_get_cnt++;
        printf("arg %ld: key=%d, value=%d ", m_get_cnt, arg->key, arg->value);
        auto it = m_kvs.find(arg->key);
        if (m_kvs.end() != it && arg->value == it->second)
            printf("true\n");
        else
            printf("false\n");

        if (m_get_cnt >= m_kvs.size())
            printf("当前已取得 %ld 个回调参数，总数为 %ld，测试完成\n", m_get_cnt, m_kvs.size());
    });
    return true;
}

int main(int argc, char *argv[])
{
    sch_ecs ecs;
    ecs.add_cmd("sc", [&](std::vector<std::string>& args, crx::console *c) {
        ecs.simulate_callback();
    }, "模拟线程回调");
    ecs.run(argc, argv);
}
