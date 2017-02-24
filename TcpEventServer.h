//
// Created by zhangshibo on 17-2-24.
//

#ifndef TCPEVENTSERVER_TCPEVENTSERVER_H
#define TCPEVENTSERVER_TCPEVENTSERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>

#include <map>
using std::map;

#include <event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

class TcpEventServer;
class Conn;
class ConnQueue;
struct LibeventThread;

//这个类一个链表的结点类，结点里存储各个连接的信息，
//并提供了读写数据的接口
class Conn
{
    //此类只能由TcpBaseServer创建，
    //并由ConnQueue类管理
    friend class ConnQueue;
    friend class TcpEventServer;

private:
    const int m_fd;				//socket的ID
    evbuffer *m_ReadBuf;		//读数据的缓冲区
    evbuffer *m_WriteBuf;		//写数据的缓冲区

    Conn *m_Prev;				//前一个结点的指针
    Conn *m_Next;				//后一个结点的指针
    LibeventThread *m_Thread;

    Conn(int fd=0);
    ~Conn();

public:
    LibeventThread *GetThread() { return m_Thread; }
    int GetFd() { return m_fd; }

    //获取可读数据的长度
    int GetReadBufferLen()
    { return evbuffer_get_length(m_ReadBuf); }

    //从读缓冲区中取出len个字节的数据，存入buffer中，若不够，则读出所有数据
    //返回读出数据的字节数
    int GetReadBuffer(char *buffer, int len)
    { return evbuffer_remove(m_ReadBuf, buffer, len); }

    //从读缓冲区中复制出len个字节的数据，存入buffer中，若不够，则复制出所有数据
    //返回复制出数据的字节数
    //执行该操作后，数据还会留在缓冲区中，buffer中的数据只是原数据的副本
    int CopyReadBuffer(char *buffer, int len)
    { return evbuffer_copyout(m_ReadBuf, buffer, len); }

    //获取可写数据的长度
    int GetWriteBufferLen()
    { return evbuffer_get_length(m_WriteBuf); }

    //将数据加入写缓冲区，准备发送
    int AddToWriteBuffer(char *buffer, int len)
    { return evbuffer_add(m_WriteBuf, buffer, len); }

    //将读缓冲区中的数据移动到写缓冲区
    void MoveBufferData()
    { evbuffer_add_buffer(m_WriteBuf, m_ReadBuf); }

};

//带头尾结点的双链表类，每个结点存储一个连接的数据
class ConnQueue
{
private:
    Conn *m_head;
    Conn *m_tail;
public:
    ConnQueue();
    ~ConnQueue();
    Conn *InsertConn(int fd, LibeventThread *t);
    void DeleteConn(Conn *c);
    //void PrintQueue();
};

//每个子线程的线程信息
struct LibeventThread
{
    pthread_t tid;				//线程的ID
    struct event_base *base;	//libevent的事件处理机
    struct event notifyEvent;	//监听管理的事件机
    int notifyReceiveFd;		//管理的接收端
    int notifySendFd;			//管道的发送端
    ConnQueue connectQueue;		//socket连接的链表

    //在libevent的事件处理中要用到很多回调函数，不能使用类隐含的this指针
    //所以用这样方式将TcpBaseServer的类指针传过去
    TcpEventServer *tcpConnect;	 //TcpBaseServer类的指针
};

class TcpEventServer
{
private:
    int m_ThreadCount;					//子线程数
    int m_Port;							//监听的端口
    LibeventThread *m_MainBase;			//主线程的libevent事件处理机
    LibeventThread *m_Threads;			//存储各个子线程信息的数组
    map<int, event*> m_SignalEvents;	//自定义的信号处理

public:
    static const int EXIT_CODE = -1;

private:
    //初始化子线程的数据
    void SetupThread(LibeventThread *thread);

    //子线程的入门函数
    static void *WorkerLibevent(void *arg);
    //（主线程收到请求后），对应子线程的处理函数
    static void ThreadProcess(int fd, short which, void *arg);
    //被libevent回调的各个静态函数
    static void ListenerEventCb(evconnlistener *listener, evutil_socket_t fd,
                                sockaddr *sa, int socklen, void *user_data);
    static void ReadEventCb(struct bufferevent *bev, void *data);
    static void WriteEventCb(struct bufferevent *bev, void *data);
    static void CloseEventCb(struct bufferevent *bev, short events, void *data);

protected:
    //这五个虚函数，一般是要被子类继承，并在其中处理具体业务的

    //新建连接成功后，会调用该函数
    virtual void ConnectionEvent(Conn *conn) { }

    //读取完数据后，会调用该函数
    virtual void ReadEvent(Conn *conn) { }

    //发送完成功后，会调用该函数（因为串包的问题，所以并不是每次发送完数据都会被调用）
    virtual void WriteEvent(Conn *conn) { }

    //断开连接（客户自动断开或异常断开）后，会调用该函数
    virtual void CloseEvent(Conn *conn, short events) { }

    //发生致命错误（如果创建子线程失败等）后，会调用该函数
    //该函数的默认操作是输出错误提示，终止程序
    virtual void ErrorQuit(const char *str);

public:
    TcpEventServer(int count);
    ~TcpEventServer();

    //设置监听的端口号，如果不需要监听，请将其设置为EXIT_CODE
    void SetPort(int port)
    { m_Port = port; }

    //开始事件循环
    bool StartRun();
    //在tv时间里结束事件循环
    //否tv为空，则立即停止
    void StopRun(timeval *tv);

    //添加和删除信号处理事件
    //sig是信号，ptr为要回调的函数
    bool AddSignalEvent(int sig, void (*ptr)(int, short, void*));
    bool DeleteSignalEvent(int sig);

    //添加和删除定时事件
    //ptr为要回调的函数，tv是间隔时间，once决定是否只执行一次
    event *AddTimerEvent(void(*ptr)(int, short, void*),
                         timeval tv, bool once);
    bool DeleteTImerEvent(event *ev);
};

#endif //TCPEVENTSERVER_TCPEVENTSERVER_H
