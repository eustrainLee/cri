# 编译

请先阅读运行，再阅读编译

# 编译virtual-kubelet

首先运行

```shell
docker login
```

登录到docker镜像仓库后，运行

```shell
docker-build
```

构建镜像，在日志中可以看到输出的镜像名，记下来

```shell
make build && mv ./bin ../ && cd ..
```

## 编译 agent
进入agent目录

```shell
g++ -std=c++17 -pthread -Iinclude ./rpc/* agent.cpp -Os -o agent
```


# 准备k8s集群

安装满足CRI规范的容器管理引擎，如containerd

使用kubeadm、minikube、micro等工具创建一个k8s集群

安装`cilium`作为CNI插件(可选)

安装helm3

```shell
bash <(curl -fsSL https://raw.githubusercontent.com/helm/helm/main/scripts/get-helm-3)
```

通过helm3安装部署cilium

```shell
helm repo add cilium https://helm.cilium.io
helm install cilium cilium/cilium -n kube-system --set hubble.relay.enabled=true --set hubble.ui.enabled=true --set operator.replicas=1
```

通过命令`kubectl get pods -n kube-system`查看容器状态，等待`cilium`容器全部启动

# 启动所有agent

在所有的agent上执行如下命令

// TODO: 基于容器启动

```
./agent IP地址 端口号
```

当前所有的hrglet实例都要求在启动的时候agent也在运行

# 创建Namespace

```
kubectl create namespace hrg
```



# 允许任意容器部署到control-plane节点（可选）

## 配置节点

kubectl get nodes查找control-plane的节点
对于每一个要允许部署hrglet的节点

```
kubectl describe 节点名称
```

找到Taints一行，一般是这样的

```
Taints:             node-role.kubernetes.io/master:NoSchedule
```

使用对应的命令清除污点

```
kubectl taint node 节点名称 node-role.kubernetes.io/master-
```

# 获取权限

TODO: 带有对Pod，Service，Secret，ConfigMap完整权限的ServiceAccount

应用如下yaml文件，为Pod配置权限

Rule：

```yaml
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRole
metadata:
  name: hrglet-rule
rules:
- apiGroups: [""]
  resources: ["pods"]
  verbs: ["create", "get", "watch", "list", "update", "patch", "delete", "deletecollection"]
- apiGroups: [""]
  resources: ["node"]
  verbs: ["create", "get", "watch", "list", "update", "patch", "delete", "deletecollection"]
- apiGroups: [""]
  resources: ["secrets"]
  verbs: ["get", "watch", "list"]
- apiGroups: [""]
  resources: ["services"]
  verbs: ["get", "watch", "list"]
- apiGroups: [""]
  resources: ["configmaps"]
  verbs: ["get", "watch", "list"]
```

ServiceAccount：

```yaml
apiVersion: v1
kind: ServiceAccount
metadata:
  name: hrglet-ac
  namespace: hrg
---
apiVersion: rbac.authorization.k8s.io/v1
kind: ClusterRoleBinding
metadata:
  name: hrglet-binding
  namespace: kube-system
subjects:
- kind: ServiceAccount
  name: hrglet-ac
  namespace: hrg
roleRef:
  apiGroup: rbac.authorization.k8s.io
  kind: ClusterRole
  name: hrglet-rule
```

使用`kubectl apply -f XXX.yaml`依次应用以上两个yaml文件



# 创建pod

编写yaml文件，image处要改成上面记录的镜像名，记得前面带上镜像仓库的名称

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: hrglet01
  namespace: hrg
spec:
  serviceAccountName: "hrg/hrglet"
  containers:
  - name: hrglet
    image: eustrain/hrglet:7c65c2c
    ports:
    - containerPort: 10250
    env:
    - name: REMOTE_IP
      value: "116.62.49.91"
    - name: CRI_PORT
      value: "10201"
    - name: AGENT_PORT
      value: "10000"
    - name: APISERVER_CERT_LOCATION
      value: "/etc/kubernetes/pki/ca.crt"
    - name: APISERVER_KEY_LOCATION
      value: "/etc/kubernetes/pki/ca.key"
    - name: KUBELET_PORT
      value: "10250"
    command: ["hrglet"]
    args: [
      "--provider", "hrgcri", "--kubeconfig", "/etc/kubernetes/admin.conf", "--nodename", "hrglet01"
    ]
    volumeMounts:
    - mountPath: "/etc/kubernetes"
      name: k8sconfig
      readOnly: true
  volumes:
    - name: k8sconfig
      hostPath:
        path: "/etc/kubernetes"
  nodeSelector:
    node-role.kubernetes.io/control-plane: ""
  tolerations:
  - key: "node-role.kubernetes.io/master"
    operator: "Exists"
    effect: "NoSchedule"
```

> AGENT_PORT和CRI_PORT都可以写成`ip:port`的形式，此形式下会忽略REMOTE_IP

最后，`kubectl apply -f 文件名.yaml` 就可以了，不过建议使用deployment，然后限制pod数量为1

