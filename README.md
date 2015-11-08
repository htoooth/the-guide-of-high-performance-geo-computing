# 高性能地理计算学习资料清单
收集有关并行计算，并发设计和地理计算的相关资料进行汇总，方便自己学习查看。资料分为六个部分：

# 目录
* 第一部分：__地理信息科学__，讨论地理信息科学的内容；
* 第二部分：__地理计算__，有关GDAL/OGR等的资料；
* 第三部分：__并行计算__，为高性能计算部分，有关并行、并发、MPI等资料；
* 第四部分：__高性能地理计算__，收集整理高性能地理计算部分；
* 第五部分：__业界最新发展方向__，为业界最新发展方向；
* 第六部分：__云计算和大数据__，为业界的动态；
* 第七部分：__当前实验进展__，为当前实验进展；


---

# 第一部分 地理信息科学
## 1 地理信息科学框架

![框架](docs/frame.jpg)

源文件[visio格式](docs/frame.vsdx)

## 2 地理信息科学的主要研究方面
### 李老师的GIS三界理论

* 第一界：GIS的理论研究
* 第二界：GIS的技术开发
* 第三界：GIS的应用实践

## 3 地理信息科学的核心重要问题

* 定性
* 定量
* 定位（空间分析）
* 定期（时序分析）
* 定时（典型调查与总体特征分析）

## 4 地理信息科学的终级问答

人类依靠地理能怎样认识地球？


----


# 第二部分 地理计算

## 1 什么是地理计算

