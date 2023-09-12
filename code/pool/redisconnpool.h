#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <hiredis/hiredis.h>
#include <chrono>
#include <queue>
#include <mutex>
#include <thread>  // lock_guard
#include <semaphore.h>
#include "../log/log.h"
// #define NDEBUG
#include <cassert>

/*
利用Redis缓存技术提高查询效率
*/

class Redis {
public:
  // 构造函数，连接到Redis服务器
  Redis(const std::string& host, int port);
  // 析构函数，断开与Redis服务器的连接
  ~Redis();

  void set(const std::string& key, const std::string& value);  // 实现SET命令 
  std::string get(const std::string& key);  // 实现GET命令
  std::vector<std::string> lrange(const std::string& key, int start, int end);  // 实现LRANGE命令,用于获取列表中指定区间内的元素的命令

private:
    redisContext* redis_;
};


class RedisConnPool {
public:
    // 获取数据库连接池，唯一实例
    static RedisConnPool& Instance();

    void Init(const char* host, int port, int connSize);  // connSize指设置的连接数量 
    
    Redis* GetConn();  // 获取数据库连接池中的连接
    void FreeConn(Redis * conn);  // 释放连接

    int GetFreeConnCount();  // 查询池中剩余连接
    int GetCurrConnCount();

    void ClosePool();  // 关闭连接池

private:
    RedisConnPool(): useCount_(0), freeCount_(0), MAX_CONN_(16) {}
    ~RedisConnPool();
    RedisConnPool(RedisConnPool&) = delete;  // 删除拷贝构造
    RedisConnPool& operator=(RedisConnPool&) = delete;  // 删除拷贝赋值

    int useCount_;  // 当前使用数
    int freeCount_;  // 可使用连接数
    int MAX_CONN_;  // 最大连接数 
    
    std::queue<Redis *> connQue_;  // 队列存储具体的数据库连接
    std::mutex mtx_;  // 互斥锁实现互斥访问队列，保证多线程下的线程安全
    sem_t semId_;  // 信号量实现同步
};


class RedisConnRAII {
public:
    RedisConnRAII(Redis** conn, RedisConnPool *connpool) {
        *conn = connpool->GetConn();
        conn_ = *conn;
        pool_ = connpool;
    }

    ~RedisConnRAII() {
       if(conn_) {
          pool_->FreeConn(conn_);
       }
    }
    
private:
    Redis* conn_;
    RedisConnPool* pool_;
};




// 单元测试
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