package cri

import (
	"context"
	"fmt"
	"io"

	// "io/ioutil"
	"net"
	"path"
	"path/filepath"
	"strings"
	"time"

	"github.com/pkg/errors"
	"github.com/virtual-kubelet/node-cli/manager"
	"github.com/virtual-kubelet/virtual-kubelet/errdefs"
	"github.com/virtual-kubelet/virtual-kubelet/log"
	"github.com/virtual-kubelet/virtual-kubelet/node/api"
	"github.com/virtual-kubelet/virtual-kubelet/trace"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	v1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	types "k8s.io/apimachinery/pkg/types"
	criapi "k8s.io/cri-api/pkg/apis/runtime/v1"
	"k8s.io/klog"
)

// TODO: Make these configurable
// const CriSocketPath = "/run/containerd/containerd.sock"
const CriSocketPath = "unix:///run/containerd/containerd.sock"
const PodLogRoot = "/var/log/vk-cri/"
const PodVolRoot = "/run/vk-cri/volumes/"
const PodLogRootPerms = 0755
const PodVolRootPerms = 0755
const PodVolPerms = 0755
const PodSecretVolPerms = 0755
const PodSecretVolDir = "/secrets"
const PodSecretFilePerms = 0644
const PodConfigMapVolPerms = 0755
const PodConfigMapVolDir = "/configmaps"
const PodConfigMapFilePerms = 0644

var CriPort = ":10350"
var AgentPort = ":40002"

// Provider implements the virtual-kubelet provider interface and manages pods in a CRI runtime
// NOTE: Provider is not inteded as an alternative to Kubelet, rather it's intended for testing and POC purposes
//
//	As such, it is far from functionally complete and never will be. It provides the minimum function necessary
type Provider struct {
	// Discard resourceManager
	resourceManager    *manager.ResourceManager
	podLogRoot         string
	podVolRoot         string
	nodeName           string
	operatingSystem    string
	internalIP         string
	nodeIP             string
	daemonEndpointPort int32
	// podStatus          map[types.UID]CRIPod // Indexed by Pod Spec UID
	holdingPod    *CRIPod
	runtimeClient criapi.RuntimeServiceClient
	imageClient   criapi.ImageServiceClient
	notifyStatus  func(*v1.Pod)

	// workaround
	// cachedPods map[CachedPodKey]CachedPod
}

type CRIPod struct {
	id         string                             // This is the CRI Pod ID, not the UID from the Pod Spec
	containers map[string]*criapi.ContainerStatus // ContainerStatus is a superset of Container, so no need to store both
	status     *criapi.PodSandboxStatus           // PodStatus is a superset of PodSandbox, so no need to store both
}

// Build an internal representation of the state of the pods and containers on the node
// Call this at the start of every function that needs to read any pod or container state
func (p *Provider) refreshNodeState(ctx context.Context) (retErr error) {
	ctx, span := trace.StartSpan(ctx, "cri.refreshNodeState")
	defer span.End()
	defer func() {
		span.SetStatus(retErr)
	}()

	allPods, err := getPodSandboxes(ctx, p.runtimeClient)
	if err != nil {
		return err
	}

	pod := allPods[0]
	if len(allPods) > 1 {
		klog.Warningf("The number of pods is %d and more than one", len(allPods))
	}
	psId := pod.Id

	pss, err := getPodSandboxStatus(ctx, p.runtimeClient, psId)
	if err != nil {
		return err
	}
	containers, err := getContainersForSandbox(ctx, p.runtimeClient, psId)
	if err != nil {
		return err
	}
	if containers == nil {
		containers = make([]*criapi.Container, 0)
	}

	var css = make(map[string]*criapi.ContainerStatus)
	for _, c := range containers {
		cstatus, err := getContainerCRIStatus(ctx, p.runtimeClient, c.Id)
		if err != nil {
			return err
		}
		css[cstatus.Metadata.Name] = cstatus
	}

	newPod := &CRIPod{
		id:         pod.Id,
		status:     pss,
		containers: css,
	}
	p.holdingPod = newPod

	return nil
}

