#include <iostream>
#include <atomic>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <future>
#include <gtest/gtest.h>
#include <cstdlib>

#include "rpc.hpp"


// using namespace rpc;
// using namespace configor;
namespace {
    const ::rpc::Json& BadRequest_2_0 = R"({"error":{"code":-32600,"message":"Invalid Request"},"id":null,"jsonrpc":"2.0"})"_json;
    const ::rpc::Json& BadRequest_1_0 = R"({"error":{"code":-32600,"message":"Invalid Request"},"id":null})"_json;
    

    std::string randstr(int len) {
        char *str = new char[len];
        srand(time(NULL));
        int i;
        for (i = 0; i < len; ++i) {
            switch ((rand() % 3))
            {
            case 1:
                str[i] = 'A' + rand() % 26;
                break;
            case 2:
                str[i] = 'a' + rand() % 26;
                break;
            default:
                str[i] = '0' + rand() % 10;
                break;
            }
        }
        str[++i] = '\0';
        std::string s(str);
        delete str;
        return s;
    }

    class tcpbuf : public std::streambuf {
    public:
        enum { BUFSIZE = 1 << 10 };
        tcpbuf(socket_t s) {
            // initsocklib();
            sock = s;
        }
    protected:
        // Buffered get
        int underflow() override {
            // auto n = ::recv(sock, buf, BUFSIZE, 0);
            auto n = ::read(sock, buf, BUFSIZE);
            return n > 0 ? (setg(buf, buf, buf + n), *gptr()) : EOF;
        }
        // Unbuffered put
        int overflow(int c) override {
            if (c == EOF)
                // return close();
                return -1;
            char b = c;
            return ::send(sock, &b, 1, 0) > 0 ? c : EOF;
        }
    private:
        socket_t sock;
        char buf[BUFSIZE];
    };
    bool readJson(
        // httplib::Stream &strm,
        socket_t sock,
        rpc::Json& json
        ) {
        //sock_iadapter iadapter(strm.socket());
        //configor::iadapterstream is {iadapter};
        // tcpbuf buf(strm.socket());
        tcpbuf buf(sock);
        ::std::istream stream(&buf);
        try{
            //json = json::parse(is);
            stream >> json;
        } catch(const ::std::exception& ex) {
            ::std::cerr << ex.what() << ::std::endl;
            ::std::cerr << "log error: Read Json" << ::std::endl;
            ::std::cerr << json.dump() << ::std::endl;
            return false;
        }
        ::std::cout << "log[Client Read Json]: " << json.dump() << ::std::endl;
        return true;
    }

    bool writeJson(
        // httplib::Stream &strm,
        socket_t sock,
        const rpc::Json& json
    ) {
        // ::std::cout << "write:" << json.dump() << ::std::endl;
        // tcpbuf buf(strm.socket());
        ::std::cout << "log[Client Write Json]: " << json.dump() << ::std::endl;
        tcpbuf buf(sock);
        ::std::ostream stream(&buf);
        try{
            stream << json;
            stream.flush();
        } catch(...) {
            return false;
        }
        return true;
    }

    /**
     * @brief 
     * 
     * @param host 目标主机IP
     * @param port 目标主机端口
     * @param in 输出buf
     * @param length 输入buf长度
     * @param out 输出buf，默认拥有足够大的空间
     * @return int 当错误发生时返回-1，否则返回写入输出buf的长度
     */
    bool tell(const char* host, int port, const rpc::Json& in, rpc::Json& out, bool read = true) {
        try {
            auto sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in servaddr = {};
            servaddr.sin_family = AF_INET;
            servaddr.sin_addr.s_addr = inet_addr(host);
            servaddr.sin_port = htons(port);
            static constexpr int MAX_NTRY = 1 << 10;
            ::std::atomic<int> ntry = 0;
            while (-1 == connect(sock,(sockaddr*)&servaddr,sizeof(sockaddr_in))) {
                if(MAX_NTRY < ntry++) {
                    return -1;
                }
                if(ntry > 1 << 8) {
                    ::std::this_thread::yield();
                    // ::std::this_thread::sleep_for(::std::chrono::nanoseconds(1));
                }
            }
            // 
            writeJson(sock,in);
            // 
            if(read) {
                readJson(sock, out);
            }
            close(sock);
        } catch(...) {
            return false;
        }
        return true;
    }

