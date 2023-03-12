LINTER_BIN ?= golangci-lint

GO111MODULE := on
export GO111MODULE

.PHONY: build
build: bin/hrglet

.PHONY: clean
clean: files := bin/hrglet
clean:
	@rm $(files) &>/dev/null || exit 0

.PHONY: test
test:
	@echo running tests
	go test -v ./...

.PHONY: vet
vet:
	@go vet ./... #$(packages)

.PHONY: lint
lint:
	@$(LINTER_BIN) run --new-from-rev "HEAD~$(git rev-list master.. --count)" ./...

.PHONY: check-mod
check-mod: # verifies that module changes for go.mod and go.sum are checked in
	@hack/ci/check_mods.sh

.PHONY: mod
mod:
	@go mod tidy

bin/hrglet: BUILD_VERSION          ?= $(shell git describe --tags --always --dirty="-dev")
bin/hrglet: BUILD_DATE             ?= $(shell date -u '+%Y-%m-%d-%H:%M UTC')
bin/hrglet: VERSION_FLAGS    := -ldflags='-X "main.buildVersion=$(BUILD_VERSION)" -X "main.buildTime=$(BUILD_DATE)"'

bin/%:
	CGO_ENABLED=0 go build -ldflags '-extldflags "-static"' -o bin/$(*) $(VERSION_FLAGS) ./cmd/$(*)

docker-build: TAG_NAME          ?= $(shell git describe --tags --always --dirty="-dev") 
docker-build:
	docker login
	docker build -t eustrain/hrglet:$(TAG_NAME) .
	docker push eustrain/hrglet:$(TAG_NAME)
	echo $(TAG_NAME)