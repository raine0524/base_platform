#include "stdafx.h"

namespace crx
{
    const char *comp_flag = "__yzeiaph@^$^@daniel__";
    const char *alg_zip_sig = "__zip__";
    const char *alg_lzma_sig = "__lzma__";

    class seria_impl
    {
    public:
        seria_impl(seria *s)
                :m_seria(s)
                ,m_packer(&m_pack_buf) {}

        void zip();			//按照zip格式对原始数据进行压缩
        void lzma();		//lzma格式的压缩

        std::string unzip();				//按照zip格式解压缩
        std::string unlzma();			//lzma格式的解压缩
        void deseria(const char *data, int len);		//反序列化操作

    private:
        //构造压缩形式的序列化字符串
        void cc_seria(const char *alg_sig, size_t org_len, std::string& tmp_buf);

    public:
        seria *m_seria;
        msgpack::sbuffer m_pack_buf;
        msgpack::packer<msgpack::sbuffer> m_packer;
        std::unordered_map<std::string, std::string> m_dump_map;
    };

    seria::seria()
    {
        seria_impl *impl = new seria_impl(this);
        /**
         * 在序列化串中预留5个字节的空间，第一个字节为int32类型在msgpack中对应的标志，后面的4个字节
         * 用于指明当前整个序列化串的大小
         */
        impl->m_packer.pack_fix_int32(0);
        m_obj = impl;
    }

    seria::~seria()
    {
        delete (seria_impl*)m_obj;
    }

    void seria::reset()
    {
        seria_impl *impl = (seria_impl*)m_obj;
        //在执行重置操作时除将缓冲区pack_buf清空之外，同样需要预留5个字节的空间
        impl->m_pack_buf.clear();
        impl->m_packer.pack_fix_int32(0);
    }

    void seria::insert(const char *key, const char *data, size_t len)
    {
        seria_impl *impl = (seria_impl*)m_obj;
        msgpack::type::raw_ref raw(data, len);
        std::pair<const char*, msgpack::type::raw_ref> pair(key, raw);
        impl->m_packer.pack(pair);		//将键值对加入序列化串中

        int32_t *hk_sz = (int32_t*)(impl->m_pack_buf.data()+1);
        *hk_sz = impl->m_pack_buf.size();		//更新当前字符串的大小
    }

    std::string seria::get_string(COMP_OPT opt /*= COMP_NONE*/)
    {
        seria_impl *impl = (seria_impl*)m_obj;
        if (crx::COMP_ZIP == opt)		//按照指定的压缩格式对原始序列化串进行压缩
            impl->zip();
        else if (COMP_LZMA == opt)
            impl->lzma();
        //** 构造序列化串，此处将发生拷贝操作，影响性能
        return std::string(impl->m_pack_buf.data(), impl->m_pack_buf.size());
    }

    void seria_impl::zip()
    {
        size_t org_len = m_pack_buf.size();
        size_t dst_len = compressBound(org_len);		//根据原始大小计算压缩之后整个字符串的大小
        std::string tmp_buf(dst_len, 0);
        //压缩操作，若不成功则直接返回原始序列化串
        if (Z_OK != compress((Bytef*)&tmp_buf[0], &dst_len, (const Bytef*)m_pack_buf.data(), org_len))
            return;
        tmp_buf.resize(dst_len);
        //压缩完成之后构造一个二级的序列化串
        cc_seria(alg_zip_sig, org_len, tmp_buf);
    }

    void seria_impl::lzma()
    {
        lzma_stream strm = LZMA_STREAM_INIT;
        assert(LZMA_OK == lzma_easy_encoder(&strm, 1, LZMA_CHECK_CRC64));
        size_t org_len = m_pack_buf.size();
        std::string tmp_buf(org_len, 0);		//压缩之后的数据量不会大于原有数据量
        strm.next_in = (const uint8_t*)m_pack_buf.data();		//指向原始的字符串
        strm.avail_in = org_len;		//原始字符串的大小
        strm.next_out = (uint8_t*)&tmp_buf.front();		//指向存放压缩字符串的缓冲区
        strm.avail_out = tmp_buf.size();		//该缓冲区的剩余可用空间
        lzma_code(&strm, LZMA_FINISH);			//压缩操作
        tmp_buf.resize(org_len-strm.avail_out);		//更新压缩缓冲区的大小
        lzma_end(&strm);
        //压缩完成之后构造一个二级的序列化串
        cc_seria(alg_lzma_sig, org_len, tmp_buf);
    }

