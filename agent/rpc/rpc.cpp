#include "rpc.hpp"
#include <sstream>
#include <experimental/net>

// TODO 添加日志功能

namespace rpc {
    static const struct placeholder_t {} placeholder;
    static const ::std::string jsonrpcLabel = "jsonrpc";
    static const ::std::string idLabel = "id";
    static const ::std::string methodLabel = "method";
    static const ::std::string paramsLabel = "params";
    static const ::std::string jsonrpc_2_0 = "2.0";
    namespace { // not export
        namespace detail {
            class tcpbuf : public std::streambuf {
            public:
                enum { BUFSIZE = 1 << 10 };
                tcpbuf(httplib::Stream& strm) 
                    :strm(strm){ }
                ~tcpbuf() override {
                    // close();
                }

            protected:
                // Buffered get
                int underflow() override {
                    // auto n = ::recv(sock, buf, BUFSIZE, 0);
                    // auto n = ::read(sock, buf, BUFSIZE);
                    auto n = strm.read(buf,BUFSIZE);
                    return n > 0 ? (setg(buf, buf, buf + n), *gptr()) : ::std::char_traits<char>::eof();
                }
                // Unbuffered put
                int overflow(int c) override {
                    if (c == EOF)
                        // return close();
                        return -1;
                    char b = c;
                    char s[2];
                    s[0] = b;
                    s[1] = '\0';
                    return strm.write(s) > 0 ? c : ::std::char_traits<char>::eof();
                    // return ::send(sock, &b, 1, 0) > 0 ? c : ::std::char_traits<char>::eof;
                }
            private:
                httplib::Stream& strm;
                char buf[BUFSIZE];
            };
            // 处理一条网络连接上的rpc请求
            bool processRpc(
                httplib::Stream &strm,
                bool close_connection,
                bool &connection_closed
            );

            //   当writeJson返回false时需要检查error
            bool writeJson(
                httplib::Stream &strm,
                bool close_connection,
                const Json& json,
                Error& error
            );

            //   当readJson返回false时需要检查error
            bool readJson(
                httplib::Stream &strm,
                Json& json
            );

            // 仅根据错误类型(enum)，自动生成错误对象(JSON)
            inline Json makeErrorObject(
                Error error
            );

            // 仅根据错误码(int)，自动生成错误对象(JSON)
            inline Json makeErrorObject(
                int code
            );

            // 构建错误对象
            inline Json makeErrorObject(
                int code,
                const ::std::string& message,
                Json data
            );

            inline Json makeErrorObject(
                int code,
                const ::std::string& message
            );

            // 构建请求对象
            inline Json makeRequestObject(
                ::std::string method,
                Json params,
                Json id
            );

            inline Json makeResponseObject_1_0(
                placeholder_t,
                Json error,
                Json id
            );

            inline Json makeResponseObject_1_0(
                Json result,
                placeholder_t,
                Json id
            );

            //  构建相应对象
            inline Json makeResponseObject_2_0(
                placeholder_t,
                Json error,
                Json id
            );

            // 构建相应对象
            inline Json makeResponseObject_2_0(
                Json result,
                placeholder_t,
                Json id
            );

            // 判断是否为jsonrpc 1.0 的请求对象
            bool is_jsonrpc_1_0_request(const Json& request) noexcept;

            // 判断是否为jsonrpc 2.0 的请求对象
            bool is_jsonrpc_2_0_request(const Json& request) noexcept;

            // TODO move write and read to class Server // why? I forgot the reason it should do it.
            bool writeJson(
                httplib::Stream &strm,
                const Json& json
            ) {
                // TODO erase log message
                ::std::cout << "log[Server Write Json]: " << json.dump() << ::std::endl;
                tcpbuf buf(strm);
                ::std::ostream stream(&buf);
                try{ 
                    stream << json;
                } catch(...) {
                    return false;
                }
                return true;
            }

            bool readJson(
                httplib::Stream &strm,
                Json& json
            ) {
                //sock_iadapter iadapter(strm.socket());
                //configor::iadapterstream is {iadapter};
                tcpbuf buf(strm);
                ::std::istream stream(&buf);
                try{
                    //json = json::parse(is);
                    stream >> json;
                } catch(const ::std::exception& ex) {
                    ::std::cerr << ex.what() << ::std::endl;
                    ::std::cerr << "error[Server Read Json]" << json.dump() << ::std::endl;
                    return false;
                }
                // TODO erase log message.
                ::std::cout << "log[Server Read Json]: " << json.dump() << ::std::endl;
                return true;
            }