// Initialize the CRI APIs required
func getClientAPIs(criEndpoint string) (criapi.RuntimeServiceClient, criapi.ImageServiceClient, error) {
	// Set up a connection to the server.
	conn, err := getClientConnection(criEndpoint)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to connect: %v", err)
	}
	rc := criapi.NewRuntimeServiceClient(conn)
	if rc == nil {
		return nil, nil, fmt.Errorf("failed to create runtime service client")
	}
	ic := criapi.NewImageServiceClient(conn)
	if ic == nil {
		return nil, nil, fmt.Errorf("failed to create image service client")
	}
	ver, err2 := rc.Version(context.TODO(), &criapi.VersionRequest{})
	fmt.Println("version:", ver, "err:", err2)
	return rc, ic, err
}

func unixDialer(addr string, timeout time.Duration) (net.Conn, error) {
	return net.DialTimeout("unix", addr, timeout)
}

// Initialize CRI client connection
func getClientConnection(criEndpoint string) (*grpc.ClientConn, error) {
	fmt.Println("get client connection:", criEndpoint)
	var conn *grpc.ClientConn
	var err error
	if strings.HasPrefix(criEndpoint, "unix") {
		var path string
		if criEndpoint == "unix" {
			path = CriSocketPath
		} else {
			path = criEndpoint
		}
		fmt.Println("connecting endpoint:", path)
		conn, err = grpc.Dial(path, grpc.WithTransportCredentials(insecure.NewCredentials()), grpc.WithTimeout(10*time.Second), grpc.WithDialer(unixDialer))
	} else {
		fmt.Println("connecting endpoint:", criEndpoint)
		conn, err = grpc.Dial(criEndpoint, grpc.WithTransportCredentials(insecure.NewCredentials()), grpc.WithTimeout(10*time.Second))
	}
	if err != nil {
		return nil, fmt.Errorf("failed to connect: %v", err)
	}
	return conn, nil
}

// Create a new Provider
func NewProvider(nodeName, operatingSystem string, nodeIP, internalIP string, resourceManager *manager.ResourceManager, daemonEndpointPort int32) (*Provider, error) {
	var criEndpoint string = CriPort
	runtimeClient, imageClient, err := getClientAPIs(criEndpoint)
	if err != nil {
		return nil, err
	}

	// Discard the operatingSystem from the InitConfig
	_ = operatingSystem
	operatingSystem = "LiteOS"

	provider := Provider{
		resourceManager:    resourceManager,
		podLogRoot:         PodLogRoot,
		podVolRoot:         PodVolRoot,
		nodeName:           nodeName,
		operatingSystem:    operatingSystem,
		internalIP:         internalIP,
		nodeIP:             nodeIP,
		daemonEndpointPort: daemonEndpointPort,
		// podStatus:          make(map[types.UID]CRIPod),
		holdingPod:    nil,
		runtimeClient: runtimeClient,
		imageClient:   imageClient,

		// cachedPods: make(map[CachedPodKey]CachedPod),
	}
	err = mkdirAll(provider.podLogRoot, PodLogRootPerms)
	if err != nil {
		return nil, err
	}
	err = mkdirAll(provider.podVolRoot, PodVolRootPerms)
	if err != nil {
		return nil, err
	}

	if !heart() {
		return nil, errors.New("connect agent failed")
	}

	p := &provider
	return p, err
}

// Take the labels from the Pod spec and turn the into a map
// Note: None of the "special" labels appear to have any meaning outside of Kubelet
func createPodLabels(pod *v1.Pod) map[string]string {
	labels := make(map[string]string)

	for k, v := range pod.Labels {
		labels[k] = v
	}

	return labels
}

// Convert protocol spec to CRI
func convertCRIProtocol(in v1.Protocol) criapi.Protocol {
	switch in {
	case v1.ProtocolTCP:
		return criapi.Protocol_TCP
	case v1.ProtocolUDP:
		return criapi.Protocol_UDP
	}
	return criapi.Protocol(-1)
}

