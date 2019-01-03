#pragma once

#if defined(CRX_EXPORT_SYMBOL)
#	if defined(_MSC_VER)
#		define CRX_SHARE __declspec(dllexport)
#	else
#		define CRX_SHARE __attribute__ ((visibility("default")))
#	endif	//_MSC_VER
#else
#	if defined(_MSC_VER)
#		define CRX_SHARE __declspec(dllimport)
#	else
#		define CRX_SHARE
#	endif	//_MSC_VER
#endif	//CRX_EXPORT_SYMBOL

//////////////////////////////////////////////////////////////////////////
//standard/system library macro define

//////////////////////////////////////////////////////////////////////////
//c standard library
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint-gcc.h>
#include <time.h>
#include <wchar.h>
#include <malloc.h>

//////////////////////////////////////////////////////////////////////////
//c++ standard library (mainly include the template library)
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cctype>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <memory>
#include <functional>
#include <future>
#include <regex>

#include <set>
#include <unordered_set>
#include <list>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>
#include <deque>
#include <bitset>
#include <random>
#include <tuple>

//////////////////////////////////////////////////////////////////////////
//platform(linux) specific library
#include <zlib.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <iconv.h>
#include <ifaddrs.h>
#include <sched.h>
#include <syscall.h>
#include <syslog.h>
#include <ucontext.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/sysinfo.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <execinfo.h>

//////////////////////////////////////////////////////////////////////////
//third-party open source library
#include <openssl/md5.h>
#include <openssl/sha.h>

//rapidjson
#include "rapidjson/encodings.h"
#include "rapidjson/document.h"
#include "rapidjson/reader.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"

//googletest
#include "gtest/gtest.h"
#include "gmock/gmock.h"

//mysql
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/exception.h>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/statement.h>

using namespace std::placeholders;
using namespace rapidjson;

extern std::string g_server_name;

namespace crx
{
    static const long int nano_per_sec = 1000*1000*1000;

    enum CRX_SHARE ADDR_TYPE
    {
        ADDR_MAC = 0,
        ADDR_IP,
    };

    class CRX_SHARE impl
    {
    public:
        impl() = default;
        virtual ~impl() = default;

    private:
        impl(const impl&) = delete;
        impl& operator=(const impl&) = delete;
    };

    class CRX_SHARE kobj
    {
    public:
        kobj() = default;
        virtual ~kobj() = default;
        std::shared_ptr<impl> m_impl;
    };

    struct CRX_SHARE datetime   //日期时间
    {
        unsigned int date;		//format: YYYYMMDD
        unsigned int time;		//format: HHMMSSmmm，最后三位为毫秒

        struct tm *t;
        int64_t time_stamp;    //当前时间戳，精确到毫秒

        datetime() : date(0), time(0), time_stamp(0) {}
    };

    struct CRX_SHARE mem_ref
    {
        const char *data;
        size_t len;

        mem_ref() { bzero(this, sizeof(mem_ref)); }
        mem_ref(const char *s, size_t l) : data(s), len(l) {}
    };

    struct CRX_SHARE endpoint
    {
        char ip_addr[16];
        int port;

        endpoint() : port(0) { bzero(ip_addr, sizeof(ip_addr)); }
    };
}

//////////////////////////////////////////////////////////////////////////
//kbase library (self custome interface)
#include "kbase/base_util.h"
#include "kbase/ini.h"
#include "kbase/logger.h"
#include "kbase/schutil.h"
#include "kbase/tcp_proto.h"
#include "kbase/http_proto.h"
#include "kbase/etcd_wrap.h"
#include "kbase/scheduler.h"
#include "kbase/console.h"
