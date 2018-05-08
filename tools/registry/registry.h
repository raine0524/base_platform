#pragma once

#include "stdafx.h"

class registry : public crx::console
{
public:

public:
    bool init(int argc, char **argv) override;

    void destroy() override;

private:
    crx::seria m_seria;
};