// Create CRI port mappings from the Pod spec
func createPortMappings(pod *v1.Pod) []*criapi.PortMapping {
	result := []*criapi.PortMapping{}
	for _, c := range pod.Spec.Containers {
		for _, p := range c.Ports {
			result = append(result, &criapi.PortMapping{
				HostPort:      p.HostPort,
				ContainerPort: p.ContainerPort,
				Protocol:      convertCRIProtocol(p.Protocol),
				HostIp:        p.HostIP,
			})
		}
	}
	return result
}

// A Pod is privileged if it contains a privileged container. Look for one in the Pod spec
func existsPrivilegedContainerInSpec(pod *v1.Pod) bool {
	for _, c := range pod.Spec.Containers {
		if c.SecurityContext != nil &&
			c.SecurityContext.Privileged != nil &&
			*c.SecurityContext.Privileged {
			return true
		}
	}
	return false
}

// Test a bool pointer. If nil, return default value
func valueOrDefaultBool(input *bool, defVal bool) bool {
	if input != nil {
		return *input
	}
	return defVal
}

// Create CRI LinuxContainerConfig from Pod and Container spec
// TODO: Currently incomplete
func createCtrLinuxConfig(container *v1.Container, pod *v1.Pod) *criapi.LinuxContainerConfig {
	v1sc := container.SecurityContext
	var sc *criapi.LinuxContainerSecurityContext

	if v1sc != nil {
		sc = &criapi.LinuxContainerSecurityContext{
			Capabilities:       nil,                                        // type: *Capability
			Privileged:         valueOrDefaultBool(v1sc.Privileged, false), // No default Pod value
			NamespaceOptions:   nil,                                        // type: *NamespaceOption
			SelinuxOptions:     nil,                                        // type: *SELinuxOption
			RunAsUser:          nil,                                        // type: *Int64Value
			RunAsGroup:         nil,                                        // type: *Int64Value
			RunAsUsername:      "",
			ReadonlyRootfs:     false,
			SupplementalGroups: []int64{},
			ApparmorProfile:    "",
			SeccompProfilePath: "",
			NoNewPrivs:         false,
		}
	}
	return &criapi.LinuxContainerConfig{
		Resources:       nil, // type: *LinuxContainerResources
		SecurityContext: sc,
	}
}

// Provider function to create a Pod
func (p *Provider) CreatePod(ctx context.Context, pod *v1.Pod) error {
	klog.Info("Create pod:", pod)
	ctx, span := trace.StartSpan(ctx, "cri.CreatePod")
	defer span.End()
	ctx = span.WithFields(ctx, log.Fields{
		"pod": path.Join(pod.GetNamespace(), pod.GetName()),
	})
	err := p.createPod(ctx, pod)
	span.SetStatus(err)
	return err
}