    bool callOnce(rpc::Server& server, rpc::Json request, rpc::Json expect_id, rpc::Json expect_params, std::string expect_method, const ::rpc::Json& expectResponse = ::rpc::Json(nullptr)) {
        bool notification = (request.end() != request.find("id") && request["id"].is_null());
        bool notificationSuccess = true;
        std::atomic_bool notification_end = false;
        if(notification) {
            server.addNotification(expect_method, [&](const rpc::Json& arg) {
                // EXPECT_EQ(params, arg);
                if(expect_params != arg) {
                    notification_end.store(true, ::std::memory_order_acquire);
                }
            });
        } else {
            server.addMethod(expect_method, [&](const rpc::Json& arg)-> rpc::Json {
                // EXPECT_EQ(params, arg);
                if(expect_params != arg) {
                    notificationSuccess = false;
                }
                return arg;
            });
        }
        rpc::Json response;
        if(!tell("127.0.0.1",8080,request, response)) {
            return false;
        }
        if(notification) {
            server.removeNotification(expect_method);
        } else {
            server.removeMethod(expect_method);
        }
        if(notification) {
            while(!notification_end.load(::std::memory_order_release));
            return notificationSuccess;
        }
        // check response
        if(expectResponse.is_null()) {
            ::rpc::Json echoResponse = {
                {"result", expect_params},
                {"id", expect_id}
            };
            if(request.end() != request.find("jsonrpc") && "2.0" == static_cast<std::string>(request["jsonrpc"])) {
                echoResponse["jsonrpc"] = "2.0";
            }
            return response == echoResponse;
        }
        return response == expectResponse;
    }
    
    bool callOnce(rpc::Server& server, const rpc::Json& request, const ::rpc::Json& expectResponse = ::rpc::Json(nullptr)) {
        return callOnce(server, request, request["id"], request["params"], static_cast<std::string>(request["method"]), expectResponse);
    }

    bool callOnce(rpc::Server& server, const rpc::Json& params, bool notification, bool jsonrpc_2_0, const ::rpc::Json& expectResponse = ::rpc::Json(nullptr)) {
        bool success = true;
        rpc::Json request;
        if(jsonrpc_2_0) {
            request["jsonrpc"] = "2.0";
        }
        rpc::Json id;
        if(notification) {
            id = rpc::Json(nullptr);
        } else {
            // int id = rand() % 100;
            if(rand() & 1 == 0) {
                id = rand() % 100;
            } else {
                id = randstr(rand() % 5 + 10);
            }
            request["id"] = rpc::Json(id);
        }
        std::string methodName = randstr(rand() % 5 + 10);
        request["method"] = methodName;
        request["params"] = params;
        success = success && callOnce(server, request, expectResponse);
        return success;
    }

    bool callOnce(rpc::Json request, rpc::Json expect_id, rpc::Json expect_params, std::string expect_method, const ::rpc::Json& expectResponse = ::rpc::Json(nullptr)) {
        rpc::Server server;
        ::std::thread servThread([&]{
            server.start(8080);
        });
        bool success = callOnce(server, request, expect_id, expect_params, expect_method, expectResponse);
        server.close();
        servThread.join();
        return success;
    }

    bool callOnce(const rpc::Json& params, bool notification, bool jsonrpc_2_0, const ::rpc::Json& expectResponse = ::rpc::Json(nullptr)) {
        rpc::Server server;
        ::std::thread servThread([&]{
            server.start(8080);
        });
        bool success = callOnce(server, params, false, true, expectResponse);
        server.close();
        servThread.join();
        return success;
    }

