/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../log/log.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../pool/sqlconnRAII.h"
#include "../http/httpconn.h"

class WebServer {
public:
    WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);

    ~WebServer();
    void Start();

private:
    bool InitSocket_();  // socket初始化
    void InitEventMode_(int trigMode);  // 事件触发模式设置
    void AddClient_(int fd, sockaddr_in addr);  // 添加客户端连接
  
    void DealListen_();  // 处理新连接
    void DealWrite_(HttpConn* client);  // 处理客户端写事件, 是将写事件的处理函数OnWrite和参数添加到任务队列
    void DealRead_(HttpConn* client);   // 处理客户端读事件, 是将读事件的处理函数OnWrite和参数添加到任务队列

    void SendError_(int fd, const char*info);  
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);

    void OnRead_(HttpConn* client);  // 具体的读事件处理函数：调用client对象的read函数，将内核读缓冲区数据读到readbuffer中
    void OnWrite_(HttpConn* client);  // 具体的写事件处理函数：调用client对象的write函数（个人理解 待定）
    void OnProcess(HttpConn* client);  // 调用client对象的process事件进行处理, 并改变文件描述符监听事件

    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    int port_;
    bool openLinger_;
    int timeoutMS_;  /* 毫秒MS */
    bool isClose_;
    int listenFd_;  // 用于监听的文件描述符
    char* srcDir_;
    
    uint32_t listenEvent_;
    uint32_t connEvent_;
   
    std::unique_ptr<HeapTimer> timer_;  // unique_ptr指针包装定时器
    std::unique_ptr<ThreadPool> threadpool_;  // unique_ptr指针包装线程池
    std::unique_ptr<Epoller> epoller_;  // unique_ptr指针包装Epoller
    std::unordered_map<int, HttpConn> users_;  // 记录文件描述符fd和对应的http连接对象的哈希表
};


#endif //WEBSERVER_H