func (p *Provider) createPod(ctx context.Context, pod *v1.Pod) error {
	log.G(ctx).Debugf("receive CreatePod %q", pod.Name)
	klog.Infof("receive CreatePod %q", pod.Name)
	var attempt uint32 // TODO: Track attempts. Currently always 0
	logPath := filepath.Join(p.podLogRoot, string(pod.UID))
	volPath := filepath.Join(p.podVolRoot, string(pod.UID))
	err := p.refreshNodeState(ctx)
	if err != nil {
		return err
	}
	pConfig, err := generatePodSandboxConfig(ctx, pod, logPath, attempt)
	if err != nil {
		return err
	}
	existing := p.getHoldingCRIPod()

	// TODO: Is re-using an existing sandbox with the UID the correct behavior?
	// TODO: Should delete the sandbox if container creation fails
	var pId string
	if existing == nil {
		klog.Info("run sandbox", pConfig)
		pId, err = runPodSandbox(ctx, p.runtimeClient, pConfig)
		if err != nil {
			return err
		}
	} else {
		pId = existing.status.Metadata.Uid
	}

	containerIds := make([]string, 0)
	for _, c := range pod.Spec.Containers {
		log.G(ctx).Debugf("Pulling image %s", c.Image)
		klog.Info("Pulling image", c.Image)
		imageRef, err := pullImage(ctx, p.imageClient, c.Image)
		if err != nil {
			return err
		}
		log.G(ctx).Debugf("Creating container %s", c.Name)
		klog.Info("Creating container", c.Name)
		cConfig, err := generateContainerConfig(ctx, &c, pod, imageRef, volPath, p.resourceManager, attempt)
		if err != nil {
			return err
		}
		cId, err := createContainer(ctx, p.runtimeClient, cConfig, pConfig, pId)
		if err != nil {
			return err
		}
		log.G(ctx).Debugf("Starting container %s", c.Name)
		klog.Infof("Starting container %s", c.Name)
		err = startContainer(ctx, p.runtimeClient, cId)
		if err != nil {
			klog.Errorln("start container ", cId, " failed")
		}
		containerIds = append(containerIds, cId)
	}

	time.AfterFunc(5*time.Second, func() {
		p.refreshNodeState(ctx)
		existing = p.getHoldingCRIPod()
		if existing == nil {
			return
		}
		newPod := pod.DeepCopy()
		// check phase
		newPod.Status.Phase = v1.PodRunning
		for _, c := range existing.containers {
			if c.State != criapi.ContainerState_CONTAINER_RUNNING {
				newPod.Status.Phase = v1.PodFailed
			}
		}
		p.notifyStatus(newPod)
	})

	return err
}

// Update is currently not required or even called by VK, so not implemented
func (p *Provider) UpdatePod(ctx context.Context, pod *v1.Pod) error {
	log.G(ctx).Debugf("receive UpdatePod %q", pod.Name)
	klog.Info("update pod:", pod, ". The method has not implemented.")
	return nil
}

// Provider function to delete a pod and its containers
func (p *Provider) DeletePod(ctx context.Context, pod *v1.Pod) error {
	ctx, span := trace.StartSpan(ctx, "cri.DeletePod")
	defer span.End()
	klog.Info("delete pod:", pod)
	ctx = span.WithFields(ctx, log.Fields{
		"pod": path.Join(pod.GetNamespace(), pod.GetName()),
	})
	err := p.deletePod(ctx, pod)
	span.SetStatus(err)
	return err
}

func (p *Provider) deletePod(ctx context.Context, pod *v1.Pod) error {
	log.G(ctx).Debugf("receive DeletePod %q", pod.Name)

	err := p.refreshNodeState(ctx)
	if err != nil {
		return err
	}

	ps := p.getHoldingCRIPod()
	if ps == nil {
		return errdefs.NotFoundf("Pod %s not found", pod.UID)
	}

	// TODO: Check pod status for running state
	err = stopPodSandbox(ctx, p.runtimeClient, ps.status.Id)
	if err != nil {
		// Note the error, but shouldn't prevent us trying to delete
		log.G(ctx).Debug(err)
	}

	// Remove any emptyDir volumes
	// TODO: Is there other cleanup that needs to happen here?
	err = removeAll(filepath.Join(p.podVolRoot, string(pod.UID)))
	if err != nil {
		log.G(ctx).Debug(err)
	}
	err = removePodSandbox(ctx, p.runtimeClient, ps.status.Id)

	p.notifyStatus(pod)
	return err
}

// Provider function to return a Pod spec - mostly used for its status
func (p *Provider) GetPod(ctx context.Context, namespace, name string) (*v1.Pod, error) {
	klog.Info("Get pod: namespace ", namespace, ",name ", name)
	ctx, span := trace.StartSpan(ctx, "cri.GetPod")
	defer span.End()
	ctx = span.WithFields(ctx, log.Fields{
		"pod": path.Join(namespace, name),
	})
	pod, err := p.getPod(ctx, namespace, name)
	span.SetStatus(err)
	return pod, err
}

func (p *Provider) getPod(ctx context.Context, namespace, name string) (*v1.Pod, error) {
	log.G(ctx).Debugf("receive GetPod %q", name)

	err := p.refreshNodeState(ctx)
	if err != nil {
		return nil, err
	}

	pod := p.getHoldingCRIPod()
	if pod == nil {
		return nil, errdefs.NotFoundf("Pod %s in namespace %s could not be found on the node", name, namespace)
	}

	return createPodSpecFromCRI(pod, p.nodeName), nil
}

