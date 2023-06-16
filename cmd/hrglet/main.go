// Copyright © 2017 The virtual-kubelet authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import (
	"context"
	"os"
	"strings"

	"github.com/eustrainLee/cri"
	"github.com/sirupsen/logrus"
	cli "github.com/virtual-kubelet/node-cli"
	logruscli "github.com/virtual-kubelet/node-cli/logrus"
	opencensuscli "github.com/virtual-kubelet/node-cli/opencensus"
	"github.com/virtual-kubelet/node-cli/opts"
	"github.com/virtual-kubelet/node-cli/provider"
	"github.com/virtual-kubelet/virtual-kubelet/log"
	logruslogger "github.com/virtual-kubelet/virtual-kubelet/log/logrus"
	"github.com/virtual-kubelet/virtual-kubelet/trace"
	"github.com/virtual-kubelet/virtual-kubelet/trace/opencensus"
)

var (
	buildVersion = "N/A"
	buildTime    = "N/A"
	// k8sVersion   = "v1.15.2" // This should follow the version of k8s.io/kubernetes we are importing
	k8sVersion = "v1.21.9"
)

var (
	nodeIP = "127.0.0.1"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	ctx = cli.ContextWithCancelOnSignal(ctx)
	logger := logrus.StandardLogger()
	log.L = logruslogger.FromLogrus(logrus.NewEntry(logger))
	logConfig := &logruscli.Config{LogLevel: "info"}

	trace.T = opencensus.Adapter{}
	traceConfig := opencensuscli.Config{
		AvailableExporters: map[string]opencensuscli.ExporterInitFunc{
			"ocagent": initOCAgent,
		},
	}

	// agent and IP address
	nodeIP = os.Getenv("REMOTE_IP")
	if nodeIP == "" {
		nodeIP = "127.0.0.1"
	}
	criPort := os.Getenv("CRI_PORT")
	if criPort != "" {
		if strings.HasPrefix(criPort, "unix") {
			cri.CriPort = criPort
		} else if strings.Contains(criPort, ":") {
			cri.CriPort = criPort
		} else {
			cri.CriPort = nodeIP + ":" + criPort
		}
	} else {
		cri.CriPort = nodeIP + ":10350"
	}

	agentDependency := os.Getenv("AGENT_DEPEND") != "0"
	if agentDependency {
		agentPort := os.Getenv("AGENT_PORT")
		if agentPort != "" {
			if strings.Contains(agentPort, ":") {
				cri.AgentPort = agentPort
			} else {
				cri.AgentPort = nodeIP + ":" + agentPort
			}
		} else {
			cri.AgentPort = nodeIP + ":40002"
		}
	}

	o := opts.New()
	o.Provider = "hrgcri"
	o.Version = strings.Join([]string{k8sVersion, "vk-hrgcri", buildVersion}, "-")
	node, err := cli.New(ctx,
		cli.WithBaseOpts(o),
		cli.WithCLIVersion(buildVersion, buildTime),
		cli.WithProvider("hrgcri", func(cfg provider.InitConfig) (provider.Provider, error) {
			return cri.NewProvider(cfg.NodeName, cfg.OperatingSystem, nodeIP, cfg.InternalIP, cfg.ResourceManager, cfg.DaemonPort, agentDependency)
		}),
		cli.WithPersistentFlags(logConfig.FlagSet()),
		cli.WithPersistentPreRunCallback(func() error {
			return logruscli.Configure(logConfig, logger)
		}),
		cli.WithPersistentFlags(traceConfig.FlagSet()),
		cli.WithPersistentPreRunCallback(func() error {
			return opencensuscli.Configure(ctx, &traceConfig, o)
		}),
	)
	if err != nil {
		log.G(ctx).Fatal(err)
	}

	if err := node.Run(); err != nil {
		log.G(ctx).Fatal(err)
	}
}

func init() {
	log.L.Info("virtual kubelet run")
}