    bool callBatch(const rpc::Server& server, const rpc::Json& request) {
        throw "unrealizerd";
        rpc::Json response;
        bool success = tell("127.0.0.1", 8088, request, response);
        int nMethodCall = 0;
        ::std::map<::rpc::Json, ::rpc::Json> expectResponses;
        for(const rpc::Json& subRequest : request) {
            bool notifaction = 
                subRequest.find("id") == subRequest.end() ||
                subRequest.find("jsonrpc") != subRequest.end() &&
                subRequest["jsonrpc"].is_string() &&
                static_cast<std::string>(subRequest["jsonrpc"]) == "2.0";
            if(notifaction) ++ nMethodCall;
            expectResponses.insert({subRequest["id"], ::rpc::Json {
                {"id", subRequest["id"]},
                {"result", subRequest["result"]}
            }});
        }

        success = success && (nMethodCall == response.size());
        if(!success) {
            return false;
        }
        for(rpc::Json& subResponse : response) {
            
        }
        return success;
    }

    // bool callBatch(const rpc::Server& server, const vector<rpc::Json>& request) {
        
    // }



}

bool testServer1() {
    try{
        rpc::Server server;
        ::std::thread thread([&server]{
            server.start(8080);
        });
        while(!server.is_running()){ ::std::this_thread::yield(); }
        server.close();
        try{
            thread.join();
        } catch(...) {
            return false;
        }
        return true;
    } catch(::std::exception const & ex) {
        ::std::cerr << ex.what() << ::std::endl;
        return false;
    } catch(...) {
        ::std::cerr << "unknown error occurred" << ::std::endl;
        return false;
    }
}

struct Res1 : public ::rpc::Tuple{
    int i;
    ::std::string s;
    Res1() = default;
    Res1(int i, ::std::string s) 
        :i{i}, s{::std::move(s)} {} 
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Res1, i, s)

struct Arg1 : public ::rpc::Tuple {
    int i;
    ::std::string s;
    Arg1() = default;
    Arg1(int i, ::std::string s) 
        :i{i}, s{::std::move(s)} {} 
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Arg1, i, s)

bool testDecoder() {
    rpc::Json j;
    j["i"] = 1;
    j["s"] = "str";
    auto decoder = [] (rpc::Json const & json) -> ::std::shared_ptr<rpc::Tuple>  {
        return ::std::make_shared<Arg1>(json);
    };
    auto ptr = decoder(j);
    if(ptr) {
        Arg1 & arg = dynamic_cast<Arg1 &>(*ptr);
        return 1 == arg.i && "str" == arg.s;
    }
    return false;
}

bool testEncoder() {
    Res1 res {1, "str"};

    auto encoder = [](rpc::Tuple const & tuple) -> rpc::Json {
        return dynamic_cast<Res1 const &>(tuple);
    };
    rpc::Json j = encoder(res);
    return j.is_object() && 1 == static_cast<int>(j["i"]) && "str" == static_cast<std::string>(j["s"]);
}

bool testRegister1() {
    auto decoder = [] (rpc::Json const & json) -> ::std::shared_ptr<rpc::Tuple>  {
        return ::std::make_shared<Arg1>(json);
    };
    auto encoder = [](rpc::Tuple const & tuple) -> rpc::Json {
        return dynamic_cast<Res1 const &>(tuple);
    };
    try {
        ::rpc::Server server;
        server.addMethod(
            "test1",
            [](rpc::Tuple const& arg)-> ::std::shared_ptr<rpc::Tuple> {
                Arg1 const & arg_ = dynamic_cast<Arg1 const &>(arg);
                return ::std::make_shared<Res1>(arg_.i,arg_.s);
            }, decoder,encoder);
        return server.nMethods() == 1;
    } catch(::std::exception const & ex) {
        ::std::cerr << ex.what() << ::std::endl;
        return false;
    } catch(...) {
        ::std::cerr << "unknown error occurred" << ::std::endl;
        return false;
    }
}

