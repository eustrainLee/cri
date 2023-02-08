package cri

import (
	"fmt"
	"log"
	"net"
	"net/rpc"
	"net/rpc/jsonrpc"
)

func getSystemTotalMemory() uint64 {
	conn, err := net.Dial("tcp", agentPort)
	if err != nil {
		log.Fatal("dial error:", err)
	}

	client := rpc.NewClientWithCodec(jsonrpc.NewClientCodec(conn))

	var reply uint64
	err = client.Call("getSystemTotalMemory", nil, &reply)
	if err != nil {
		log.Fatal("getSystemTotalMemory error:", err)
	}
	fmt.Printf("getSystemTotalMemory: %d\n", reply)
	return reply
}

func init() {
	fmt.Println("load rpc client")
}
