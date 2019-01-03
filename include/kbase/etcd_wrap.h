#pragma once

namespace crx
{
    enum ETCD_STRATEGY
    {
        ES_ROUNDROBIN = 0,      // 目前只提供了可用节点的轮询策略
    };

    class CRX_SHARE etcd_client : public kobj
    {
    public:
        /*
         * 在etcd节区中配置了watch_paths字段时，可以通过此接口获取可用的节点，参数@svr_name表示path中的最后一部分
         * 例如，若watch path中存在以下路径： /test/example/foo，那么此处获取worker时需要填写的svr_name即为foo
         * 若不存在可用的worker节点，那么endpoint.port的值为0，应用层在取到节点后需要通过port来判断节点是否可用
         */
        endpoint get_worker(const std::string& svr_name);
    };
}