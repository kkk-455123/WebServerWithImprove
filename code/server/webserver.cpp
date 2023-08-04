/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(make_unique<HeapTimer>()), threadpool_(make_unique<ThreadPool>(threadNum)), epoller_(make_unique<Epoller>())
            // timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256);  // 获取当前工作目录路径
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);  // 字符串拼接，得到资源文件的路径
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    InitEventMode_(trigMode);  // 初始化事件触发模式
    if(!InitSocket_()) { isClose_ = true;}  // 初始化socket套接字

    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

/*
 * 设置监听的文件描述符和连接文件描述符的事件处理模式
 * EPOLLEDHUP
 * EPOLLONESHOT
 * EPOLLET 
 */
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;  // 内核自动处理对端异常关闭的情况，应用层不用处理
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;  
    switch (trigMode)
    {
    case 0:
        break;
    case 1:  // 连接fd是ET模式
        connEvent_ |= EPOLLET;
        break;
    case 2:  // 监听fd是ET模式
        listenEvent_ |= EPOLLET;
        break;
    case 3:  // 二者都是ET模式
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:  // 默认二者都是ET模式
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);  // 通过按位与获取连接模式中关于ET模式的设置
}

void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    if(!isClose_) { LOG_INFO("========== Server start =========="); }
    while(!isClose_) {  // 死循环，不断调用epoll_wait
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }
        int eventCnt = epoller_->Wait(timeMS);
        // 循环遍历事件
        for(int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if(fd == listenFd_) {  // 监听到的发生事件的fd与listenfd一致，表示有客户端连接进来
                DealListen_();  // 接收客户端连接
            }
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {  // 连接错误
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);  // 关闭连接
            }
            else if(events & EPOLLIN) {    
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);  // 处理读事件
            }
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);  // 处理写事件
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);  // 直接发送字符串信息？不应该是一个http响应吗
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);  // 关闭该连接
}

void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());  // 从epoll中移除
    client->Close();
}

void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr);  // 客户端连接初始化
    if(timeoutMS_ > 0) {
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_);  // 添加到epoll中，监听读事件
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

// 处理客户端连接事件
void WebServer::DealListen_() {
    struct sockaddr_in addr;  // 保存连接的客户端的信息 
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0) { return;}  // 非阻塞模式下，accept没有可接受连接时不会阻塞而是立马返回，在此处判断后跳出循环
        else if(HttpConn::userCount >= MAX_FD) {  // 最大连接数满了
            SendError_(fd, "Server busy!");  // 向客户端写一个服务器忙的信息
            LOG_WARN("Clients is full!");  // 记录日志
            return;
        }
        AddClient_(fd, addr);  // 添加客户端fd到httpconn的users、epoll监听的数据结构中
    } while(listenEvent_ & EPOLLET);  // ET模式，需要保证一次处理完，所以采用while循环不断调用accept接受listenFd上的连接，直到都接受完毕accept返回<=0后才return;
}

void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);  // 延长关闭连接的时间
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));  // 任务队列添加读任务
}

void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);  // 延长关闭连接的时间
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));  // 任务队列添加写任务
}

void WebServer::ExtentTime_(HttpConn* client) {  // 调用定时器调整关闭操作的到期时间
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);  // 读取客户端数据
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    OnProcess(client);  // 业务逻辑的处理
}

void WebServer::OnProcess(HttpConn* client) {
    if(client->process()) {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);  // connEvent_存放的是事件的触发方式（ET OR LT），使用|运算符在设置事件的同时快速设置事件的触发方式
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


