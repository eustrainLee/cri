/*
 * @Author: 李兴鑫
 * @Date: 2022-01-11 18:45:23
 * @Description: JSONRPC 2.0 Server
 */
#ifndef LITE_RPC__
#define LITE_RPC__

#include <new>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>

#include "def.hpp"
#include "httplib.hpp"

#include <iostream>

// Note: word “rpc” refers to jsonrpc in this file.

class ThreadPool;

namespace rpc
{

    const size_t DEFAULT_NTHREADS = ((std::max)(8u, std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() - 1 : 0));

    enum class Error : int {
        // jsonrpc 2.0

        Parse = -32700, // Parse error
        Request = -32600, // Invalid Request
        Method = -32601, // Method not found
        Params = 32602, // Invalid params
        Internal = -32603, // Internal error
        Server = -32099, // Server error from -32099 to -32000
        Exception = -32001, // custom error

        Undefined = -1,// undefined

        Success = 0// not an error

    };
    
    class Tuple { public: virtual ~Tuple() = 0; }; // Don't insert any additional code in the class.

    class Server final : protected httplib::Server {
    public:

    public: // 接口声明
        // BEGIN method call(方法调用)
        /**
         * 1
         * @description: 注册一个函数，要求参数和返回值继承::rpc::Tuple, func的参数和返回值必须继承自::rpc::Tuple
         * @param: {::std::string} name 注册的名称, 名称不应以"rpc"作为开头
         * @param: {::rpc::MethodFunc} func 注册的函数
         * @param: {::rpc::Decoder} decoder 能够将Json对象转化为Tuple派生类对象的函数
         * @param: {::rpc::Encoder} encoder 能够将Tuple派生类对象转化为Json对象的函数
         * @return: {bool} 是否成功注册该函数
         */
        bool addMethod(
            ::std::string name,
            ::rpc::MethodFunc func,
            ::rpc::Decoder decoder,
            ::rpc::Encoder encoder
        ) &;
        // 注册一个函数，但这个函数会自动生成decoder和encoder，前提是
        /**
         * 2
         * @description: 注册一个函数,并自动生成decoder和encoder，要求模板参数Res和Arg能够与Json对象相互转换,且Res和Arg继承于::rpc::Tuple
         * @param: {typename} Res 返回值类型
         * @param: {typename} Arg 参数类型
         * @param: {::std::string} name 注册的名称
         * @param: {::rpc::MethodFunc} func 注册的函数
         * @return: {bool} 是否成功注册该函数
         */
        template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type = 0>
        bool addMethod(
            ::std::string name,
            ::rpc::MethodFunc func
        ) &;
        
        /**
         * 3
         * @description: 注册一个函数,并自动生成decoder和encoder，要求模板参数Res和Arg能够与Json对象相互转换,且Res和Arg不继承于::rpc::Tuple
         * @param: {typename} Res 返回值类型
         * @param: {typename} Arg 参数类型
         * @param: {::std::string} name 注册的名称
         * @param: {::rpc::MethodFunc} func 注册的函数
         * @return: {bool} 是否成功注册该函数
         */
        template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type = 0>
        bool addMethod(
            ::std::string name,
            ::rpc::MateMethodFunc<Res,Arg> func
        ) &;
        /**
         * 4
         * @description: 注册一个接收Json const &返回Json函数
         * @param: {::std::string} name 注册的名称
         * @param: {JsonMethodFunc} func 注册的函数
         * @return: {bool} 是否成功注册该函数
         */
        bool addMethod (
            ::std::string name,
            ::rpc::JsonMethodFunc func
        ) &;

        /**
         * 5
         * @description: 取消注册所有的注册有此名称的函数
         * @param: {::std::string} name 要删除的一个函数注册的名称
         * @return： {bool} 是否删除了函数
         */
        bool removeMethod(::std::string const &name);
        // 取消注册所有函数
        void removeAllMethods() noexcept;  // noexcept begin c++11

        // END method call(方法调用)


