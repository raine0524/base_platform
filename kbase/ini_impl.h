#pragma once

#include "crx_pch.h"

namespace crx
{
    struct ini_item
    {
        std::string key;
        std::string value;
        std::string comment;
    };

    struct ini_section
    {
        std::string name;
        std::string comment;
        std::vector<ini_item> items;
    };

    class ini_impl
    {
    public:
        ini_impl() : m_com_flag('#') {}
        virtual ~ini_impl() {}

        char m_com_flag;
        std::string m_file_name;
        std::unordered_map<std::string, ini_section> m_sections;
    };
}
