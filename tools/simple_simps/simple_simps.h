#include "crx_pch.h"

class simple_simps : public crx::console
{
public:
    simple_simps() = default;
    virtual ~simple_simps() override = default;

public:
    bool init(int argc, char **argv) override;

    void destroy() override {}

private:
    static void on_connect(const crx::server_info& info, void *arg);

    static void on_disconnect(const crx::server_info& info, void *arg);

    static void on_request(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len, void *arg);

    static void on_response(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len, void *arg);

    static void on_notify(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len, void *arg);

private:
    std::shared_ptr<crx::simpack_server> m_server;
};