# 编译

请先阅读运行，再阅读编译

# 编译virtual-kubelet
root权限下在cri目录下执行make build，并将编译后的bin目录移到别处

```shell
make build && mv ./bin ../ && cd ..
```

## 编译 agent
进入agent目录

```shell
g++ -std=c++17 -pthread -Iinclude ./rpc/* agent.cpp -Os -o agent
```

# 运行
## 运行agent
在liteos_a或linux主机编译agent，然后在root权限下执行
```shell
./agent 这个主机的IP 这个agent监听的PORT
```
## 运行virtual-kubelet
在k8s的master节点编译`virtual-kubelet`，在/bin的同级目录下创建文件run.sh
```shell
#! /bin/sh
export KUBERNETES_SERVICE_HOST="192.168.30.94" # master节点的IP地址, 也就是这个主机的IP地址
export KUBERNETES_SERVICE_PORT="6443" # API Server监听的端口，默认6443
export VKUBELET_POD_IP="192.168.30.95" # worker节点的IP地址，同时也是agent监听的IP地址
export REMOTE_IP="192.168.30.95" #和VKUBELET_POD_IP一样
export AGENT_PORT="40002" #agent监听的port
export CRI_PORT="10350" # CRI监听的端口号
export APISERVER_CERT_LOCATION="/etc/kubernetes/pki/client.crt" # 需要一个apiserver承认的.crt文件和一个.key文件，以用于认证
#详见https://kubernetes.io/docs/setup/best-practices/certificates/
export APISERVER_KEY_LOCATION="/etc/kubernetes/pki/client.key"
export KUBELET_PORT="10250" # 作为kubelet的端口号，这个一般不用改
# 启动 --nodename是在k8s集群中的节点名
cd bin
./virtual-kubelet --provider cri --kubeconfig /etc/kubernetes/admin.conf --nodename vk01
```

如果virtual-kubelet正常启动，那么agent应该会打印一条查询内存信息的日志
