#pragma once

#include "stdafx.h"

class log_server : public crx::console
{
public:
    log_server() = default;
    virtual ~log_server() override = default;

public:
    bool init(int argc, char **argv) override;

    void destroy() override {}

private:
    void on_connect(const crx::server_info& info);

    void on_disconnect(const crx::server_info& info);

    void on_request(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len);

    void on_response(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len) {}

    void on_notify(const crx::server_info& info, const crx::server_cmd& cmd, char *data, size_t len);

private:
    crx::seria m_seria;
    std::shared_ptr<crx::simpack_server> m_server;
};