// Provider function to read the logs of a container
func (p *Provider) GetContainerLogs(ctx context.Context, namespace, podName, containerName string, opts api.ContainerLogOpts) (io.ReadCloser, error) {
	return nil, errdefs.NotFound("not implement")
}

// RunInContainer executes a command in a container in the pod, copying data
// between in/out/err and the container's stdin/stdout/stderr.
// TODO: Implementation
func (p *Provider) RunInContainer(ctx context.Context, namespace, name, container string, cmd []string, attach api.AttachIO) error {
	log.G(ctx).Debugf("receive ExecInContainer %q\n", container)
	klog.Info("Run in container: namespace ", namespace, ", name", name, ", container ", container)
	return nil
}

func (p *Provider) getHoldingCRIPod() *CRIPod {
	if p.holdingPod == nil {
		return nil
	}
	if len(p.holdingPod.containers) == 0 {
		return nil
	}
	return p.holdingPod
}

// Provider function to return the status of a Pod
func (p *Provider) GetPodStatus(ctx context.Context, namespace, name string) (*v1.PodStatus, error) {
	// klog.Info("Get pod status: namespace ", namespace, ", name ", name)
	fmt.Println("Get pod status: namespace ", namespace, ", name ", name)
	ctx, span := trace.StartSpan(ctx, "cri.GetPodStatus")
	defer span.End()
	ctx = span.WithFields(ctx, log.Fields{
		"pod": path.Join(namespace, name),
	})
	pod, err := p.getPodStatus(ctx, namespace, name)
	span.SetStatus(err)
	klog.Info("Get pod status result:", pod)
	fmt.Println("Get pod status: namespace ", namespace, ", name ", name, "result ", pod)
	return pod, err
}

func (p *Provider) getPodStatus(ctx context.Context, namespace, name string) (*v1.PodStatus, error) {
	log.G(ctx).Debugf("receive GetPodStatus %q", name)

	err := p.refreshNodeState(ctx)
	if err != nil {
		return nil, err
	}

	pod := p.getHoldingCRIPod()
	if pod == nil {
		return nil, errdefs.NotFoundf("pod %s in namespace %s could not be found on the node", name, namespace)
	}

	return createPodStatusFromCRI(pod), nil
}

// Converts CRI container state to ContainerState
func createContainerStateFromCRI(state criapi.ContainerState, status *criapi.ContainerStatus) *v1.ContainerState {
	var result *v1.ContainerState
	switch state {
	case criapi.ContainerState_CONTAINER_UNKNOWN:
		fallthrough
	case criapi.ContainerState_CONTAINER_CREATED:
		result = &v1.ContainerState{
			Waiting: &v1.ContainerStateWaiting{
				Reason:  status.Reason,
				Message: status.Message,
			},
		}
	case criapi.ContainerState_CONTAINER_RUNNING:
		result = &v1.ContainerState{
			Running: &v1.ContainerStateRunning{
				StartedAt: metav1.NewTime(time.Unix(0, status.StartedAt)),
			},
		}
	case criapi.ContainerState_CONTAINER_EXITED:
		result = &v1.ContainerState{
			Terminated: &v1.ContainerStateTerminated{
				ExitCode:   status.ExitCode,
				Reason:     status.Reason,
				Message:    status.Message,
				StartedAt:  metav1.NewTime(time.Unix(0, status.StartedAt)),
				FinishedAt: metav1.NewTime(time.Unix(0, status.FinishedAt)),
			},
		}
	}
	return result
}

