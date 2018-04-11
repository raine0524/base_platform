#include "stdafx.h"

namespace crx
{
    ini::ini()
    {
        m_obj = new ini_impl;
    }

    ini::~ini()
    {
        delete (ini_impl*)m_obj;
    }

    bool ini::load(const char *file_name)
    {
        auto impl = (ini_impl*)m_obj;
        impl->m_sections.clear();
        impl->m_file_name = file_name;

        FILE *fp = fopen(file_name, "r");
        if (!fp)
            return false;

        std::string line(1024, 0), comment;
        while (fgets(&line[0], line.size()-1, fp)) {
            size_t len = strlen(line.data());
            if (1 == len)       //只有一个换行符
                continue;

            line.resize(len-1);     //去掉最后一个换行符'\n'
            trim(line);

            line.resize(1024);
            bzero(&line[0], line.size());
        }

        fclose(fp);
        return false;
    }
}