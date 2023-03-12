FROM golang:1.19 as builder
ARG GO_BUILD_PATH=/root/.cache/go-build
ARG GO_MOD_PATH=/go/pkg/mod
ENV GOMODCACHE ${GO_MOD_PATH}
WORKDIR /go/src/hrglet
COPY go.* ./
RUN go mod download
COPY . ./
# RUN apk add git && apk add make && apk add go
RUN CGO_ENABLED=0 go build -ldflags '-extldflags "-static"' -o /bin/hrglet ./cmd/hrglet
RUN rm -rf ${GO_MOD_PATH}
RUN rm -rf ${GO_BUILD_PATH}
FROM alpine 
COPY --from=builder /bin/hrglet /bin/hrglet
# ADD /bin/kubernetes /bin/kubernetes
EXPOSE 10250
ENV KUBERNETES_SERVICE_HOST "192.168.30.86"
ENV KUBERNETES_SERVICE_PORT "6443"
ENV VKUBELET_POD_IP ""
ENV APISERVER_CERT_LOCATION "/etc/kubernetes/pki/ca.crt"
ENV APISERVER_KEY_LOCATION "/etc/kubernetes/pki/ca.key"
ENV KUBELET_PORT "10250"
ENV REMOTE_IP ""
ENV CRI_PORT ""
ENV AGENT_PORT "40002"
ENV NODE_NAME "hrglet"
CMD [ "hrglet", "--provider", "cri", "--kubeconfig", "/etc/kubernetes/admin.conf", "--nodename", "vk01" ]
