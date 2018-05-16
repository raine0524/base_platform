#include "stdafx.h"

namespace crx
{
    struct ini_item
    {
        bool valid;
        const char *key;
        std::string value;
        std::string comment;

        ini_item() : valid(true), key(nullptr) {}
    };

    struct ini_section
    {
        bool valid;
        const char *name;
        std::string comment;
        std::vector<ini_item> items;

        //it->first: key, it->second: index of items
        std::unordered_map<std::string, size_t> item_map;

        ini_section() : valid(true) {}
    };

    class ini_impl : public impl
    {
    public:
        ini_impl() : m_com_flag('#'), m_sec_idx(-1) {}

        size_t parse_line(size_t sec_idx, std::string& line);

        void dump(FILE *fp, bool display_label);

        const char* get_key(const char *key_name);

        char m_com_flag;
        std::string m_file_name;

        int m_sec_idx;
        std::vector<ini_section> m_sections;

        //it->first: key, it->second: index of sections
        std::unordered_map<std::string, size_t> m_sec_map;
    };

    ini::ini()
    {
        m_impl = std::make_shared<ini_impl>();
    }

    bool ini::load(const char *file_name)
    {
        if (!file_name)
            return false;

        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        impl->m_sections.clear();
        impl->m_file_name = file_name;

        FILE *fp = fopen(file_name, "r");
        if (!fp)
            return false;

        //创建一个默认的section，存放文件开始处没有任何section的行
        const char *def_sec = "__default__";
        size_t sec_idx = impl->m_sections.size();
        impl->m_sec_map[def_sec] = sec_idx;
        impl->m_sections.emplace_back();

        auto& sec = impl->m_sections[sec_idx];
        sec.name = impl->m_sec_map.find(def_sec)->first.c_str();

        std::string line(1024, 0);
        while (fgets(&line[0], (int)(line.size()-1), fp)) {
            std::string temp = line.substr(0, strlen(line.data())-1);   //去掉最后一个换行符
            trim(temp);
            if (temp.empty())
                continue;

            sec_idx = impl->parse_line(sec_idx, temp);
            bzero(&line[0], line.size());
        }

        fclose(fp);
        return true;
    }

    size_t ini_impl::parse_line(size_t sec_idx, std::string& line)
    {
        line.push_back(0);
        if ('[' == line.front()) {      //new section
            const char *pos = strchr(line.data()+1, ']');
            if (!pos) {
                printf("invalid section: %s\n", line.c_str());
                return sec_idx;
            }

            if (strchr(pos+1, ']')) {
                printf("invalid section: %s\n", line.c_str());
                return sec_idx;
            }

            std::string sec_name = line.substr(1, pos-line.data()-1);
            if (m_sec_map.end() != m_sec_map.find(sec_name))
                return sec_idx;     //该section已存在，不再创建新的section

            size_t nsec_idx = m_sections.size();
            m_sec_map[sec_name] = nsec_idx;
            m_sections.emplace_back();

            auto& nsec = m_sections[nsec_idx];
            nsec.name = m_sec_map.find(sec_name)->first.c_str();
            pos = strchr(pos+1, m_com_flag);
            if (pos)
                nsec.comment = pos;
            return nsec_idx;
        } else {        //new line
            auto& sec = m_sections[sec_idx];
            size_t line_idx = sec.items.size();
            sec.items.emplace_back();
            auto& item = sec.items[line_idx];

            const char *flag = strchr(line.data(), m_com_flag);
            //在该行只有注释没有kv的情形中，若在注释符前出现任意字符皆为未定义行为，此处简单丢弃这些字符串
            if (flag)       //有注释以第一个出现的注释符为基准，此后皆为该行注释
                item.comment = flag;
            else
                flag = &line.back();

            //有kv以第一个出现的'='为基准，该符号之前为key，之后为value
            const char *equal = strchr(line.data(), '=');
            if (equal && equal < flag) {    //当equal >= flag时该kv已被注释
                std::string key = line.substr(0, equal-line.data());
                trim(key);
                if (sec.item_map.end() != sec.item_map.find(key)) {
                    sec.items.pop_back();       //主键已存在，丢弃该行
                    return sec_idx;
                }

                sec.item_map[key] = line_idx;
                item.key = sec.item_map.find(key)->first.c_str();
                item.value = line.substr(equal-line.data()+1, flag-equal-1);
                trim(item.value);
            }

            if (!equal && flag == &line.back())     //没有kv且没有注释，表明这是一个无效的行
                sec.items.pop_back();
            return sec_idx;
        }
    }