bool testRegister2() {
    try {
        ::rpc::Server server;
        server.addMethod<Res1,Arg1>(
            "test1",
            [](rpc::Tuple const& arg)-> ::std::shared_ptr<rpc::Tuple> {
                Arg1 const & arg_ = dynamic_cast<Arg1 const &>(arg);
                return ::std::make_shared<Res1>(arg_.i,arg_.s);
            });
        return server.nMethods() == 1;
    } catch(::std::exception const & ex) {
        ::std::cerr << ex.what() << ::std::endl;
        return false;
    } catch(...) {
        ::std::cerr << "unknown error occurred" << ::std::endl;
        return false;
    }
}

bool testRegister3() {
    try {
        ::rpc::Server server;
        server.addMethod<Res1,Arg1>(
            "test1",
            [](Arg1 const& arg)-> Res1 {
                Arg1 const & arg_ = dynamic_cast<Arg1 const &>(arg);
                return Res1{arg_.i,arg_.s};
            });
        return server.nMethods() == 1;
    } catch(::std::exception const & ex) {
        ::std::cerr << ex.what() << ::std::endl;
        return false;
    } catch(...) {
        ::std::cerr << "unknown error occurred" << ::std::endl;
        return false;
    }
}

TEST(testServer, server1) {
    EXPECT_TRUE(testServer1());
}

TEST(testJson, decoder1) {
    EXPECT_TRUE(testDecoder());
}

TEST(testJson, encoder1) {
    EXPECT_TRUE(testEncoder());
}

TEST(testJson, parse) {
    rpc::Json j {
        {"method", "methodName"},
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"params", {
            {"a", 1},
            {"b",2.0},
            {"c","str"}
        }}
    };
    EXPECT_EQ(static_cast<std::string>(j["method"]), "methodName");
    EXPECT_EQ(static_cast<std::string>(j["jsonrpc"]), "2.0");
    EXPECT_EQ(static_cast<int>(j["id"]), 1);
    EXPECT_TRUE(j["params"].is_object());
    EXPECT_EQ(static_cast<int>(j["params"]["a"]),1);
    EXPECT_EQ(static_cast<int>(j["params"]["b"]),2.0);
    EXPECT_EQ(static_cast<std::string>(j["params"]["c"]),"str");
}

TEST(testRegester, regesterMethod1) {
    EXPECT_TRUE(testRegister1());
}

TEST(testRegester, regesterMethod2) {
    EXPECT_TRUE(testRegister2());
}

TEST(testRegester, regesterMethod3) { 
    EXPECT_TRUE(testRegister3());
}

TEST(testMethodCall, trival) {
    rpc::Json request {
        {"jsonrpc","2.0"},
        {"method", "singleMethodCall1"},
        {"params", {
            {"a", 1},
            {"b", 2}
        }},
        {"id", "4"}
    };
    rpc::Server server;
    server.addMethod("singleMethodCall1",[](const rpc::Json& arg)->rpc::Json{
        rpc::Json expect {
            {"a", 1},
            {"b", 2}
        };
        EXPECT_EQ(expect,arg);
        return arg;
    });
    ::std::thread servThread([&]() {
        server.start(8080);
    });
    rpc::Json response;
    EXPECT_TRUE(tell("127.0.0.1", 8080, request,response));
    rpc::Json expectResponse {
        {"jsonrpc", "2.0"},
        {"result", {
            {"a", 1},
            {"b", 2}
        }},
        {"id","4"}
    };
    EXPECT_EQ(response, expectResponse);
    server.close();
    servThread.join();
}
TEST(testMethodCall, singleMethodCall1) {
    rpc::Json params {
        {"a", 1},
        {"b", "2"}
    };
    EXPECT_TRUE(callOnce(params, false, true));
}

