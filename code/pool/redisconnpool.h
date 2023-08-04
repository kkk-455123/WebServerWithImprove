#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <hiredis/hiredis.h>
#include <chrono>
// #define NDEBUG
#include <cassert>

/*
利用Redis缓存技术提高查询效率
*/

class Redis {
public:
  // 构造函数，连接到Redis服务器
  Redis(const std::string& host, int port) {
    // 使用hiredis库的函数来连接到Redis服务器
    redis_ = redisConnect(host.c_str(), port);
    if (redis_ == NULL || redis_->err) {
      throw std::runtime_error("Error: failed to connect to Redis server.");
    }
  }

  // 析构函数，断开与Redis服务器的连接
  ~Redis() {
    // 使用hiredis库的函数来断开与Redis服务器的连接
    redisFree(redis_);
  }

  // 实现SET命令
  void set(const std::string& key, const std::string& value) {
    // 使用hiredis库的函数来执行SET命令
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(redis_, "SET %b %b", key.data(), key.size(), value.data(), value.size()));
    if (reply == NULL) {
        throw std::runtime_error("Error: failed to execute SET command.");
    }
    freeReplyObject(reply);
  } 

  // 实现GET命令
  std::string get(const std::string& key) {
    // 使用hiredis库的函数来执行GET命令
    redisReply* reply = static_cast<redisReply*>(redisCommand(redis_, "GET %b", key.data(), key.size()));
    if (reply == NULL || reply->type == REDIS_REPLY_NIL) {
        // throw std::runtime_error("Error: failed to execute GET command.");
        freeReplyObject(reply);
        return std::string("");
    }
    std::string value(reply->str, reply->len);
    freeReplyObject(reply);
    return value;
  }

    // 实现LRANGE命令
    // 用于获取列表中指定区间内的元素的命令
    std::vector<std::string> lrange(const std::string& key, int start, int end) {
        // 使用hiredis库的函数来执行LRANGE命令
        std::vector<std::string> values;
        redisReply* reply = static_cast<redisReply*> ( 
            redisCommand(redis_, "LRANGE %b %d %d", key.data(), key.size(), start, end));
        if (reply == NULL || reply->type != REDIS_REPLY_ARRAY) 
            throw std::runtime_error("Error: failed to execute LRANGE command.");
        for (size_t i = 0; i < reply->elements; ++i) 
            values.emplace_back(reply->element[i]->str, reply->element[i]->len);
        freeReplyObject(reply);
        return values;
    }

private:
    redisContext* redis_;
};





// int main() {
//     try {
//         // 创建Redis类的实例并连接到本地的Redis服务器
//         Redis redis("localhost", 6379);
//         // 执行SET命令
//         redis.set("hello", "world");
//         // 执行LRANGE命令
//         std::vector<std::string> values = redis.lrange("mylist", 0, -1);
//         for (const std::string& value : values) 
//             std::cout << "LRANGE mylist: " << value << std::endl;

//         // 在Web服务器中使用的示例
//         // 接收到客户端请求时，使用Redis对象的set方法来存储请求相关的数据
//         std::string key = "request_id";
//         std::string value = "12345";
//         redis.set(key, value);

//         // 在Web服务器处理客户端请求时，使用Redis对象的get方法来获取存储的请求相关的数据
//         const auto startTime = std::chrono::steady_clock::now();
//         std::string request_id = redis.get(key);
//         const auto endTime = std::chrono::steady_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
//         std::cout << "Request ID: " << request_id << std::endl
//                   << "Redis查询耗时:" << duration << "us" << std::endl;
//     }

//     catch(const std::exception& e) {
//         std::cerr << e.what() << std::endl;
//         return 1;
//     }

//     return 0;
// }