// Converts CRI container spec to Container spec
func createContainerSpecsFromCRI(containerMap map[string]*criapi.ContainerStatus) ([]v1.Container, []v1.ContainerStatus) {
	containers := make([]v1.Container, 0, len(containerMap))
	containerStatuses := make([]v1.ContainerStatus, 0, len(containerMap))
	for _, c := range containerMap {
		// TODO: Fill out more fields
		container := v1.Container{
			Name:  c.Metadata.Name,
			Image: c.Image.Image,
			//Command:    Command is buried in the Info JSON,
		}
		containers = append(containers, container)
		// TODO: Fill out more fields
		containerStatus := v1.ContainerStatus{
			Name:        c.Metadata.Name,
			Image:       c.Image.Image,
			ImageID:     c.ImageRef,
			ContainerID: c.Id,
			Ready:       c.State == criapi.ContainerState_CONTAINER_RUNNING,
			State:       *createContainerStateFromCRI(c.State, c),
			// LastTerminationState:
			// RestartCount:
		}

		containerStatuses = append(containerStatuses, containerStatus)
	}
	return containers, containerStatuses
}

// Converts CRI pod status to a PodStatus
func createPodStatusFromCRI(p *CRIPod) *v1.PodStatus {
	_, cStatuses := createContainerSpecsFromCRI(p.containers)

	// TODO: How to determine PodSucceeded and PodFailed?
	// phase := v1.PodPending
	// if p.status.State == criapi.PodSandboxState_SANDBOX_READY {
	// 	phase = v1.PodRunning
	// }
	// // phase := v1.PodRunning
	phase := v1.PodRunning
	if p.containers != nil {
		for _, c := range p.containers {
			if c.State != criapi.ContainerState_CONTAINER_RUNNING {
				phase = v1.PodPending
				break
			}
		}
	}

	startTime := metav1.NewTime(time.Unix(0, p.status.CreatedAt))
	return &v1.PodStatus{
		Phase:             phase,
		Conditions:        []v1.PodCondition{},
		Message:           "",
		Reason:            "",
		HostIP:            "",
		PodIP:             p.status.Network.Ip,
		StartTime:         &startTime,
		ContainerStatuses: cStatuses,
	}
}

// Creates a Pod spec from data obtained through CRI
func createPodSpecFromCRI(p *CRIPod, nodeName string) *v1.Pod {
	cSpecs, _ := createContainerSpecsFromCRI(p.containers)

	// TODO: Fill out more fields here
	podSpec := v1.Pod{
		TypeMeta: metav1.TypeMeta{
			Kind:       "Pod",
			APIVersion: "v1",
		},
		ObjectMeta: metav1.ObjectMeta{
			Name:      p.status.Metadata.Name,
			Namespace: p.status.Metadata.Namespace,
			// ClusterName:       TODO: What is this??
			UID:               types.UID(p.status.Metadata.Uid),
			CreationTimestamp: metav1.NewTime(time.Unix(0, p.status.CreatedAt)),
		},
		Spec: v1.PodSpec{
			NodeName:   nodeName,
			Volumes:    []v1.Volume{},
			Containers: cSpecs,
		},
		Status: *createPodStatusFromCRI(p),
	}

	return &podSpec
}

// Provider function to return all known pods
// TODO: Should this be all pods or just running pods?
func (p *Provider) GetPods(ctx context.Context) ([]*v1.Pod, error) {
	log.G(ctx).Debugf("receive GetPods")
	klog.Info("Get pods")

	err := p.refreshNodeState(ctx)
	if err != nil {
		return nil, err
	}

	var pods []*v1.Pod
	ps := p.getHoldingCRIPod()
	if ps != nil {
		pods = []*v1.Pod{createPodSpecFromCRI(ps, p.nodeName)}
	} else {
		pods = []*v1.Pod{}
	}

	klog.Info("Get pods result:", pods)

	return pods, nil
}

func (p *Provider) ConfigureNode(ctx context.Context, n *v1.Node) {
	err := p.refreshNodeState(ctx)
	if err != nil {
		panic(err)
	}

	n.Status.Capacity = p.capacity(ctx)
	n.Status.Allocatable = p.allocatable(ctx)
	n.Status.Conditions = p.nodeConditions()
	n.Status.Addresses = p.nodeAddresses()
	n.Status.DaemonEndpoints = p.nodeDaemonEndpoints()
	n.Status.NodeInfo.OperatingSystem = p.operatingSystem
}