            Json makeErrorObject(
                Error error
            ) {
                return makeErrorObject(static_cast<int>(error));
            }

            Json makeErrorObject(
                int code
            ) {
                switch(code) {
                case -32700:
                    return makeErrorObject(code, "Parse error");
                case -32600:
                    return makeErrorObject(code, "Invalid Request");
                case -32601:
                    return makeErrorObject(code, "Method not found");
                case -32602:
                    return makeErrorObject(code, "Invalid params");
                case -32603:
                    return makeErrorObject(code, "Internal error");
                default:
                    if(code >= -32099 && code <= -32000)
                        return makeErrorObject(code, "Server error");
                    return makeErrorObject(code, "Undefined error");
                    
                }
            }

            Json makeErrorObject(
                int code,
                const ::std::string& message
            ) {
                return Json {
                    {"code", code},
                    {"message", message}
                };
            }


            Json makeErrorObject(
                int code,
                const ::std::string& message,
                Json data
            ) {
                // 
                return Json {
                    {"code", code},
                    {"message", message},
                    {"data", data}
                };
            }

            inline Json makeResponseObject_1_0(
                placeholder_t,
                Json error,
                Json id
            ) {
                return Json {
                    {"result", Json(nullptr)},
                    {"error", error},
                    {"id", id}
                };
            }

            inline Json makeResponseObject_1_0(
                Json result,
                placeholder_t,
                Json id
            ) {
                return Json {
                    {"result", result},
                    {"error", Json(nullptr)},
                    {"id", id}
                };
            }

            //  构建相应对象
            inline Json makeResponseObject_2_0(
                placeholder_t,
                Json error,
                Json id
            ) {
                return Json {
                    {jsonrpcLabel, jsonrpc_2_0},
                    {"error", error},
                    {"id", id}
                };
            }

            // 构建相应对象
            inline Json makeResponseObject_2_0(
                Json result,
                placeholder_t,
                Json id
            ) {
                return Json {
                    {jsonrpcLabel, jsonrpc_2_0},
                    {"result", result},
                    {"id", id}
                };
            }

            // 判断是否为jsonrpc 1.0 的请求对象
            bool is_jsonrpc_1_0_request(const Json& request) noexcept {
                if(request.end() == request.find(methodLabel)) { return false; }
                if(!request[methodLabel].is_string()) { return false; }
                if(request.end() == request.find(paramsLabel)) { return false; }
                if(!request[paramsLabel].is_array()) { return false; }
                // for(const ::rpc::Json& subObj : request[paramsLabel]) {
                //     if(!subObj.is_object()) { return false; }
                // }
                if(request.end() == request.find(idLabel)) { return false;} // id must exist
                // if(request[idLabel].is_null()) { return false; }
                return true;
            }
            
            // 判断是否为jsonrpc 2.0 的请求对象
            bool is_jsonrpc_2_0_request(const Json& request) noexcept {
                if(request.end() == request.find(methodLabel)) { return false; }
                if(!request[methodLabel].is_string()) { return false; }
                if(request.end() == request.find(paramsLabel)) { return false; }
                if(!request[paramsLabel].is_object()) { return false; }
                // do not check id, if it is not exist, it is a notification, otherwise, it a method call // this words is worry
                auto idIter = request.find(idLabel);
                if(request.end() != idIter) { // id exist
                    if(!(idIter->is_number() || idIter->is_string() || idIter->is_null())) {
                        return false;
                    }
                }
                const auto jsonRpcIter = request.find(jsonrpcLabel);
                if(request.end() == jsonRpcIter) { return false; } // jsonrpc must exist and it is equal as "2.0"
                const Json& jsonrpc = *jsonRpcIter;
                if(!jsonrpc.is_string()) { return false; }
                if(jsonrpc_2_0 != static_cast<const std::string>(jsonrpc)) { return false; }
                return true;
            }

        } // namespace detail
    }

