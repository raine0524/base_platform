#include "crx_pch.h"

class simple_crawler : public crx::console
{
public:
    virtual bool init(int argc, char *argv[]);

    virtual void destroy(){}

    void get_web_page(std::vector<std::string>& urls);

private:
    crx::http_client m_http_client;
};

void simple_crawler::get_web_page(std::vector<std::string>& urls)
{
    for (auto& url : urls) {
        size_t co_id = co_create([&](crx::scheduler *sch) {
            int conn = m_http_client.connect(url.c_str(), 80);
            m_http_client.GET(conn, "/", nullptr);
            }, true);

        printf("url: %s, co_id: %lu\n", url.c_str(), co_id);
        co_yield(co_id);
    }
}

bool simple_crawler::init(int argc, char *argv[])
{
    m_http_client = get_http_client([this](int conn, int status, std::unordered_map<std::string, const char*>& header_kvs,
            const char* data, size_t len) {
        printf("\nresponse: %d %d\n\n", conn, status);
        if (data)
            printf("%s\n", data);
        m_http_client.release(conn);
    });
    return true;
}

int main(int argc, char *argv[])
{
    simple_crawler crawler;
    crawler.add_cmd("get", [&](std::vector<std::string>& args) {
        crawler.get_web_page(args);
    }, "抓取指定网页");
    crawler.run(argc, argv);
}
