#include "rpc.hpp"
#include <sys/sysinfo.h>
#include <filesystem>
#include <unistd.h>

struct GetSystemTotalMemoryArg{};
struct GetSystemTotalMemoryRes{ int64_t value;};

int64_t getSystemTotalMemory() {
    ::std::clog << "getSystemTotalMemory" << ::std::endl;
    struct sysinfo info;
    int ret;
    ret = sysinfo(&info);
    if (ret == -1) {
        return 0;
    }
    return info.totalram * info.mem_unit;
}


int64_t getNumCPU() {
    ::std::clog << "getNumCPU" << ::std::endl;
    struct sysinfo info;
    int ret;
    // ret = sysinfo(&info);
    ret = sysconf(_SC_NPROCESSORS_CONF);
    if (ret == -1) {
        return 0;
    }
    return info.totalram * info.mem_unit;
}

bool mkdirAll(::std::string path, uint perm) {
    ::std::clog << "mkdirAll path:" + path << ", perm:" + ::std::to_string(perm) << ::std::endl;
    ::std::filesystem::path stdpath = path;
    ::std::filesystem::perms stdperms{perm};
    // ::std::error_code errcode;
    if (::std::filesystem::exists(stdpath)) {
        return false;
    }
    if (!::std::filesystem::create_directories(stdpath)) {
        throw ::std::string{"mkdir:"} + path + ::std::string{" failed"};
    }
    if (perm != 0) {
        // ::std::filesystem::permissions(stdpath, stdperms, errcode);
        ::std::filesystem::permissions(stdpath, stdperms);
    }
    // if(errcode) {
    //     throw errcode.message();
    // }
    return true;
}

bool removeAll(::std::string path) {
    ::std::clog << "removeAll path:" + path << ::std::endl;
    ::std::filesystem::path stdpath = path;
    if (!::std::filesystem::exists(stdpath)) {
        return false;
    }
    auto removed = std::filesystem::remove_all(stdpath);
    return removed != 0;
}

::std::vector<::std::string> scanFile(std::string path) {
    ::std::filesystem::path stdpath = path;
    if (!::std::filesystem::exists(stdpath)) {
        throw ::std::string{"file:"} + path + " not exist";
    }
    ::std::vector<::std::string> lines;
    ::std::ifstream ifs{path};
    ::std::string line;
    while(::std::getline(ifs,line)) {
        lines.push_back(::std::move(line));
    }
    ifs.close();
    return lines;
}


nlohmann::json mkdirAllWrapped(nlohmann::json const& args) {
    return mkdirAll(args[0]["path"], args[0]["perm"]);
}

nlohmann::json removeAllWrapped(nlohmann::json const& args) {
    return removeAll(args[0]);
}

nlohmann::json scanFileWrapped(nlohmann::json const& args) {
    return scanFile(args[0]);
}

nlohmann::json getSystemTotalMemoryWrapped(nlohmann::json const& args) {
    return getSystemTotalMemory();
}

nlohmann::json getNumCPUWrapped(nlohmann::json const& args) {
    return getNumCPU();
}

nlohmann::json heart(nlohmann::json const& args) {
    return true;
}

bool init() {
    // Do nothing
    return true;
}

// The jsonrpc in golang is pure rubbish!
int main(int argc, char* argv[]) {
    if (!init()) {
        ::std::cerr << "init failed" << ::std::endl;
        return -1;
    }
    rpc::Server server;
    server.addMethod("mkdirAll", mkdirAllWrapped);
    server.addMethod("removeAll", removeAllWrapped);
    server.addMethod("scanFile", scanFileWrapped);
    server.addMethod("getSystemTotalMemory", getSystemTotalMemoryWrapped);
    server.addMethod("getNumCPU", getNumCPUWrapped);
    server.addMethod("heart", heart);
    ::std::cout << "server start" << ::std::endl;
    ::std::string ip = "127.0.0.1";
    int port = 40002;
    if (argc > 2) {
        port = ::std::stoi(argv[2]);
    }
    if (argc > 1) {
        ip = argv[1];
    }
    server.start(ip,port);
}
