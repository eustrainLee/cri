#ifndef JSONRPC_HTTP
#define JSONRPC_HTTP

#include "httplib.hpp"
#include "json.hpp"
// This is an headonly http-based version of jsonrpc1.0 only
namespace rpc {
    using JsonMethodFunc = ::std::function<nlohmann::json(nlohmann::json const&)>;
    using JsonNotificationFunc = ::std::function<void(nlohmann::json const&)>;
    // bind a jsonrpc1.0 server to a httplib::Server object.
    void bindServer(
        httplib::Server& server,
        ::std::map<::std::string, rpc::JsonMethodFunc> methodMap = {},
        ::std::map<::std::string, rpc::JsonNotificationFunc> notificationMap = {},
        const ::std::string& path = "/jsonrpc") {
        static auto makeResponse = [](nlohmann::json result, nlohmann::json error, nlohmann::json id) -> nlohmann::json{
            return { {"result", ::std::move(result)}, {"error", ::std::move(error)}, {"id", ::std::move(id)} };
        };
        static auto doMethodCall = [](
            nlohmann::json request,
            ::std::map<::std::string, rpc::JsonMethodFunc> methodMap = {})
            noexcept -> nlohmann::json {
            auto idIter = request.find("id");
            auto methodIter = request.find("method");
            auto paramsIter = request.find("params");
            if(request.end() == methodIter || request.end() == paramsIter || !paramsIter->is_array()) {
                ::std::clog << "Bad Request id=" + idIter->dump() << ::std::endl;
                return makeResponse(nullptr, "Bad Request",*idIter);
            }
            try{
                auto callMethodIter = methodMap.find(*methodIter);
                if(methodMap.end() == callMethodIter) { return makeResponse(nullptr, "Method Not Found",*idIter); }
                return makeResponse(callMethodIter->second(*paramsIter), nullptr, *idIter);
            } catch(::std::exception e) {
                ::std::clog <<  ::std::string("Exception:") + e.what() + " id=" + idIter->dump() << ::std::endl;
                return makeResponse(nullptr, ::std::string("Exception:") + e.what(), *idIter);
            } catch(...) {
                ::std::clog << "Unknown Exception id=" + idIter->dump() << ::std::endl;
                return makeResponse(nullptr, "Unknown Exception", *idIter);
            }
        };
        static auto doNotification = [](nlohmann::json request,
            ::std::map<::std::string, rpc::JsonMethodFunc> methodMap = {},
            ::std::map<::std::string, rpc::JsonNotificationFunc> notificationMap = {})
            noexcept -> void {
            auto methodIter = request.find("method");
            auto paramsIter = request.find("params");
            if(request.end() == methodIter || request.end() == paramsIter || !request.is_array()) { /*do_nothing*/ return; }
            try{
                if(auto nfuncIter = notificationMap.find(*methodIter); notificationMap.end() != nfuncIter) { nfuncIter->second(*paramsIter); }
                else if(auto mfuncIter = methodMap.find(*methodIter); methodMap.end() != mfuncIter) { mfuncIter->second(*paramsIter); }
            } catch(...) { /* do nothing */ }
        };
        server.Post(path, [&server, methodMap=::std::move(methodMap), notificationMap=::std::move(notificationMap)](const httplib::Request & request, httplib::Response & response) {
            nlohmann::json jRequest;
            try {
                ::std::cout << "received:" << request.body << ::std::endl;
                jRequest = nlohmann::json::parse(request.body);
            } catch(...) {
                response.set_content(makeResponse(nullptr,"Bad Request",nullptr).dump(), "text/plain");
            }
            if(auto idIter = jRequest.find("id"); jRequest.end() == idIter) { response.set_content(makeResponse(nullptr,"Bad Request",nullptr).dump(), "text/plain"); }
            else if(idIter->is_null()) { doNotification(::std::move(jRequest), methodMap, notificationMap); }
            else response.set_content(doMethodCall(::std::move(jRequest), methodMap).dump(), "text/plain");
        });
    }
    // check result if it returned true, check error if not.
    bool call(const ::std::string& host, int port, nlohmann::json params, nlohmann::json& result, nlohmann::json& error, ::std::string method, bool notification = false, const char* path = "/jsonrpc") {
        static std::atomic_int64_t id = 0;
        if(path == nullptr || host.empty() || port > 65525 || port < 0 || method.empty() || !params.is_array()) {
            error = "Bad Request";
            return false;
        } // pre check
        httplib::Client client(host, port);
        nlohmann::json jRequest {
            {"id", (notification ? nlohmann::json(nullptr) : nlohmann::json(id++))},
            {"method", std::move(method)},
            {"params", std::move(params)}
        };
        httplib::Result hResult = client.Post(path, jRequest.dump(), "text/plain"); // Send request and get Response
        client.stop(); // close the socket
        if(notification) { return true; }
        if(!(hResult)) {
            error = "Bad Connection";
            return false;
        }
        if(200 != hResult->status) {
            error = "HTTP " + std::to_string(hResult->status);
            return false;
        }
        try {
            nlohmann::json jResponse = nlohmann::json::parse(hResult->body);
            auto errorIter = jResponse.find("error"), resultIter = jResponse.find("result");
            if(jResponse.end() == errorIter || jResponse.end() == resultIter || jResponse.end() == jResponse.find("id")) { error = "Bad Response"; }
            if(!errorIter->is_null()) { error = *errorIter; }
            else if(!resultIter->is_null()  ) { result = *resultIter; }
            else { error = "Bad Response"; }
            return error.is_null();
        } catch(...) {
            error = "Bad Response";
            return false;
        }
    }
}

#endif