    void ini::print()
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        impl->dump(stdout, true);
    }

    void ini::saveas(const char *fname /*= nullptr*/)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        if (!fname && impl->m_file_name.empty())
            return;

        if (!fname)
            fname = impl->m_file_name.c_str();

        FILE *fp = fopen(fname, "w");
        impl->dump(fp, false);
        fclose(fp);
    }

    void ini_impl::dump(FILE *fp, bool display_label)
    {
        for (size_t i = 0; i < m_sections.size(); ++i) {
            auto& sec = m_sections[i];
            if (!sec.valid)
                continue;

            if (i != 0) {
                if (display_label)
                    fprintf(fp, "[(s_s)%s(s_e)]\t(c_s)%s(c_e)\n", sec.name, sec.comment.c_str());
                else
                    fprintf(fp, "[%s]\t%s\n", sec.name, sec.comment.c_str());
            }

            for (size_t j = 0; j < sec.items.size(); ++j) {
                auto& item = sec.items[j];
                if(item.valid) {
                    if (item.key) {
                        if (display_label)
                            fprintf(fp, "\t(k_s)%s(k_e) = (v_s)%s(v_e)\t(c_s)%s(c_e)\n", item.key, item.value.c_str(), item.comment.c_str());
                        else
                            fprintf(fp, "\t%s = %s\t%s\n", item.key, item.value.c_str(), item.comment.c_str());
                    } else {
                        if (display_label)
                            fprintf(fp, "\t(c_s)%s(c_e)\n", item.comment.c_str());
                        else
                            fprintf(fp, "\t%s\n", item.comment.c_str());
                    }
                }
            }
            fputc('\n', fp);
        }
    }

    bool ini::has_section(const char *sec_name)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        return impl->m_sec_map.end() != impl->m_sec_map.find(sec_name);
    }

    void ini::set_section(const char *sec_name)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        auto it = impl->m_sec_map.find(sec_name);
        if (impl->m_sec_map.end() != it && impl->m_sections[it->second].valid)
            impl->m_sec_idx = (int)it->second;
        else
            impl->m_sec_idx = -1;
    }

    void ini::create_section(const char *sec_name, const char *comment /*= nullptr*/)
    {
        if (!sec_name)
            return;

        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        auto it = impl->m_sec_map.find(sec_name);
        if (impl->m_sec_map.end() != it) {      //已存在待创建的区段
            if (!comment)
                impl->m_sections[it->second].comment = comment;
            return;
        }

        auto sec_idx = impl->m_sections.size();
        impl->m_sections.emplace_back();
        impl->m_sec_map[sec_name] = sec_idx;

        auto& sec = impl->m_sections.back();
        sec.name = impl->m_sec_map.find(sec_name)->first.c_str();
        if (!comment)
            sec.comment = comment;
    }

    void ini::delete_section(const char *sec_name)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        auto it = impl->m_sec_map.find(sec_name);
        if (impl->m_sec_map.end() != it) {
            impl->m_sections[it->second].valid = false;
            if (impl->m_sec_idx == it->second)
                impl->m_sec_idx = -1;
            impl->m_sec_map.erase(it);
        }
    }

    bool ini::has_key(const char *key_name)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        if (-1 == impl->m_sec_idx)
            return false;

        auto& sec = impl->m_sections[impl->m_sec_idx];
        auto it = sec.item_map.find(key_name);
        if (sec.item_map.end() != it)
            return sec.items[it->second].valid;
        else
            return false;
    }

    void ini::set_key(const char *key_name, const char *value, const char *comment /*= nullptr*/)
    {
        if (!key_name || !value)
            return;

        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        if (-1 == impl->m_sec_idx)
            return;

        auto& sec = impl->m_sections[impl->m_sec_idx];
        auto it = sec.item_map.find(key_name);
        if (sec.item_map.end() != it) {     //已有指定的key
            sec.items[it->second].value = value;
            if (comment)
                sec.items[it->second].comment = comment;
        } else {    //创建新的key
            auto line_idx = sec.items.size();
            sec.items.emplace_back();
            sec.item_map[key_name] = line_idx;

            auto& item = sec.items[line_idx];
            item.key = sec.item_map.find(key_name)->first.c_str();
            item.value = value;
            if (comment)
                item.comment = comment;
        }
    }

    void ini::delete_key(const char *key_name)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        if (-1 == impl->m_sec_idx)
            return;

        auto& sec = impl->m_sections[impl->m_sec_idx];
        auto it = sec.item_map.find(key_name);
        if (sec.item_map.end() != it) {
            sec.items[it->second].valid = false;
            sec.item_map.erase(it);
        }
    }

    const char* ini_impl::get_key(const char *key_name)
    {
        if (-1 == m_sec_idx)
            return nullptr;

        auto& sec = m_sections[m_sec_idx];
        auto it = sec.item_map.find(key_name);
        if (sec.item_map.end() != it)
            return sec.items[it->second].value.c_str();
        else
            return nullptr;
    }

    std::string ini::get_str(const char *key_name, const char *def /*= ""*/)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        const char *value = impl->get_key(key_name);
        return (value ? value : std::string(def));
    }

    int ini::get_int(const char *key_name, int def /*= 0*/)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        const char *value = impl->get_key(key_name);
        return (value ? atoi(value) : def);
    }

    double ini::get_double(const char *key_name, double def /*= 0.0f*/)
    {
        auto impl = std::dynamic_pointer_cast<ini_impl>(m_impl);
        const char *value = impl->get_key(key_name);
        return (value ? atof(value) : def);
    }
}