    std::string seria_impl::unzip()
    {
        size_t dst_len = *(size_t*)m_dump_map["__org_len__"].data();		//获取原始字符串的大小
        std::string buf(dst_len, 0);
        //解压缩操作
        assert(Z_OK == uncompress((Bytef*)&buf.front(), &dst_len,
                                  (const Bytef*)m_dump_map["__data__"].data(), m_dump_map["__data__"].size()));
        return buf;
    }

    std::string seria_impl::unlzma()
    {
        lzma_stream strm = LZMA_STREAM_INIT;
        assert(LZMA_OK == lzma_stream_decoder(&strm, UINT64_MAX,
                                              LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED));

        size_t dst_len = *(size_t*)m_dump_map["__org_len__"].data();		//获取原始字符串的大小
        std::string buf(dst_len, 0);
        strm.next_in = (const uint8_t*)m_dump_map["__data__"].data();		//指向原始字符串的缓冲区
        strm.avail_in = m_dump_map["__data__"].size();		//原始字符串的大小
        strm.next_out = (uint8_t*)buf.data();		//指向存放解压缩后的字符串的缓冲区
        strm.avail_out = buf.size();			//解压缩缓冲区的大小
        lzma_code(&strm, LZMA_FINISH);		//执行解压缩操作
        assert(!strm.avail_out);
        lzma_end(&strm);
        return buf;
    }

    void seria_impl::cc_seria(const char *alg_sig, size_t org_len, std::string& tmp_buf)
    {
        m_seria->reset();
        m_seria->insert("__comp__", comp_flag, strlen(comp_flag));			//指明当前字符串是一个压缩字符串
        m_seria->insert("__alg__", alg_sig, strlen(alg_sig));							//指明压缩所使用的算法
        m_seria->insert("__org_len__", (const char*)&org_len, sizeof(size_t));		//原始字符串的长度
        m_seria->insert("__data__", tmp_buf.data(), tmp_buf.size());			//指向压缩之后的字符串
    }

    void seria_impl::deseria(const char *data, int len)
    {
        size_t off = 0;
        bool fetch_size = false;
        while (off != len) {
            msgpack::unpacked up;
            msgpack::unpack(up, data, len, off);
            if (!fetch_size) {		//该string的第一个块存放的是整个字符串的长度，将其过滤
                fetch_size = true;
                continue;
            }

            //从序列化串中构造相应的键值对
            std::pair<std::string, msgpack::type::raw_ref> pair = up.get().convert();
            m_dump_map[pair.first] = std::string(pair.second.ptr, pair.second.size);
        }
    }

    std::unordered_map<std::string, std::string> seria::dump(const char *data, int len)
    {
        seria_impl *impl = static_cast<seria_impl*>(m_obj);
        if (!impl->m_dump_map.empty())
            impl->m_dump_map.clear();
        if (static_cast<char>(0xd2u) != *data) {		//验证第一个字节的魔数是否等于0xd2u
            printf("[seria::dump] 当前数据流的验证码 0x%x 出错，不再继续执行反序列化\n", *data);
            return impl->m_dump_map;
        }

        int hook_size = *(int*)(data+1);		//获取序列化串的大小
        if (hook_size != len) {		//验证数据流长度是否和hook_size相等
            printf("[seria::dump] 当前数据流中记录的长度 %d 与整个数据流的长度 %d不等，不再反序列化\n", hook_size, len);
            return impl->m_dump_map;
        }

        impl->deseria(data, len);		//反序列化
        if (impl->m_dump_map.end() != impl->m_dump_map.find("__comp__") &&
            !strncmp(comp_flag, impl->m_dump_map["__comp__"].data(), impl->m_dump_map["__comp__"].size())) {
            /**
             * 查找压缩标志并验证签名，确认启用了压缩，验证签名是为了防止真实数据中同样含有"__comp__" key
             * 但是仍有可能在真实数据中与该签名同样的value，不过出现这种状况的概率较小
             */
            std::string buf;
            //首先执行解压缩操作获取原始的序列化串
            if (!strncmp(alg_zip_sig, impl->m_dump_map["__alg__"].data(), impl->m_dump_map["__alg__"].size()))
                buf = impl->unzip();
            else if (!strncmp(alg_lzma_sig, impl->m_dump_map["__alg__"].data(), impl->m_dump_map["__alg__"].size()))
                buf = impl->unlzma();
            //对解压缩之后的原始序列化串再做一次反序列化操作
            impl->m_dump_map.clear();
            impl->deseria(buf.data(), buf.size());
        }
        return impl->m_dump_map;
    }
}
