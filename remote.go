package cri

import (
	"fmt"
	"net"
	"net/rpc"
	"net/rpc/jsonrpc"
)

func heart() bool {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		fmt.Println("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var save bool

	err = client.Call("heart", nil, &save)
	if err != nil {
		fmt.Println("scanFile error:", err)
		return false
	}
	return save
}

func getSystemTotalMemory() uint64 {
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		fmt.Println("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var memory uint64
	err = client.Call("getSystemTotalMemory", nil, &memory)
	if err != nil {
		fmt.Println("getSystemTotalMemory error:", err)
	}
	conn.Close()
	fmt.Println("getSystemTotalMemory:", memory)
	return memory
}

func numCPU() int {
	// TODO:
	conn, err := net.Dial("tcp", AgentPort)
	if err != nil {
		fmt.Println("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var numCPU uint64
	err = client.Call("getNumCPU", nil, &numCPU)
	if err != nil {
		fmt.Println("getNumCPU error:", err)
	}
	conn.Close()
	fmt.Println("getNumCPU:", numCPU)
	return int(numCPU)
}

// func scanFile(filename string) ([]string, error) {
// 	conn, err := net.Dial("tcp", AgentPort)
// 	if err != nil {
// 		fmt.Println("dial error:", err)
// 	}

// 	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

// 	var lines []string

// 	err = client.Call("scanFile", &filename, &lines)
// 	if err != nil {
// 		fmt.Println("scanFile error:", err)
// 	}
// 	return lines, nil
// }

// type mkdirAllArgs struct {
// 	Path string `json:"path"`
// 	Perm uint32 `json:"perm"`
// }

// func mkdirAll(path string, perm os.FileMode) error {
// 	conn, err := net.Dial("tcp", AgentPort)
// 	if err != nil {
// 		fmt.Println("dial error:", err)
// 	}

// 	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))
// 	args := mkdirAllArgs{
// 		Path: path,
// 		Perm: uint32(perm),
// 	}
// 	var mked bool
// 	err = client.Call("mkdirAll", &args, &mked)
// 	if err != nil {
// 		fmt.Println("mkdirAll error:", err)
// 		return err
// 	}
// 	_ = mked
// 	return nil
// }

// func removeAll(path string) error {
// 	conn, err := net.Dial("tcp", AgentPort)
// 	if err != nil {
// 		fmt.Println("dial error:", err)
// 	}

// 	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

// 	var rmed bool
// 	err = client.Call("mkdirAll", &path, &rmed)
// 	if err != nil {
// 		fmt.Println("mkdirAll error:", err)
// 		return err
// 	}
// 	_ = rmed
// 	return nil
// }

func init() {
	fmt.Println("load rpc client")
}