        // BEGIN notification(通知)
        /**
         * 1
         * @description: 注册一个函数，要求参数和返回值继承::rpc::Tuple, func的参数和返回值必须继承自::rpc::Tuple
         * @param: {::std::string} name 注册的名称, 名称不应以"rpc"作为开头
         * @param: {::rpc::MethodFunc} func 注册的函数
         * @param: {::rpc::Decoder} decoder 能够将Json对象转化为Tuple派生类对象的函数
         * @param: {::rpc::Encoder} encoder 能够将Tuple派生类对象转化为Json对象的函数
         * @return: {bool} 是否成功注册该函数
         */
        bool addNotification(
            ::std::string name,
            ::rpc::NotificationFunc func,
            ::rpc::Decoder decoder,
            ::rpc::Encoder encoder
        ) &;
        // 注册一个函数，但这个函数会自动生成decoder和encoder，前提是
        /**
         * 2
         * @description: 注册一个函数,并自动生成decoder和encoder，要求模板参数Res和Arg能够与Json对象相互转换,且Res和Arg继承于::rpc::Tuple
         * @param: {typename} Arg 参数类型
         * @param: {::std::string} name 注册的名称
         * @param: {::rpc::MethodFunc} func 注册的函数
         * @return: {bool} 是否成功注册该函数
         */
        template<typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type = 0>
        bool addNotification(
            ::std::string name,
            ::rpc::NotificationFunc func
        ) &;
        
        /**
         * 3
         * @description: 注册一个函数,并自动生成decoder和encoder，要求模板参数Res和Arg能够与Json对象相互转换,且Res和Arg不继承于::rpc::Tuple
         * @param: {typename} Res 返回值类型
         * @param: {typename} Arg 参数类型
         * @param: {::std::string} name 注册的名称
         * @param: {::rpc::MethodFunc} func 注册的函数
         * @return: {bool} 是否成功注册该函数
         */
        template<typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type = 0>
        bool addNotification(
            ::std::string name,
            ::rpc::MateNotificationFunc<Arg> func
        ) &;
        /**
         * 4
         * @description: 注册一个接收Json const &返回Json函数
         * @param: {::std::string} name 注册的名称
         * @param: {JsonMethodFunc} func 注册的函数
         * @return: {bool} 是否成功注册该函数
         */
        bool addNotification (
            ::std::string name,
            ::rpc::JsonNotificationFunc func
        ) &;

        /**
         * 5
         * @description: 取消注册所有的注册有此名称的函数
         * @param: {::std::string} name 要删除的一个函数注册的名称
         * @return： {bool} 是否删除了函数
         */
        bool removeNotification(::std::string const &name);
        // 取消注册所有函数
        void removeAllNotifications() noexcept;  // noexcept begin c++11

        // END notification(通知)


        void clear() noexcept; // 移除所有函数

        size_t nMethods();
        size_t nNotifications();

        /*
            注:

            对于任一名称只允许注册一个函数
            要求传入的可执行类型是move-able或copy-able的
            若Server正在运行, 不保证注册和取消注册能够及时生效
            1. 要求参数和返回值类型继承于::rpc::Tuple, 不要求可以与Json直接转换类型
            2. 要求参数和返回值类型继承于::rpc::Tuple, 要求可以与Json直接转换类型
            3. 不要求参数和返回值类型继承于::rpc::Tuple, 要求可以与Json直接转换类型
            4. 要求参数和返回值类型是::rpc::Json, 或者可以发生隐式类型转换
        */

        // 启动rpc服务器
        bool start(std::string ip, int port) &;
        // 关闭rpc服务器，并释放连接资源，这个函数可能会阻塞当前线程, 若已经释放资源则不会做任何事情
        void close();

        using httplib::Server::is_running;

    public:
        explicit Server();
        Server(Server const &) = delete;
        Server(Server&&) = delete;
        Server& operator = (Server const &) = delete;
        Server& operator = (Server &&) = delete;
        ~Server() { 
            this->close(); // 释放资源
        }

    private:
        // 被存储的接收Json对象并返回Json对象的可直接调用的函数, 用于method call(方法调用)和notification(通知)
        using MethodFunc = ::std::function<Json(const Json&)>;
        // 被存储的接收Json对象可直接调用的函数数, 用于notification(通知)
        using NotificationFunc = ::std::function<void(const Json&)>;
        // ::std::string --> MethodFunc 根据指定的名称获取对应的函数, 在method call(方法调用)和notification(通知)中被尝试匹配
        using MethodMap = ::std::map<::std::string, MethodFunc>;
        // ::std::string --> NotificationFunc 根据指定的名称获取对应的函数, 在notification(通知)中被尝试匹配
        using NotificationMap = ::std::map<::std::string, NotificationFunc>;
        // One possible type of MyPiar is ::std::pair<::std::string const, MethodFunc>
        // for method call
        using MethodPair = MethodMap::value_type;
        // for Notification
        using NotificationPair = NotificationMap::value_type;
        