    bool Server::process_and_close_socket(socket_t sock) {
        bool ret = false;
        // do_something
        ret = httplib::detail::process_server_socket(
            svr_sock_, sock, keep_alive_max_count_, keep_alive_timeout_sec_,
            read_timeout_sec_, read_timeout_usec_, write_timeout_sec_,
            write_timeout_usec_,
            [this](httplib::Stream &strm, bool close_connection, bool &connection_closed) {
                return processRpc(strm, close_connection, connection_closed);
        });
        httplib::detail::shutdown_socket(sock);
        httplib::detail::close_socket(sock);
        return ret;
    }

    bool Server::processRpc(
        httplib::Stream &strm,
        bool close_connection, // true
        bool &connection_closed // we need set the variable ture
    ) {
        // read jsonObject and process it
        Json request;
        try {
            if(!detail::readJson(strm,request)) {
                ::std::cerr << "[Internal1]" << ::std::endl;
                detail::writeJson(
                    strm,
                    detail::makeResponseObject_2_0(
                        placeholder,
                        detail::makeErrorObject(Error::Internal),
                        Json(nullptr)
                    )
                );
                return false;
            }
        } catch(...) {
            detail::writeJson(
                strm,
                detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Parse),
                    Json(nullptr)
                )
            );
            return false;
        }
        // is array?
        if(request.is_array()) {
            if(request.empty()) {
                detail::writeJson(
                    strm,
                    detail::makeResponseObject_2_0(
                        placeholder,
                        detail::makeErrorObject(Error::Request),
                        Json(nullptr)
                    )
                );
                return false;
            }
            ::std::list<Json> responses;
            ::std::list<::std::thread> methodThreads;
            ::std::list<::std::thread> notificationThreads;
            int nbad = 0;
            for(Json& subRequest : request) {
                if(subRequest.end() != subRequest.find(jsonrpcLabel)) {
                    if(jsonrpc_2_0 == subRequest[jsonrpcLabel]) {
                        // JSON-RPC 2.0
                        if(subRequest.end() != subRequest.find(idLabel)) {
                            // method call
                            responses.emplace_back();
                            methodThreads.emplace_back(
                                &Server::execMethod_2_0,
                                this,
                                ::std::move(subRequest),
                                ::std::ref(*responses.rbegin())
                            );
                        } else {
                            // notification
                            notificationThreads.emplace_back(
                                [this,request=::std::move(subRequest)] {
                                    this->execNotification_2_0(
                                        ::std::move(request)
                                    );
                                }
                            );
                        }

                    } else {
                        // Unknown JSON-RPC version
                        responses.emplace_back(
                            detail::makeResponseObject_2_0(
                                placeholder,
                                detail::makeErrorObject(Error::Request),
                                ((subRequest.end() != subRequest.find("id")) ? 
                                    subRequest["id"] : 
                                    Json(nullptr))
                            )
                        );
                        ++nbad;
                    }
                } else {
                    // JSON-RPC 1.0
                    if(subRequest.end() != subRequest.find(idLabel)) {
                        // method call
                        responses.emplace_back();
                        methodThreads.emplace_back(
                            &Server::execMethod_1_0,
                            this,
                            ::std::ref(subRequest),
                            ::std::ref(*responses.rbegin())
                        );
                    } else {
                        // notification
                        notificationThreads.emplace_back(
                            [this,request=::std::move(subRequest)] {
                                this->execNotification_1_0(request);
                            }
                        );
                    }

                }
            }
            for(auto& thread : notificationThreads) {
                thread.detach();
            }
            for(auto& thread : methodThreads) {
                thread.join();
            }
            if(!responses.empty()) {
                Json response(responses);
                if(!response.is_array()) {
                ::std::cerr << "[Internal2]" << ::std::endl;
                    detail::writeJson(
                        strm,
                        detail::makeResponseObject_2_0(
                            placeholder,
                            detail::makeErrorObject(Error::Internal),
                            Json(nullptr)
                        )
                    );
                    return false;
                }
                detail::writeJson(strm,response);
                if(request.size() == nbad) {
                    return false;
                }
            } else {
                // do_nothing
            }
        } else if(request.is_object()) {
            if(request.end() != request.find(jsonrpcLabel)) {
                if(jsonrpc_2_0 == request[jsonrpcLabel]) {
                    // JSON-RPC 2.0
                    if(request.end() != request.find(idLabel)) {
                        Json response;
                        this->execMethod_2_0(
                            request,
                            response
                        );
                        detail::writeJson(
                            strm,
                            response
                        );
                    } else {
                        this->execNotification_2_0(
                            request
                        );
                    }
                } else {
                    // Unknown JSON-RPC version
                    detail::writeJson(
                        strm,
                        detail::makeResponseObject_2_0(
                            placeholder,
                            detail::makeErrorObject(Error::Request),
                            Json(nullptr)
                        )
                    );
                    return false;
                }
            } else {
                // JSON-RPC 1.0
                if(request.end() != request.find(idLabel)) {
                    Json response;
                    this->execMethod_1_0(
                        request,
                        response
                    );
                    detail::writeJson(
                        strm,
                        response
                    );
                } else {
                    this->execNotification_1_0(
                        request
                    );
                }
            }

        } else {
            detail::writeJson(
                strm,
                detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Request),
                    Json(nullptr)
                )
            );
            return false;
        }
        // All successful branches will be executed here.
        return true;
    }



    //  执行调用
    bool Server::execMethod_1_0(Json request, Json& response) noexcept {
        try {
            if(!detail::is_jsonrpc_1_0_request(request)) {
                response = detail::makeResponseObject_1_0(
                    placeholder,
                    detail::makeErrorObject(Error::Request),
                    Json(nullptr) // must be null
                );
                return false;
            }
            auto methodIter = request.find(methodLabel);
            auto paramsIter = request.find(paramsLabel);
            auto idIter = request.find(idLabel);
            // check method call
            if(
                request.end() == idIter // exist and may be any type
            ) {
                // should be a notification
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Internal), // a notification should not executing to here
                    Json(nullptr) // must be null
                );
                return false;
            }
            ::std::unique_lock ulk(this->methodMutex);
            auto funcIter = this->methodMap.find(static_cast<std::string>(*methodIter));
            if(this->methodMap.end() == funcIter) { // not found
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Method),
                    ::std::move(*idIter)
                );
                return false;
            }
            MethodFunc& method = funcIter->second;
            ulk.unlock();
            try {
                Json result = method(*paramsIter);
                response = detail::makeResponseObject_2_0(
                    ::std::move(result),
                    placeholder,
                    ::std::move(*idIter)
                );
                return true;
            } catch(const ::std::exception& e) {
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(static_cast<int>(Error::Exception),e.what()),
                    ::std::move(*idIter)
                );
                return false;
            }
        } catch (...) {
                ::std::cerr << "[Internal4]" << ::std::endl;
            response = detail::makeResponseObject_2_0(
                placeholder,
                detail::makeErrorObject(Error::Internal),
                request.end() != request.find(idLabel) ? ::std::move(request[idLabel]) : Json(nullptr)
            );
            return false;
        }
        return true;
    }
    //  执行通知
    bool Server::execNotification_1_0(Json request) noexcept {
        try {
            if(!detail::is_jsonrpc_1_0_request(request)) {
                return false;
            }
            auto methodIter = request.find(methodLabel);
            auto paramsIter = request.find(paramsLabel);
            auto idIter = request.find(idLabel);
            if(request.end() != idIter) {
                return false;
            }
            ::std::unique_lock n_ulk(this->notificationMutex);
            auto funcIter = this->notificationMap.find(static_cast<std::string>(*methodIter));
            if(this->notificationMap.end() == funcIter) {
                n_ulk.unlock();
                ::std::unique_lock m_ulk(this->methodMutex);
                auto funcIter = this->methodMap.find(static_cast<std::string>(*methodIter));
                if(this->methodMap.end() == funcIter) {
                    return false;
                } else {
                    MethodFunc& method = funcIter->second;
                    m_ulk.unlock();
                    try {
                        method(*paramsIter);
                    } catch(::std::exception e) {
                        return false;
                    }
                }
            } else {
                NotificationFunc method = funcIter->second;
                n_ulk.unlock();
                try {
                    method(*paramsIter);
                } catch(...) {
                    return false;
                }   
            }
        } catch (...) {
                ::std::cerr << "[Internal5]" << ::std::endl;
            detail::makeResponseObject_2_0(
                placeholder,
                detail::makeErrorObject(Error::Internal),
                request.end() != request.find(idLabel) ? ::std::move(request[idLabel]) : Json(nullptr)
            );
            return false;
        }
        return true;
    }
    //  执行调用
    bool Server::execMethod_2_0(Json request, Json& response) noexcept {
        try {
            if(!detail::is_jsonrpc_2_0_request(request)) {
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Request),
                    Json(nullptr) // must be null
                );
                return false;
            }
            auto methodIter = request.find(methodLabel);
            auto paramsIter = request.find(paramsLabel);
            auto idIter = request.find(idLabel);
            // check method call
            if(
                request.end() == idIter ||
                !(idIter->is_null() || idIter->is_number() || idIter->is_string())
            ) {
                ::std::cerr << "[Internal6]" << ::std::endl;
                // should be a notification
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Internal),
                    Json(nullptr) // must be null
                );
                return false;
            }
            ::std::unique_lock ulk(this->methodMutex);
            auto funcIter = this->methodMap.find(static_cast<std::string>(*methodIter));
            if(this->methodMap.end() == funcIter) { // not found
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(Error::Method),
                    ::std::move(*idIter)
                );
                return false;
            }
            MethodFunc& method = funcIter->second;
            ulk.unlock();
            try {
                Json result = method(*paramsIter);
                response = detail::makeResponseObject_2_0(
                    ::std::move(result),
                    placeholder,
                    ::std::move(*idIter)
                );
                return true;
            } catch(const ::std::exception& e) {
                response = detail::makeResponseObject_2_0(
                    placeholder,
                    detail::makeErrorObject(static_cast<int>(Error::Exception),e.what()),
                    ::std::move(*idIter)
                );
                return false;
            }
        } catch (...) {
                ::std::cerr << "[Internal7]" << ::std::endl;
            response = detail::makeResponseObject_2_0(
                placeholder,
                detail::makeErrorObject(Error::Internal),
                request.end() != request.find(idLabel) ? ::std::move(request[idLabel]) : Json(nullptr)
            );
            return false;
        }
        return true;
    }
    //  执行通知
    bool Server::execNotification_2_0(Json request) noexcept {
        try {
            if(!detail::is_jsonrpc_2_0_request(request)) {
                return false;
            }
            auto methodIter = request.find(methodLabel);
            auto paramsIter = request.find(paramsLabel);
            auto idIter = request.find(idLabel);
            if(request.end() != idIter) {
                return false;
            }
            ::std::unique_lock n_ulk(this->notificationMutex);
            auto funcIter = this->notificationMap.find(static_cast<std::string>(*methodIter));
            if(this->notificationMap.end() == funcIter) {
                n_ulk.unlock();
                ::std::unique_lock m_ulk(this->methodMutex);
                auto funcIter = this->methodMap.find(static_cast<std::string>(*methodIter));
                if(this->methodMap.end() == funcIter) {
                    return false;
                } else {
                    MethodFunc& method = funcIter->second;
                    m_ulk.unlock();
                    try {
                        method(*paramsIter);
                    } catch(::std::exception e) {
                        return false;
                    }
                }
            } else {
                NotificationFunc method = funcIter->second;
                n_ulk.unlock();
                try {
                    method(*paramsIter);
                } catch(...) {
                    return false;
                }   
            }
        } catch (...) {
                ::std::cerr << "[Internal8]" << ::std::endl;
            detail::makeResponseObject_2_0(
                placeholder,
                detail::makeErrorObject(Error::Internal),
                request.end() != request.find(idLabel) ? ::std::move(request[idLabel]) : Json(nullptr)
            );
            return false;
        }
        return true;
    }
    
    // Tuple
    Tuple::~Tuple() {
        // do nothing
    }

    Server::Server() {
        this->set_keep_alive_max_count(1);
    }
        

    // 成员函数定义

    // 方法调用函数

    bool Server::addMethod (
        ::std::string name,
        ::rpc::MethodFunc func,
        ::rpc::Decoder decoder,
        ::rpc::Encoder encoder
        ) & {
        ::std::lock_guard<Server::Mutex> {this->methodMutex};
        if(1 == this->methodMap.count(name)) { // 重复
            return false;
        }
        Server::MethodFunc callFunc {[
            func=::std::move(func),
            encoder=::std::move(encoder),
            decoder=::std::move(decoder)
            ](Json const & argJson) -> Json {
            auto argPtr = decoder(argJson);
            auto resPtr = func(*argPtr);
            auto resJson = encoder(*resPtr);
            return resJson;
        }};
        bool success {this->methodMap.emplace(name, ::std::move(callFunc)).second};
        return success;
    }

    // these are defined at the trail of file "rpc.hpp"

    // template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::configor::json>::value,int>::type>
    // bool Server::addMethod(
    //     ::std::string name,
    //     ::rpc::MethodFunc func) &;

    // template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::configor::json>::value,int>::type>
    // bool Server::addMethod(
    //     ::std::string name,
    //     MateMethodFunc<Res,Arg> func) &;

    bool Server::addMethod (
            ::std::string name,
            JsonMethodFunc func) & {
        if(1 == this->methodMap.count(name)) {
            return false;
        }
        bool success {this->methodMap.emplace(name, ::std::move(func)).second};
        return success;
    }

    // 在Server中移除有指定名称的method call函数
    bool Server::removeMethod(::std::string const &name) {
        ::std::lock_guard<Server::Mutex> {this->methodMutex};
        bool number = this->methodMap.erase(name);
        return 0 != number;
    }

    void Server::removeAllMethods() noexcept {
        ::std::lock_guard<Server::Mutex> {this->methodMutex};
        this->methodMap.clear();
    }
    size_t Server::nMethods() {
        ::std::lock_guard<Mutex>{this->methodMutex};
        return this->methodMap.size();
    }

    bool Server::addNotification(
        ::std::string name,
        ::rpc::NotificationFunc func,
        ::rpc::Decoder decoder,
        ::rpc::Encoder encoder
    ) & {
        ::std::lock_guard<Server::Mutex> { this->notificationMutex };
        if(1 == this->methodMap.count(name)) { // 重复
            return false;
        }
        Server::NotificationFunc notificationFunc {[
            func=::std::move(func),
            encoder=::std::move(encoder),
            decoder=::std::move(decoder)
            ](Json const & argJson) {
                auto argPtr = decoder(argJson);
                func(*argPtr);
        }};
        bool success {this->notificationMap.emplace(name, ::std::move(notificationFunc)).second};
        return success;
    }

    // these are defined at the trail of file "rpc.hpp"    

    // template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::configor::json>::value,int>::type = 0>
    // bool addNotification(
    //     ::std::string name,
    //     ::rpc::NotificationFunc func
    // ) &;

    // template<typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::configor::json>::value,int>::type = 0>
    // bool addNotification(
    //     ::std::string name,
    //     ::rpc::MateNotificationFunc<Arg> func
    // ) &;

    bool Server::addNotification (
        ::std::string name,
        ::rpc::JsonNotificationFunc func
    ) & {
        ::std::lock_guard<Server::Mutex> { this->notificationMutex };
        if(1 == this->methodMap.count(name)) {
            return false;
        }
        bool success {this->notificationMap.emplace(name, ::std::move(func)).second};
        return success;
    }

    bool Server::removeNotification(::std::string const& name) {
        ::std::lock_guard<Server::Mutex> {this->notificationMutex};
        bool number = this->notificationMap.erase(name);
        return 0 != number;
    }

    void Server::removeAllNotifications() noexcept {
        ::std::lock_guard<Server::Mutex> {this->notificationMutex};
        this->notificationMap.clear();
    }

    void Server::clear() noexcept {
        this->removeAllNotifications();
        this->removeAllMethods();
    }

    size_t Server::nNotifications() {
        ::std::lock_guard<Mutex>{this->methodMutex};
        return this->methodMap.size();
    }

    // 启动rpc服务器，当启动失败或异常关闭时，将会返回false
    bool Server::start(std::string ip, int port) & {
        // single judge
        if(
            this->is_running() ||
            !(port >= 0 && port <= 65525)
        ) {
            return false;
        }
        return this->listen(ip.c_str(), port);
    }

    void Server::close() {
        this->stop();
        // Ensure the correct operation of the notification
        ::std::lock_guard<Server::Mutex> {this->methodMutex};
        this->methodMap.clear();
    }


}