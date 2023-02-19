package cri

import (
	"fmt"
	"log"
	"net"
	"net/rpc"
	"net/rpc/jsonrpc"
	"os"
)

func heart() bool {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		log.Fatal("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var save bool

	err = client.Call("heart", nil, &save)
	if err != nil {
		log.Fatal("scanFile error:", err)
		return false
	}
	return save
}

func getSystemTotalMemory() uint64 {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		log.Fatal("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var memory uint64
	err = client.Call("getSystemTotalMemory", nil, &memory)
	if err != nil {
		log.Fatal("getSystemTotalMemory error:", err)
	}
	log.Fatal("getSystemTotalMemory:", memory)
	return memory
}

func scanFile(filename string) ([]string, error) {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		log.Fatal("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var lines []string

	err = client.Call("scanFile", &filename, &lines)
	if err != nil {
		log.Fatal("scanFile error:", err)
	}
	return lines, nil
}

type mkdirAllArgs struct {
	Path string `json:"path"`
	Perm uint32 `json:"perm"`
}

func mkdirAll(path string, perm os.FileMode) error {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		log.Fatal("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))
	args := mkdirAllArgs{
		Path: path,
		Perm: uint32(perm),
	}
	var mked bool
	err = client.Call("mkdirAll", &args, &mked)
	if err != nil {
		log.Fatal("mkdirAll error:", err)
		return err
	}
	_ = mked
	return nil
}

func removeAll(path string) error {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		log.Fatal("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var rmed bool
	err = client.Call("mkdirAll", &path, &rmed)
	if err != nil {
		log.Fatal("mkdirAll error:", err)
		return err
	}
	_ = rmed
	return nil
}

func init() {
	fmt.Println("load rpc client")
}