TEST(testMethodCall, singleMethodCallFailure1) {
    rpc::Json params = "this is params";
    EXPECT_FALSE(callOnce(params, false, true));
}


TEST(testMethodCall, singleMethodCallFailure2) {
    rpc::Json params = 123456;
    EXPECT_FALSE(callOnce(params, false, true));
}

TEST(testMethodCall, singleMethodCallFailure3) {
    rpc::Json params(nullptr);
    EXPECT_FALSE(callOnce(params, false, true));
}

TEST(testMethodCall, singleMethodCallFailure4) {
    // using rpc::Json;
    rpc::Json params = rpc::Json::array({rpc::Json::object({{"a","1"}}), rpc::Json::object({{"b",1}})});
    EXPECT_FALSE(callOnce(params, false, true));
}

TEST(testMethodCall, singleMethodCallFailureMethod1) {
    rpc::Json params { {"a", "a"}, {"b", "b"}};
    rpc::Json id = "123";
    std::string expectMethod = "methodName";
    std::string realMethod = "????";
    rpc::Json request {
        {"params", params},
        {"id", id},
        {"method", realMethod}
    };
    EXPECT_FALSE(callOnce(request, id, params, expectMethod));
}

TEST(testMethodCall, singleMethodCallFailureId1) {
    rpc::Json params { {"a", "a"}, {"b", "b"}};
    rpc::Json expectId = "123";
    rpc::Json realId {"1"};
    std::string method = "methodName";
    rpc::Json request {
        {"params", params},
        {"id", realId},
        {"method", method}
    };
    EXPECT_FALSE(callOnce(request, expectId, params, method));
}

TEST(testMethodCall, singleMethodCallFailureId2) {
    rpc::Json params { {"a", "a"}, {"b", "b"}};
    rpc::Json expectId = "123";
    std::string method = "methodName";
    rpc::Json request {
        {"params", params},
        {"method", method}
    };
    EXPECT_FALSE(callOnce(request, expectId, params, method));
}

TEST(testNotification, singleNotificationCall1) {
    rpc::Json params {
        {"a", 1},
        {"b", "2"}
    };
    EXPECT_TRUE(callOnce(params, true, true));
}

TEST(testNotification, singleNotificationFailure1) {
    rpc::Json params = "this is a invalid params";
    // EXPECT_FALSE(callOnce(params, true, true));
    EXPECT_TRUE(callOnce(params, true, true, BadRequest_2_0));
}


TEST(testNotification, singleNotificationFailure2) {
    rpc::Json params = 123456;
    // EXPECT_FALSE(callOnce(params, true, true));
    EXPECT_TRUE(callOnce(params, true, true, BadRequest_2_0));
}

TEST(testNotification, singleNotificationFailure3) {
    rpc::Json params(nullptr);
    // EXPECT_FALSE(callOnce(params, true, true));
    EXPECT_TRUE(callOnce(params, true, true, BadRequest_2_0));
}

TEST(testNotification, singleNotificationFailure4) {
    rpc::Json params = rpc::Json::array({rpc::Json::object({{"a","1"}}), rpc::Json::object({{"b",1}})});
    // EXPECT_FALSE(callOnce(params, true, true));
    EXPECT_TRUE(callOnce(params, true, true, BadRequest_2_0));
}

TEST(testMethodCall, longRequest) {
    static constexpr int len = 1 << 20; // 1M
    std::string s = randstr(len);
    s[0] = '{';
    s[len-1] = '}';
    rpc::Json params {{"str", s}};
    auto buf = ::std::cout.rdbuf();
    ::std::cout.rdbuf(nullptr);
    EXPECT_TRUE(callOnce(params,false,true));
    ::std::cout.rdbuf(buf);
}

TEST(testMethodCall, emptyRequest) {
    // EXPECT_FALSE(callOnce({},false,true));
    EXPECT_TRUE(callOnce({},false,true, BadRequest_2_0));
}

