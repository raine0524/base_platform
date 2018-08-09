#include "stdafx.h"

namespace crx
{
    fs_monitor scheduler::get_fs_monitor(std::function<void(const char*, uint32_t)> f)
    {
        auto sch_impl = std::dynamic_pointer_cast<scheduler_impl>(m_impl);
        auto& impl = sch_impl->m_util_impls[FS_MONI];
        if (!impl) {
            auto moni_impl = std::make_shared<fs_monitor_impl>();
            moni_impl->fd = inotify_init1(IN_NONBLOCK);
            if (__glibc_likely(moni_impl->fd > 0)) {
                moni_impl->sch_impl = sch_impl;
                moni_impl->f = std::bind(&fs_monitor_impl::fs_monitory_callback, moni_impl.get(), _1);
                moni_impl->m_monitor_f = std::move(f);
                sch_impl->add_event(moni_impl);
                impl = moni_impl;
            } else {
                log_error(g_lib_log, "inotify_init1 failed: %s\n", strerror(errno));
            }
        }

        fs_monitor obj;
        obj.m_impl = impl;
        return obj;
    }

    void fs_monitor::add_watch(const char *path, uint32_t mask /*= IN_CREATE | IN_DELETE | IN_MODIFY*/, bool recursive /*= true*/)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<fs_monitor_impl>(m_impl);
        bzero(&impl->m_st, sizeof(impl->m_st));
        stat(path, &impl->m_st);
        bool is_dir = S_ISDIR(impl->m_st.st_mode);

        std::string monitor_path = path;
        if (is_dir && '/' != monitor_path.back())      //添加监控的是目录
            monitor_path.push_back('/');

        if (impl->m_path_mev.end() != impl->m_path_mev.find(path))
            return;

        int watch_id = inotify_add_watch(impl->fd, monitor_path.c_str(), mask);
        if (__glibc_unlikely(-1 == watch_id)) {
            log_error(g_lib_log, "inotify_add_watch(%d) path=%s failed: %s\n", impl->fd,
                    monitor_path.c_str(), strerror(errno));
            return;
        }

        impl->trigger_event(true, watch_id, monitor_path, recursive, mask);
        if (is_dir && recursive)       //对子目录递归监控
            impl->recursive_monitor(path, true, mask);
    }

    void fs_monitor::rm_watch(const char *path, bool recursive /*= true*/)
    {
        if (!m_impl) return;
        auto impl = std::dynamic_pointer_cast<fs_monitor_impl>(m_impl);
        bzero(&impl->m_st, sizeof(impl->m_st));
        stat(path, &impl->m_st);
        bool is_dir = S_ISDIR(impl->m_st.st_mode);

        std::string monitor_path = path;
        if (is_dir && '/' != monitor_path.back())  //移除监控的是目录
            monitor_path.push_back('/');

        if (impl->m_path_mev.end() == impl->m_path_mev.find(path))
            return;

        impl->trigger_event(false, impl->m_path_mev[path]->watch_id, path, 0, 0);
        if (is_dir && recursive)       //对子目录递归移除
            impl->recursive_monitor(path, false, 0);
    }

    void fs_monitor_impl::fs_monitory_callback(uint32_t events)
    {
        int sts = async_read(fd, m_nfy_buf);
        if (sts <= 0)
            return;

        const char *start = m_nfy_buf.data(), *end = &m_nfy_buf.back()+1;
        while (true) {
            int total_len = (int)(end-start);
            if (total_len < sizeof(inotify_event))
                return;      //read more

            auto nfy_ev = (inotify_event*)start;
            if (total_len < nfy_ev->len+sizeof(inotify_event))
                return;      //read more

            size_t valid_len = sizeof(inotify_event)+nfy_ev->len;
            if (m_wd_mev.end() == m_wd_mev.find(nfy_ev->wd)) {
                start += valid_len;
                continue;
            }

            auto& mev = m_wd_mev[nfy_ev->wd];
            std::string file_name = mev->path;
            if (nfy_ev->len)
                file_name += nfy_ev->name;

            if ((nfy_ev->mask & IN_ISDIR) && mev->recur_flag) {      //directory
                if ('/' != file_name.back())
                    file_name.push_back('/');

                auto mev_it = m_path_mev.find(file_name);
                if ((nfy_ev->mask & IN_CREATE) && m_path_mev.end() == mev_it) {
                    int watch_id = inotify_add_watch(fd, file_name.c_str(), mev->mask);
                    if (__glibc_likely(-1 != watch_id))
                        trigger_event(true, watch_id, file_name, mev->recur_flag, mev->mask);
                } else if ((nfy_ev->mask & IN_DELETE) && m_path_mev.end() != mev_it) {
                    trigger_event(false, m_path_mev[file_name]->watch_id, file_name, 0, 0);
                }
            }
            m_monitor_f(file_name.c_str(), nfy_ev->mask);
            start += valid_len;
        }
    }

    void fs_monitor_impl::recursive_monitor(const std::string& root_dir, bool add, uint32_t mask)
    {
        depth_first_traverse_dir(root_dir.c_str(), [&](std::string& dir_name) {
            struct stat st = {0};
            stat(dir_name.c_str(), &st);
            if (!S_ISDIR(st.st_mode))       //仅对目录做处理
                return;

            if ('/' != dir_name.back())
                dir_name.push_back('/');

            auto mev_it = m_path_mev.find(dir_name);
            if (add && m_path_mev.end() == mev_it) {
                int watch_id = inotify_add_watch(fd, dir_name.c_str(), mask);
                if (__glibc_unlikely(-1 == watch_id)) {
                    log_error(g_lib_log, "inotify_add_watch failed: %s\n", strerror(errno));
                    return;
                }
                trigger_event(true, watch_id, dir_name, true, mask);
                return;
            }

            if (!add && m_path_mev.end() != mev_it)     //remove
                trigger_event(false, m_path_mev[dir_name]->watch_id, dir_name, 0, 0);
        }, true, false);
    }

    void fs_monitor_impl::trigger_event(bool add, int watch_id, const std::string& path, bool recur_flag, uint32_t mask)
    {
        if (add) {   //add watch
            auto mev = std::make_shared<monitor_ev>();
            mev->recur_flag = recur_flag;
            mev->watch_id = watch_id;
            mev->mask = mask;
            mev->path = path;

            m_path_mev[path] = mev;
            m_wd_mev[watch_id] = std::move(mev);
        } else {    //remove watch
            inotify_rm_watch(fd, watch_id);
            m_wd_mev.erase(watch_id);
            m_path_mev.erase(path);
        }
    }
}