// Provider function to return the capacity of the node
func (p *Provider) capacity(ctx context.Context) v1.ResourceList {
	var cpuQ resource.Quantity
	cpuQ.Set(int64(numCPU()))
	var memQ resource.Quantity
	memQ.Set(int64(getSystemTotalMemory()))

	return v1.ResourceList{
		"cpu":    cpuQ,
		"memory": memQ,
		"pods":   resource.MustParse("1"),
	}
}

func (p *Provider) allocatable(ctx context.Context) v1.ResourceList {
	// TODO: get current state
	return nil
}

// Provider function to return node conditions
// TODO: For now, use the same node conditions as the MockProvider
func (p *Provider) nodeConditions() []v1.NodeCondition {
	// TODO: Make this configurable
	return []v1.NodeCondition{
		{
			Type:               "Ready",
			Status:             v1.ConditionTrue,
			LastHeartbeatTime:  metav1.Now(),
			LastTransitionTime: metav1.Now(),
			Reason:             "KubeletReady",
			Message:            "kubelet is ready.",
		},
		{
			Type:               "OutOfDisk",
			Status:             v1.ConditionFalse,
			LastHeartbeatTime:  metav1.Now(),
			LastTransitionTime: metav1.Now(),
			Reason:             "KubeletHasSufficientDisk",
			Message:            "kubelet has sufficient disk space available",
		},
		{
			Type:               "MemoryPressure",
			Status:             v1.ConditionFalse,
			LastHeartbeatTime:  metav1.Now(),
			LastTransitionTime: metav1.Now(),
			Reason:             "KubeletHasSufficientMemory",
			Message:            "kubelet has sufficient memory available",
		},
		{
			Type:               "DiskPressure",
			Status:             v1.ConditionFalse,
			LastHeartbeatTime:  metav1.Now(),
			LastTransitionTime: metav1.Now(),
			Reason:             "KubeletHasNoDiskPressure",
			Message:            "kubelet has no disk pressure",
		},
		{
			Type:               "NetworkUnavailable",
			Status:             v1.ConditionFalse,
			LastHeartbeatTime:  metav1.Now(),
			LastTransitionTime: metav1.Now(),
			Reason:             "RouteCreated",
			Message:            "RouteController created a route",
		},
	}

}

// Provider function to return a list of node addresses
func (p *Provider) nodeAddresses() []v1.NodeAddress {
	addresses := []v1.NodeAddress{{
		Type:    "InternalIP",
		Address: p.internalIP,
	}}
	if p.nodeIP != "" && p.nodeIP != "127.0.0.1" {
		addresses = append(addresses, v1.NodeAddress{
			Type:    "ExternalIP",
			Address: p.nodeIP,
		})
	}
	return addresses
}

// Provider function to return the daemon endpoint
func (p *Provider) nodeDaemonEndpoints() v1.NodeDaemonEndpoints {
	return v1.NodeDaemonEndpoints{
		KubeletEndpoint: v1.DaemonEndpoint{
			Port: p.daemonEndpointPort,
		},
	}
}

func (p *Provider) NotifyPods(ctx context.Context, f func(*v1.Pod)) {
	fmt.Println("NotifyPods!")
	p.notifyStatus = f
	go p.statusLoop(ctx)
}

func (p *Provider) statusLoop(ctx context.Context) {
	fmt.Println("statusLoop start")
	defer fmt.Println("statusLoop exit")
	t := time.NewTimer(5 * time.Second)
	if !t.Stop() {
		<-t.C
	}

	for {
		t.Reset(5 * time.Second)
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}

		if err := p.notifyPodStatuses(ctx); err != nil {
			log.G(ctx).WithError(err).Error("Error updating node statuses")
		}
	}
}

func (p *Provider) notifyPodStatuses(ctx context.Context) error {
	ls, err := p.GetPods(ctx)
	if err != nil {
		return err
	}

	for _, pod := range ls {
		p.notifyStatus(pod)
	}

	return nil
}
