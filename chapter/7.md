# 第七部分 实践中的地理计算框架

## 1 框架设想
MPI是高效的，基础的。

## 2 任务安排

### 第一步，构建消息库
* 构建一个可靠的消息队列库
* 利用消息队列库，实现多进程的合作

### 第二步，设计同步模式
* 基于scala的actor
* 基结go的channel
* 基于react的，也就是基于事件
* geaman队列系统

### 第三步，嵌入脚本语言
* 嵌入脚本，融合python，ruby，lua或者newlisp。

## 3 打算要做的事
* 将gdal中[port](https://github.com/OSGeo/gdal/tree/trunk/gdal/port)库提出来，专门做一个工具库类库。
* 将[readme.md](https://github.com/htoooth/libpcm)写完
* 将[mpi-rpc](https://github.com/htoooth/hpgc_new/blob/master/src/rpc.cpp)再拿出来研究
