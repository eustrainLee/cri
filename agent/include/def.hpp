


#ifndef __LITE_RPC_DEF__
#define __LITE_RPC_DEF__

#include <functional>
#include <memory>


#include "json.hpp"


namespace rpc {
        
        // public definition


        class Tuple;
        // 绑定的Json类型
        using Json = ::nlohmann::json;

        using Decoder = ::std::function<::std::shared_ptr<Tuple>(Json const&)>;
        using Encoder = ::std::function<Json(Tuple const&)>;
        
        // 默认的Rpc函数类型
        using MethodFunc = ::std::function<::std::shared_ptr<Tuple>(Tuple const &)>;
        using NotificationFunc = ::std::function<void(Tuple const&)>;
        // 元Rpc函数类型
        template<typename Res, typename Arg>
        using MateMethodFunc = ::std::function<Res(Arg const &)>;
        template<typename Arg>
        using MateNotificationFunc = ::std::function<void(Arg const &)>;
        // 处理Json的Rpc函数类型
        using JsonMethodFunc = ::std::function<Json(Json const&)>;
        using JsonNotificationFunc = ::std::function<void(Json const&)>;
        // 文件描述符类型
        // using FD = int;

}




#endif