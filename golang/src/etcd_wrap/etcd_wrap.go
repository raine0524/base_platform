package etcd_wrap

import (
	"context"
	"encoding/json"
	"go.etcd.io/etcd/client"
	"logger"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

type WorkerInfo struct {
	Service string `json:"service"`
	Node    string `json:"node"`
	IP      string `json:"ip"`
	Port    int    `json:"port"`
	Cpu     int    `json:"cpu"`
}

type Worker struct {
	WorkerInfo
	worker_path string

	etcd_api client.KeysAPI
	etcd_cfg client.Config
}

// @node: worker节点名
// @addr: worker工作路径，比如192.168.1.10:1234或是www.example.com:1234
// @path: worker节点的注册路径，比如/test/foo，那么这个节点的service name即为foo
// @endpoints: etcd cluster server的地址
func RegisterWorker(node, addr, path string, endpoints []string) {
	if len(path) > 1 && '/' == path[len(path)-1] {
		path = path[:len(path)-1]
	}
	last_slash := strings.LastIndex(path, "/")
	if -1 == last_slash {
		logger.Fatal("illegal worker path: %v", path)
		return
	}

	var err error
	addr_parts := strings.Split(addr, ":")
	port := 80
	if len(addr_parts) >= 2 {
		port, err = strconv.Atoi(addr_parts[1])
		if nil != err {
			logger.Fatal("illegal worker addr: %v", addr)
			return
		}
	}

	w := &Worker{
		WorkerInfo: WorkerInfo{
			path[last_slash+1:],
			node,
			addr_parts[0],
			port,
			runtime.NumCPU(),
		},
		worker_path: path + "/" + node,

		etcd_cfg: client.Config{
			Endpoints:               endpoints,
			Transport:               client.DefaultTransport,
			HeaderTimeoutPerRequest: time.Second,
		},
	}

	etcd_client, err := client.New(w.etcd_cfg)
	if nil != err {
		logger.Fatal("worker connect to etcd failed: %v", err)
		return
	}
	w.etcd_api = client.NewKeysAPI(etcd_client)

	if w.exist() {
		logger.Fatal("worker=%v has been registered: %v\n", w.worker_path, w.WorkerInfo)
		return
	}
	go w.periodic_heart_beat()
}

func (w *Worker) exist() bool {
	opts := &client.GetOptions{Quorum: true}
	_, err := w.etcd_api.Get(context.Background(), w.worker_path, opts)
	if nil != err {
		etcd_error := err.(client.Error)
		if client.ErrorCodeKeyNotFound != etcd_error.Code {
			logger.Fatal("worker get etcd unexpected response: %v", etcd_error)
		}
		return false
	}
	return true
}

func (w *Worker) periodic_heart_beat() {
	info_str, _ := json.Marshal(w.WorkerInfo)
	for {
		_, err := w.etcd_api.Set(context.Background(), w.worker_path, string(info_str),
			&client.SetOptions{TTL: 5 * time.Second})

		if nil != err {
			logger.Error("worker heartbeat etcd server failed: %v, info: %v", err, w.WorkerInfo)
			etcd_client, _ := client.New(w.etcd_cfg)
			w.etcd_api = client.NewKeysAPI(etcd_client)
		}
		time.Sleep(3 * time.Second)
	}
}

const (
	ES_RoundRobin = 0
)

var (
	g_strategy = ES_RoundRobin
)

type rr_sates struct {
	curr_idx int
}

type sates struct {
	infos          []*WorkerInfo
	strategy_sates interface{}
}

type Master struct {
	map_lock    sync.Mutex
	workers     map[string]*sates
	watch_paths []string

	etcd_api client.KeysAPI
	etcd_cfg client.Config
}

// @endpoints: etcd cluster server的地址
// @path: 所有需要观察的路径
func NewMaster(endpoints []string, paths []string, strategy int) *Master {
	g_strategy = strategy
	master := &Master{
		workers:     make(map[string]*sates, 0),
		watch_paths: paths,

		etcd_cfg: client.Config{
			Endpoints:               endpoints,
			Transport:               client.DefaultTransport,
			HeaderTimeoutPerRequest: time.Second,
		},
	}

	for _, path := range master.watch_paths {
		if '/' == path[len(path)-1] {
			path = path[:len(path)-1]
		}

		last_slash := strings.LastIndex(path, "/")
		service_name := path[last_slash+1:]
		if s, ok := master.workers[service_name]; !ok {
			s = &sates{infos: make([]*WorkerInfo, 0)}
			switch g_strategy {
			case ES_RoundRobin:
				s.strategy_sates = &rr_sates{0}
			}
			master.workers[service_name] = s
		}
	}

	etcd_client, err := client.New(master.etcd_cfg)
	if nil != err {
		logger.Fatal("master connect to etcd failed: %v", err)
	}
	master.etcd_api = client.NewKeysAPI(etcd_client)
	go master.periodic_wait_event()
	return master
}

// 根据service name获取一个可用的worker，当不存在可用的worker时返回""
func (m *Master) GetWorker(service_name string) string {
	m.map_lock.Lock()
	defer m.map_lock.Unlock()

	if workers, ok := m.workers[service_name]; ok {
		if 0 == len(workers.infos) {
			return ""
		}

		index := 0
		switch g_strategy {
		case ES_RoundRobin:
			s := workers.strategy_sates.(*rr_sates)
			s.curr_idx++
			if s.curr_idx >= len(workers.infos) {
				s.curr_idx = 0
			}
			workers.strategy_sates = s
			index = s.curr_idx
		}

		w := workers.infos[index]
		return w.IP + ":" + strconv.Itoa(w.Port)
	} else {
		return ""
	}
}

func (m *Master) periodic_wait_event() {
	var err error
	var res *client.Response

	watchers := make([]client.Watcher, len(m.watch_paths))
	for index, path := range m.watch_paths {
		watchers[index] = m.etcd_api.Watcher(path, &client.WatcherOptions{Recursive: true})
	}

	for {
		for index, watcher := range watchers {
			res, err = watcher.Next(context.Background())
			if nil != err {
				logger.Error("master watch worker path=%v occurs error: %v", m.watch_paths[index], err)
				etcd_client, _ := client.New(m.etcd_cfg)
				m.etcd_api = client.NewKeysAPI(etcd_client)

				for index, path := range m.watch_paths { // watchers失效，所有watcher重新生成
					watchers[index] = m.etcd_api.Watcher(path, &client.WatcherOptions{Recursive: true})
				}

				time.Sleep(1 * time.Second)
				continue
			}

			var value string
			if "expire" == res.Action || "delete" == res.Action {
				value = res.PrevNode.Value
			} else if "set" == res.Action || "update" == res.Action {
				value = res.Node.Value
			}

			// handle event
			info := &WorkerInfo{}
			err = json.Unmarshal([]byte(value), info)
			if nil != err {
				logger.Error("unmarshal master watch value failed: %v, watch path: %v, value: %v",
					err, m.watch_paths[index], value)
				continue
			}

			if "expire" == res.Action || "delete" == res.Action {
				m.delete_worker(info)
			} else if "set" == res.Action || "update" == res.Action {
				m.update_worker(info)
			}
		}
	}
}

func (m *Master) delete_worker(info *WorkerInfo) {
	m.map_lock.Lock()
	defer m.map_lock.Unlock()

	if workers, ok := m.workers[info.Service]; ok {
		for index, w := range workers.infos {
			if w.Node == info.Node {
				workers.infos = append(workers.infos[:index], workers.infos[index+1:]...)
				m.workers[info.Service].infos = workers.infos
				return
			}
		}
	}
}

func (m *Master) update_worker(info *WorkerInfo) {
	m.map_lock.Lock()
	defer m.map_lock.Unlock()

	if workers, ok := m.workers[info.Service]; ok {
		for index, w := range workers.infos {
			if w.Node == info.Node {
				workers.infos[index] = info
				return
			}
		}

		workers.infos = append(workers.infos, info)
		m.workers[info.Service] = workers
	}
}
