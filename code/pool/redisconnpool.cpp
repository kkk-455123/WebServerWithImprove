#include "redisconnpool.h"

// 构造函数，连接到Redis服务器
Redis::Redis(const std::string& host, int port) {
    // 使用hiredis库的函数来连接到Redis服务器
    redis_ = redisConnect(host.c_str(), port);
    if(redis_ == NULL || redis_->err) {
        throw std::runtime_error("Error: failed to connect to Redis server.");
    }
}

// 析构函数，断开与Redis服务器的连接
Redis::~Redis() {
    // 使用hiredis库的函数来断开与Redis服务器的连接
    redisFree(redis_);
}

// 实现SET命令
void Redis::set(const std::string& key, const std::string& value) {
    redisReply* reply = static_cast<redisReply*>(redisCommand(redis_, "SET %b %b", key.data(), key.size(), value.data(), value.size()));
    if(reply == nullptr) {
        throw std::runtime_error("Error: failed to execute SET command.");
    }
    freeReplyObject(reply);
}

std::string Redis::get(const std::string& key) {
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
std::vector<std::string> Redis::lrange(const std::string& key, int start, int end) {
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


// 获取数据库连接池，唯一实例
RedisConnPool& RedisConnPool::Instance() {
    static RedisConnPool connPool;
    return connPool;
}  

// connSize指设置的连接数量
void RedisConnPool::Init(const char* host, int port, int connSize) {
    assert(connSize > 0);
    connSize = connSize > MAX_CONN_ ? MAX_CONN_ : connSize;
    for(int i = 0; i < connSize; i++) {
        Redis* redisConn = new Redis(host, port);
        if(!redisConn) {
            LOG_ERROR("Rediss init error");
        } 
        connQue_.push(redisConn);
        freeCount_ =   connSize;
    }
} 

// 获取数据库连接池中的连接
Redis* RedisConnPool::GetConn() {
    Redis* conn = nullptr;
    if(connQue_.empty()) {
        LOG_WARN("RedisConnPool busy!");
    }
    sem_wait(&semId_);  // P操作
    {
        // 互斥访问队列
        std::lock_guard<std::mutex> locker(mtx_);
        conn = connQue_.front();
        connQue_.pop();
        freeCount_--;
        useCount_++;
    }
    return conn;
}

void RedisConnPool::FreeConn(Redis * conn) {
    assert(conn);
    connQue_.push(conn);
    {
        std::lock_guard<std::mutex> locker(mtx_);
        freeCount_++;
        useCount_--;
    }
    sem_post(&semId_);
}

int RedisConnPool::GetFreeConnCount() {
    return freeCount_;
}

int RedisConnPool::GetCurrConnCount() {
    return useCount_;
} 

void RedisConnPool::ClosePool() {
    std::lock_guard<std::mutex> locker(mtx_);
    while(!connQue_.empty()) {
        auto item = connQue_.front();
        item->~Redis();
        connQue_.pop();
    }   
}

RedisConnPool::~RedisConnPool() {
    this->ClosePool();
}