TEST(testMethodCall, thickObject) {
    // static constexpr int len = 6 * 1 << 23; // Number of plies: 4M test OK
    static constexpr int len = 6 * 1 << 18; // Number of plies: 128K
    std::string s(len,'}');
    int i = 0;
    while(i < len / 6 * 5) {
        s[i++] = '{';
        s[i++] = '\"';
        s[i++] = 'O';
        s[i++] = '\"';
        s[i++] = ':';
    }
    rpc::Json params {{"str", s}};
    auto buf = ::std::cout.rdbuf();
    ::std::cout.rdbuf(nullptr);
    EXPECT_TRUE(callOnce(params,false,true));
    ::std::cout.rdbuf(buf);
}

TEST(testBatch, batchTrival1) { // multiRequest
    rpc::Json&& request = R"(
[
    {
        "jsonrpc":"2.0",
        "id":"1",
        "method":"echo",
        "params":{
            "a":123,
            "b": 2,
            "c": {
                "d":5
            }
        }
    },
    {
        "jsonrpc":"2.0",
        "id":null,
        "method":"echo",
        "params":{}
    },
    {
        "jsonrpc":"2.0",
        "id":2,
        "method":"echo",
        "params":{}
    },
    {
        "jsonrpc":"2.0",
        "method":"echo",
        "params":{
            "e":{}
        }
    },
    {
        "jsonrpc":"2.0",
        "method":"echo",
        "id":4,
        "params":"f"
    }
]
    )"_json;
    rpc::Json expectResponse = R"(
[
    {
        "jsonrpc":"2.0",
        "id":"1",
        "result":{
            "a":123,
            "b": 2,
            "c": {
                "d":5
            }
        }
    },
    {
        "jsonrpc":"2.0",
        "id":null,
        "result":{}
    },
    {
        "jsonrpc":"2.0",
        "id":2,
        "result":{}
    },
    {
        "jsonrpc":"2.0",
        "id":null,
        "error": {
            "code":-32600,
            "message":"Invalid Request"
        }
    }
]
    )"_json;
    ::rpc::Server server;
    ::std::thread thread([&]{
        server.start(8080);
    });
    server.addMethod("echo",[](const rpc::Json& arg)-> rpc::Json {
        return arg;
    });
    rpc::Json response;
    while(!server.is_running());
    tell("127.0.0.1",8080,request, response);
    EXPECT_EQ(response, expectResponse);
    server.close();
    thread.join();
    // EXPECT_TRUE(response == expectResponse);
}

TEST(testBatch, batchEmpty1) { // multiRequest
    rpc::Json&& request = R"(
[
    {
        "jsonrpc":"2.0",
        "method":"echo",
        "params": {
            "e": "2"
        }
    },
    {}
]
    )"_json;
    rpc::Json expectResponse(nullptr);
    ::rpc::Server server;
    ::std::thread thread([&]{
        server.start(8080);
    });
    server.addMethod("echo",[](const rpc::Json& arg)-> rpc::Json {
        return arg;
    });
    rpc::Json response;
    while(!server.is_running());
    tell("127.0.0.1",8080,request, response);
    EXPECT_EQ(response, expectResponse);
    server.close();
    thread.join();
}

TEST(testBatch, batchEmpty2) { // multiRequest
    rpc::Json request = R"([])"_json;
    rpc::Json expectResponse = R"(
{
    "jsonrpc":"2.0",
    "id":null,
    "error": {
        "code":-32600,
        "message":"Invalid Request"
    }
}
    )"_json;
    ::rpc::Server server;
    ::std::thread thread([&]{
        server.start(8080);
    });
    server.addMethod("echo",[](const rpc::Json& arg)-> rpc::Json {
        return arg;
    });
    rpc::Json response;
    while(!server.is_running());
    tell("127.0.0.1",8080,request, response);
    EXPECT_EQ(response, expectResponse);
    server.close();
    thread.join();
}