        // mutex for function maps
        class Mutex: public ::std::mutex {
        // DEBUG
        // public:
        //     bool try_lock() noexcept {
        //         ::std::cout << "[TL]";
        //         bool success = this->mutex.try_lock();
        //         return success;
        //     }

        //     void lock() {
        //         ::std::cout << "[L]";
        //         this->mutex.lock();
        //     }

        //     void unlock() {
        //         ::std::cout << "[UL]";
        //         this->mutex.unlock();
        //     }
        // private:
        //     ::std::mutex mutex;
        };
        

        // 被储存的所有函数 name->funcTuple
        MethodMap methodMap;
        NotificationMap notificationMap;
        // 锁
        Mutex methodMutex;
        Mutex notificationMutex;
        
    private:
        bool processRpc(
            httplib::Stream &strm,
            bool close_connection,
            bool &connection_closed
        );
        //  处理rpc请求然后关闭socket链接
        bool process_and_close_socket(socket_t sock) override;

        //  执行调用
        bool execMethod_1_0(Json request, Json& response) noexcept;
        //  执行通知
        bool execNotification_1_0(Json request) noexcept;
        //  执行调用
        bool execMethod_2_0(Json request, Json& response) noexcept;
        //  执行通知
        bool execNotification_2_0(Json request) noexcept;
    
    };

}

namespace rpc {
    // 模板定义

    template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type>
    bool Server::addMethod(
        ::std::string name,
        ::rpc::MethodFunc func) & {
        ::std::lock_guard<Server::Mutex> (this->methodMutex);
        if(1 == this->methodMap.count(name)) {
            return false;
        }
        Server::MethodFunc callFunc {[func=::std::move(func)](Json const & argJson) -> Json {
            Arg arg = static_cast<Arg>(argJson);
            auto resPtr = func(dynamic_cast<Tuple&>(arg));
            return  dynamic_cast<Res &>((*resPtr));
        }};
        bool success {this->methodMap.emplace(::std::move(name), ::std::move(callFunc)).second};
        return success;
    }

    template<typename Res, typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type>
    bool Server::addMethod(
        ::std::string name,
        MateMethodFunc<Res,Arg> func) & {
        ::std::lock_guard<Server::Mutex> (this->methodMutex);
        if(1 == this->methodMap.count(name)) {
            return false;
        }
        Server::MethodFunc callFunc {[func=::std::move(func)](Json const & argJson) -> Json {
            return func(static_cast<Arg>(argJson));
        }};
        bool success {this->methodMap.emplace(::std::move(name), ::std::move(callFunc)).second};
        return success;
    }


    template<typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type>
    bool Server::addNotification(
        ::std::string name,
        ::rpc::NotificationFunc func
    ) & {
        ::std::lock_guard<Server::Mutex> (this->notificationMutex);
        if(1 == this->notificationMap.count(name)) {
            return false;
        }
        Server::NotificationFunc notificationFunc {[func=::std::move(func)](Json const & argJson) {
            Arg arg = static_cast<Arg>(argJson);
            func(dynamic_cast<Tuple&>(arg));
        }};
        bool success {this->notificationMap.emplace(::std::move(name), ::std::move(notificationFunc)).second};
        return success;
    }

    template<typename Arg, typename ::std::enable_if<::std::is_same<::rpc::Json, ::nlohmann::json>::value,int>::type>
    bool Server::addNotification(
        ::std::string name,
        ::rpc::MateNotificationFunc<Arg> func
    ) & {
        ::std::lock_guard<Server::Mutex> { this->notificationMutex };
        if(1 == this->notificationMap.count(name)) {
            return false;
        }
        Server::NotificationFunc notificationFunc {[func=::std::move(func)](Json const & argJson) {
            func(static_cast<Arg>(argJson));
        }};
        bool success {this->notificationMap.emplace(::std::move(name), ::std::move(notificationFunc)).second};
        return success;
    }


}

#endif