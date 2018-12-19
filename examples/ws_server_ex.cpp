#include "crx_pch.h"

class ws_server : public crx::console
{
public:
    ws_server() = default;
    ~ws_server() = default;

    bool init(int argc, char **argv) override;

    void destroy() override {}

    void http_callback(int conn, const char *method, const char *router,
            std::map<std::string, const char*>&ext_headers, char *data, size_t len);

    void notify_data(int count);

private:
    int m_conn;
    std::random_device m_rand;
    crx::http_server m_server;
};

bool ws_server::init(int argc, char **argv)
{
    m_server = get_http_server(5123, std::bind(&ws_server::http_callback, this, _1, _2, _3, _4, _5, _6));
    return true;
}

void ws_server::http_callback(int conn, const char *method, const char *router,
        std::map<std::string, const char *> &ext_headers, char *data, size_t len) {
    m_conn = conn;
    printf("--- conn=%d, method=%s, router=%s\n", conn, method, router);
    for (auto& pair : ext_headers)
        printf("\t%s ==> %s\n", pair.first.c_str(), pair.second);
    if (data && len)
        printf("\tplayload: %s\n", data);
}

void ws_server::notify_data(int count)
{
    size_t co_id = co_create([this, count](scheduler *sch, size_t co_id) {
        rapidjson::Document doc;
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

        doc.SetObject();
        rapidjson::Document::AllocatorType& alloc = doc.GetAllocator();
        for (int i = 0; i < count; ++i) {
            int rand = m_rand();
            doc.AddMember("from", rapidjson::Value().SetString("my websocket server"), alloc);
            doc.AddMember("data", rand, alloc);
            doc.Accept(writer);
            printf("notify:  %s\n", buffer.GetString());
            m_server.send_data(m_conn, buffer.GetString(), buffer.GetSize());

            doc.RemoveAllMembers();
            buffer.Clear();
            writer.Reset(buffer);
            co_sleep(1);
        }
    });
    co_yield(co_id);
}

int main(int argc, char *argv[])
{
    ws_server ws;
    ws.add_cmd("nd", [&](std::vector<std::string>& args) {
        if (!args.empty())
            ws.notify_data(std::atoi(args[0].c_str()));
    }, "推送指定个数的数据给客户端 usage@ nd 10");
    return ws.run(argc, argv);
}