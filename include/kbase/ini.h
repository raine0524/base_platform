#pragma once

namespace crx
{
    class CRX_SHARE ini : public kobj
    {
    public:
        ini();
        virtual ~ini() {}

        //加载ini文件
        bool load(const char *file_name);

        //打印当前已加载的ini文件
        void print();

        //将当前内容保存到指定文件中，@file_name参数为空则保存到当前加载的文件中
        void saveas(const char *fname = nullptr);

        //判断是否存在指定区段
        bool has_section(const char *sec_name);

        //设置当前区段
        void set_section(const char *sec_name);

        //创建指定区段 @sec_name-区段名称 @comment-注释，若已存在相关区段则更新其comment
        void create_section(const char *sec_name, const char *comment = nullptr);

        //删除指定区段
        void delete_section(const char *sec_name);

        //判断当前区段中是否存在指定key
        bool has_key(const char *key_name);

        //设置当前区段中指定key的value及其comment，key存在则修改，否则创建
        void set_key(const char *key_name, const char *value, const char *comment = nullptr);

        //删除当前区段中的指定key
        void delete_key(const char *key_name);

        //获取当前区段中指定key的value，并将其转换成相应的类型，若未设置区段或不存在指定key则返回默认值
        std::string get_str(const char *key_name, const char *def = "");

        int get_int(const char *key_name, int def = 0);

        double get_double(const char *key_name, double def = 0.0f);
    };
}