## 2 脚本绑定gdal
* python-gdal，GDAL中自带，这里是[文档](http://blog.csdn.net/sunny2038/article/details/8018965),[1](https://pypi.python.org/pypi/GDAL/),[2](http://blog.csdn.net/sunny2038/article/details/8000932)
* [ruby-gdal](https://github.com/zhm/gdal-ruby)
* lua-gdal,Null
* [node.js-gdal](https://github.com/naturalatlas/node-gdal)
* [go-gdal](https://github.com/lukeroth/gdal)

## 3 文档资料
* [python_gdal接口文档](http://pcjericks.github.io/py-gdalogr-cookbook/index.html)
* [学习python_gdal的教程](http://www.gis.usu.edu/~chrisg/python/2009/)

## 4 开源地理服务
* [mapserver](http://lab.osgeo.cn/mapserver_tutorial/index.html)
* [geocloud2](http://www.mapcentia.com/en/geocloud/),A complete platform for managing geospatial data, making map visualisations and creating applications. Built on the best open source and standard based software.
* [opengoe-suite](http://boundlessgeo.com/solutions/opengeo-suite/), Build great maps and apps with the geospatial power of our standards-based platform.
* [geoserver](http://geoserver.org/),is an open source software server written in Java that allows users to share and edit geospatial data. 
* [geomoose](http://www.geomoose.org/),is a Web Client JavaScript Framework for displaying distributed cartographic data that built upon other open source projects MapServer,OpenLayers and Dojo Toolkit.
* [geonode](http://geonode.org/). Open Source Geospatial Content Management System.
* [mapzen](https://mapzen.com/)，一系列开源库，帮助你构建自己的地图系统。
* [mapbox](https://www.mapbox.com/)，新一代的地图工具。
* [cartodb](http://cartodb.com/)
* [openlayers](http://openlayers.org/)
* [leaflet](http://leafletjs.com/)

----


# 第三部分 并行计算
包括一些有关并行的程序库和文档资料。

## 1 我的关于并行计算三个设定

* 第一条：并行算法只能在并行计算机上运行
* 第二条：并行算法处理的数据，串行算法没有办法处理
* 第三条：并行算法不一定比串行算法快


## 2 程序语言讨论
![程序语言](docs/programlanguage.jpg)
这是我的判断图。
源文件[visio格式](docs/programlanguage.vsdx)

当前最新的计算机语言，几乎都把并发当成语言内置的支持，这对并行计算影响很大。

### 注意
* Cobra 语言设计很有意思将测试和契约设计思想融入语言的设计中，值得学习。

## 3 程序语言设计库
如何设计一门自已的程序语言，有以下库可以帮助你：

* [antlr](http://www.antlr.org/)
* [Nitra](https://github.com/JetBrains/Nitra)

## 4 参考设计语言

* [nonelang](https://bitbucket.org/duangle/nonelang/src)
* [fakescript](https://bitbucket.org/esrrhs/fakescript)
* [L++](https://bitbucket.org/ktg/l)
* [paren](https://bitbucket.org/ktg/paren)
* [morfa](https://bitbucket.org/morfa-dev/morfa)
* [ponyc](https://github.com/CausalityLtd/ponyc)
* [Def](https://github.com/yangjiePro/Def)
* [fantom](http://fantom.org/)
* [gosu-lang](https://github.com/gosu-lang/gosu-lang)

## 5 理想中的语言
我自己的语言正在设计中，参见[YAPL](https://github.com/htoooth/YAPL/)。

## 6 软件开发过程管理

[快速构建一致的开发环境](http://yunlzheng.github.io/2014/10/08/build-local-develop-env/)

## 7 并行类库
* [ltsmin](libraries/ltsmin/src).中有一个有关[mpi的事件机制的循环](libraries/ltsmin/src/mpi-event-loop.c).可以学习。
* [casablance](http://casablanca.codeplex.com/).该库中有一个线程实现库叫`pplx`，跨平台实现，可以参考API的设计
* [Ray](https://github.com/sebhtml/RayPlatform).mpi为基础的并行计算库
* [theron](http://www.theron-library.com/index.php).该库有设计并行的实现
* [mpi-rpc](https://github.com/rjpower/piccolo/blob/master/src/util/rpc.cc)
* [zht-mpi](https://bitbucket.org/xiaobingo/iit.datasys.zht-mpi)
* [SObjectizer](http://sourceforge.net/projects/sobjectizer/).The SObjectizer is a framework for agent-oriented programming in C++.
* [caf](http://actor-framework.org/)，该库有设计actor的实现
* [TBB](https://www.threadingbuildingblocks.org/)，Intel Threading Building Blocks
* [libprocess](https://github.com/3rdparty/libprocess).process之间的actor风格的库
* [actor-cpp](https://import.github.com/htoooth/actor-cpp/).An implementation of the actor model for C++.
* [libactor](https://import.github.com/htoooth/libactor).Actor Model Library for C.
* [Channel](http://channel.sourceforge.net/).Name Space Based C++ Framework For Asynchronous, Distributed Message Passing and Event Dispatching.
* [C++CSP2](http://www.cs.kent.ac.uk/projects/ofa/c++csp/).Easy Concurrency for C++.
* [concurrencykit](https://github.com/concurrencykit/ck)，高并发库。
* [libconcurrency](https://import.github.com/htoooth/libconcurrency)
* [libtask](http://swtch.com/libtask/)
* [state-threads](https://github.com/winlinvip/state-threads).线程级的库实现，这里有一些[文档](http://coolshell.cn/articles/12012.html)。
* [chan](https://github.com/tylertreat/chan)，纯c实现在channel库
* [libcoro](http://software.schmorp.de/pkg/libcoro.html).一个并发库
* [libev](http://software.schmorp.de/pkg/libev.html).一个事件库
* [mapreduce c++](http://craighenderson.co.uk/papers/software_scalability_mapreduce/library)
* [skynet](https://github.com/cloudwu/skynet).风云的博客中有一个关于工业级的使用，多进程和多线程，还有嵌入式脚本，博客的内容在[这里](http://blog.codingnow.com/2012/09/the_design_of_skynet.html)。
* [fibjs](https://github.com/xicilion/fibjs).其中有多个类库，可以学习
* [hpx](https://github.com/STEllAR-GROUP).一个分布式计算框架，可以看看构架
* [neu](https://github.com/andrometa/neu).另一分布式计算框架，可以看看构架
* [meguro](https://github.com/jubos/meguro)[Javascript].A Javascript Map/Reduce framework
* [Disco](http://discoproject.org/)[python].a lightweight, open-source framework for distributed computing based on the MapReduce paradigm.
* [mrjob](https://github.com/Yelp/mrjob),Run MapReduce jobs on Hadoop or Amazon Web Services.
* [jug](https://github.com/luispedro/jug), A Task-Based Parallelization Framework.
* [GraphLab](http://graphlab.org/projects/index.html)
* [MapReduce for C（MR4C）](https://github.com/google/mr4c)，目的是为了优化其地理空间数据及计算机视觉代码库。
* [GeoKettle](http://www.geokettle.org/) is a powerful, metadata-driven Spatial ETL tool dedicated to the integration of different spatial data sources for building and updating geospatial data warehouses. [download](http://dev.spatialytics.com/svn/geokettle-2.0/tags/2.5/)
* [HPX ](https://github.com/STEllAR-GROUP/hpx),A general purpose C++ runtime system for parallel and distributed applications of any scale.
* [dask](https://github.com/blaze/dask)，Dask provides multi-core execution on larger-than-memory datasets using blocked algorithms and task scheduling.


## 8 工具类库
* 序列化
  * [nanomsg](https://github.com/nanomsg)，消息类库
  * [Bond](https://github.com/Microsoft/bond/),微软出品
  * [capnproto](http://kentonv.github.io/capnproto/cxxrpc.html)，消息类库
  * [protobuf](https://github.com/google/protobuf),谷歌，消息类库，[文档](http://www.searchtb.com/2012/09/protocol-buffers.html)
  * [flatbuffers](http://google.github.io/flatbuffers/index.html),谷歌出品，新一代序列化库
  * [thrift](https://thrift.apache.org/)，多语言支持消息化库
  * [cereal](http://uscilab.github.io/cereal/index.html)，消息类库
  * [jansson](http://www.digip.org/jansson/)，json类库
* 杂项
  * [zlog](http://hardysimpson.github.io/zlog/)，日志类库
  * [acl](https://github.com/zhengshuxin/acl)，一个网络类库
  * [tbox](https://github.com/waruqi/tbox)，一个工具库类包括常用东西
  * [Ylib](https://github.com/Amaury/Ylib)，常用类库
  * [sundial](https://github.com/guiquanz/sundial)，常用数据结构
  * [tap](https://github.com/zorgnax/libtap)，测试类库
  * [cheat](https://github.com/Tuplanolla/cheat),测试类库
  * [Catch](https://github.com/philsquared/Catch),测试类库，c++
  * [mingw-常用库](http://nuwen.net/mingw.html)
  * [boost](http://www.boost.org/)
  * [POCO](https://github.com/pocoproject)
  * [Concurrent Data Structures ](http://libcds.sourceforge.net/)
* 还有很多关于facebook和google开源的项目都可以学习

## 9 关于并发的文章
* [mpi doc](http://mpi.deino.net/mpi_functions/index.htm)，这里面有对每一个mpi函数示例，非常详细。
* [mpi相关练习](https://computing.llnl.gov/tutorials/mpi/exercise.html)
* [The Auto Macro: A Clean Approach to C++ Error Handling](http://blog.memsql.com/c-error-handling-with-auto/)
* [C++ resources](https://cpp.zeef.com/faraz.fallahi)
* [awesome-cpp](https://github.com/fffaraz/awesome-cpp)
* [并发与并行](http://www.blogjava.net/killme2008/archive/2010/03/23/316273.html)
* [熟悉GO了解它的并发](https://www.zybuluo.com/Gestapo/note/32082)
* [编译器](http://mikespook.com/2014/05/%E7%BF%BB%E8%AF%91%E7%BC%96%E8%AF%91%E5%99%A81-%E4%BD%BF%E7%94%A8-go-%E5%BC%80%E5%8F%91%E7%BC%96%E8%AF%91%E5%99%A8%E7%9A%84%E4%BB%8B%E7%BB%8D/)
* [c hard way](http://c.learncodethehardway.org/book/)
* [ruby hard way](http://lrthw.github.io/ex01/)
* [glog使用](http://www.outsky.org/article.php?id=12)
* [Akka学习](http://www.iteblog.com/archives/1156)
java akka的actor学习
* [Go-style Channel in C++](http://st.xorian.net/blog/2012/08/go-style-channel-in-c/)
* [actorlite](http://www.cnblogs.com/JeffreyZhao/archive/2009/05/11/a-simple-actor-model-implementation.html)
老赵的博客中有讲actor的设计，可以借签。
* [centos下安装gcc-4.8.1](http://ilovers.sinaapp.com/article/centos%E4%B8%8B%E5%AE%89%E8%A3%85gcc-481)
* [如何在CentOS上使用高版本的GCC编译](http://my.oschina.net/kisops/blog/151089)
* [centos 6 升级gcc](http://www.cnblogs.com/peterpanzsy/archive/2013/04/10/3006838.html)
* [linux下升级gcc的方法 – 亲测可用](http://www.cppfans.org/1719.html)
* [How to Install gcc 4.7.x/4.8.x on CentOS](http://superuser.com/questions/381160/how-to-install-gcc-4-7-x-4-8-x-on-centos)
* [CENTOS快速方便支持C++](http://www.cnblogs.com/yokel/p/3977522.html)
* [Devtools for CentOS](http://braaten-family.org/ed/blog/2014-05-28-devtools-for-centos/)
* [Leveldb实现原理](http://www.cppfans.org/1652.html)
* [gmock学习01---Linux配置gmock](http://www.cnblogs.com/bourneli/archive/2012/09/08/2677000.html)
* [玩转Google开源C++单元测试框架Google Test系列(gtest)(总)](http://www.cnblogs.com/coderzh/archive/2009/04/06/1426755.html)
* [CMake 入门实战](http://hahack.com/codes/cmake/)
* [Principles of Distributed Computing](http://dcg.ethz.ch/lectures/podc_allstars/)

## 10 c++中关于线程的学习
* [知识点](http://www.360doc.com/userhome.aspx?userid=1317564&cid=437)
* [boost-thread(1,2,3,4)](http://amornio.iteye.com/category/226385)
* [C++并发实战(C++11),	Linux多线程编程C++,网络编程](http://blog.csdn.net/liuxuejiang158/article/category/1774739/2)
* [信号量 互斥锁 条件变量的区别(讲的很好，值得收藏)](http://www.cnblogs.com/lonelycatcher/archive/2011/12/20/2294161.html)
* [Sleep for milliseconds](http://stackoverflow.com/questions/4184468/sleep-for-milliseconds)
* [C++11之多线程(三、条件变量）,有很多关于c++的文章](http://blog.poxiao.me/p/multi-threading-in-cpp11-part-3-condition-variable/)
* [C++11 并发指南系列](http://www.cnblogs.com/haippy/p/3284540.html)
* [C++小品：她来听我的演唱会——C++11中的随机数、线程(THREAD)、互斥(MUTEX)和条件变量](http://www.cppblog.com/chenlq/archive/2011/11/12/159981.html)
* [Boost Thread学习](http://www.blogjava.net/LittleDS/archive/2010/06/06/201253.html)
* [再谈互斥锁与条件变量！](http://blog.chinaunix.net/uid-27164517-id-3282242.html)
* [boost库(条件变量)](http://www.cnblogs.com/zzyoucan/p/3626823.html)
* [线程学习小结](http://www.cnblogs.com/davidyang2415/archive/2012/04/09/2439104.html)
* [第34课 - 信号量](http://tjumyk.github.io/sdl-tutorial-cn/lessons/lesson34/index.html)
* [How to Write a Spelling Corrector](http://norvig.com/spell-correct.html)

## 11 C++构建工具的选择
* [premake](http://industriousone.com/premake)
* [scons](http://www.scons.org/)
* [gyp](https://github.com/htoooth/gyp)
* [cmake](http://www.cmake.org/)

## 12 脚本绑定mpi
* [ruby-mpi](https://github.com/seiya/ruby-mpi)
* [pybhon-mpi](http://mpi4py.scipy.org/)
* [lua-mpi](https://github.com/jzrake/lua-mpi)
* js-mpi , Null
* [go-mpi](https://github.com/JohannWeging/go-mpi)

## 13 MPI相关问题汇总
### MPI与Hadoop的区别和联系
并行计算的代表性技术是MPI，云计算的代表性技术是Hadoop。云计算的基础是并行计算，是并行计算和网络结合而发展起来的。MPI和Hadoop代表了不同背影开发者和学术的不同观点。所以MPI和Hadoop有很大的关系，既有共同点也有很多不同之处。

两者的相同点如下：
两者都是使用多个节点进行并行计算，协作完成一个任务；都可以在专用的并行机和廉价的PC机组成的机群上运行；Hadoop整体上是主从式架构的，HDFS是主从结构的，MapReduce任务调度模型也是主从结构的；MPI主要的程序设计模式也是主从式的。Hadoop分布式计算的主要思想是MPi和Reduce，而MPI也提供归约函数MPI_Reduce实现各个节点之间的归约操作。

两都的不同点如下：

（1）MPI标准的制定就是为了科学学者实现尖端科学技术的高速计算，目的是实现高效的并行计算。一般是在专用并行机或者PC机通过局域网搭建的机群上进行并行计算，耦合度很高，节点失效率低，所今MPI没有提供处理节点失效的备份处理。如果有节点失效，必须重新开始计算。而Hadoop是IT工程师开发实现的用于商业用途进行分布式计算的平台，目的是向用户提供服务。一般是由分布在各地的廉价PC通过互联网联接起来为用户提供服务，耦合度比较低，节点失效的可能性很大，所以Hadoop把节点失效看作是系统状态，提供了允许节点失效的备份处理容错机制。

（2）MPI向设计者提供的是一种节点间信息沟通的工具，开发者可以按照自己的意原采用任何架构来实现功能，没有主控节点，计算节点由程序员指定，在主从设计模式的程序中由指定的主节点控制信息的传递，在设计上有较大的自由度。Hadoop是一个分布式计算框架项目，是以架构的形式提出来的，HDFS的NameNode和MapReduce调度中的JobTracker控制其他执行节点，系统自动选择计算节点，所以如何进行分布处理对用户是透明的。

（2）MPI不提供分布式文件系统的支持，数据集中存储，由高级语言通过调用标准函数库传递消息实现并行计算。Hadoop有分布式文件系统HDFS支持，数据存储在HDFS上，由用户定制配置文件，然后编定Map和Reduce函数实现分布式的并行计算，计算时计算向存储迁移。

科学家和IT工程师对云计算技术做出了不同的选择反映了不同领域设计者从不同的角度和层次对同一事物做出不同描述和认识。MPI和Hadoop各有所长，MPI在程序设计上给程序员提供较大的自由度和Hadoop在程序设计上的简单给未来的云计算发展指出了方向。

注：以上的内容摘自[基于MPI和MapReduce的分布并行计算研究](http://wenku.baidu.com/view/f9c29bd0360cba1aa811dabc.html?pn=50)，第24页

### 14 MPI的问题

具体的说，MPI允许进程之间在任何时刻互相通信。如果一个进程挂了，我们确实可以请分布式操作系统重启之。但是如果要让这个“新生”获取它“前世”的状态，我们就需要让它从初始状态开始执行，接收到其前世曾经收到的所有消息。这就要求所有给“前世”发过消息的进程都被重启。而这些进程都需要接收到他们的“前世”接收到过的所有消息。这种数据依赖的结果就是：所有进程都得重启，那么这个job就得重头做。

一个job哪怕只需要10分钟时间，但是这期间一个进程都不挂的概率很小。只要一个进程挂了，就得重启所有进程，那么这个job就永远也结束不了了。

虽然我们很难让MPI框架做到fault recovery，我们可否让基于MPI的pLSA系统支持fault recovery呢？原则上是可以的——最简易的做法是checkpointing——时不常的把有所进程接收到过的所有消息写入一个分布式文件系统（比如GFS）。或者更直接一点：进程状态和job状态写入GFS。Checkpointing是下文要说到的Pregel框架实现fault recovery的基础。

注：这段内容摘自[大数据的首要目标是“大”而不是“快”](http://cxwangyi.github.io/story/01_plsa_and_mpi.md.html)

### 15 参考链接：
* [What are some scenarios for which MPI is a better fit than MapReduce?](http://stackoverflow.com/questions/1530490/what-are-some-scenarios-for-which-mpi-is-a-better-fit-than-mapreduce)
* [分布式计算概述](http://www.cnblogs.com/LeftNotEasy/archive/2010/11/27/1889598.html)
* [Hadoop 与 MPI 的特性](http://geron.herokuapp.com/blog/2012/03/hadoop-and-mpi)
* [分布式机器学习的故事](http://cxwangyi.github.io/2014/01/20/distributed-machine-learning/)，第一章[大数据的首要目标是“大”而不是“快”](http://cxwangyi.github.io/story/01_plsa_and_mpi.md.html)
* [知乎：写分布式机器学习算法，哪种编程接口比较好？](http://www.zhihu.com/question/22544716)
* [Is There Any Benchmarks Comparing C++ MPI with Spark](http://apache-spark-user-list.1001560.n3.nabble.com/Is-There-Any-Benchmarks-Comparing-C-MPI-with-Spark-td7661.html)
* [在雲端運算環境使用R和MPI](http://rstudio-pubs-static.s3.amazonaws.com/11810_23d0429b0ae443e28f5392a3a1c9d073.html)

----


# 第四部分 高性能地理计算
目前高性能地理计算资料较少，[在这里有一些mpi与gdal结合的程序](https://github.com/htoooth/mpi_resources/tree/master/libraries/hpgc_11)。

看看国外做了哪些东西：
* [HPGeoC](http://hpgeoc.sdsc.edu/index.html),is conducting research and development in high performance computing, data intensive computing, and grid computing to support geoscience applications, with particular emphasis on computational seismology.


----

# 第五部分 业界最新发展方向
## 1 业界最新的计算框架

* [Spark](https://github.com/apache/spark)
* [Storm](https://github.com/apache/storm)
* [Samza](http://samza.apache.org/)

## 2 关注地理大数据的组织

* [locationtech](https://www.locationtech.org/),观注地理信息的存储、计算和表达的项目。

## 3 在线工具
* [nitrous](https://www.nitrous.io/)
* [cloud9](https://c9.io/)
* [koding](https://koding.com/)
* [ideone](http://ideone.com/)
* [codebox](https://www.codebox.io/)

----


# 第六部分 云计算和大数据

## 1 运维工具

* [Func](https://fedorahosted.org/func/),python
* [Fabric](http://www.fabfile.org/),python
* [salt](http://www.saltstack.com/),python
* [ansible](http://www.ansible.com/home),python
* [Chef](https://www.getchef.com/),ruby
* [rundeck](https://github.com/rundeck/rundeck),Job scheduler and runbook automation. Enable self-service access to existing scripts and tools.
* [ruby-machine-learning](http://www.yangzhiping.com/tech/ruby-machine-learning.html),ruby
* [ruby布署](http://www.yangzhiping.com/tech/cloud.html),ruby
* [Rex](http://www.rexify.org/),perl
* [gearman](http://gearman.org/)
* [rabbitmq](http://www.rabbitmq.com/)

## 2 自动化脚本
* AHK
* [sikuli](http://www.sikuli.org/)，Sikuli automates anything you see on the screen. It uses image recognition to identify and control GUI components. It is useful when there is no easy access to a GUI internal or source code.

## 3 Rake 使用
* [ rake任务从命令行传递参数的两种方式](http://my.oschina.net/fxhover/blog/326766)
* [Rake Quick Reference](https://sites.google.com/site/spontaneousderivation/rake-quick-reference#TOC-Dependencies-Pre-requisites)
* [Rake: Automate All the Things](http://www.sitepoint.com/rake-automate-things/)
* [Using the Rake Build Language](http://martinfowler.com/articles/rake.html)
* [Using Rake to Automate Tasks](http://www.stuartellis.eu/articles/rake/)


## 4 机器学习
### 搜索

### 推荐

### 挖掘


----

# 第七部分 当前实验进展

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
