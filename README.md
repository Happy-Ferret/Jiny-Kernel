##JINY (Jana's tINY kernel)
[JINY](https://github.com/naredula-jana/Jiny-Kernel) is designed from ground up for superior performance on virtual machine.

1. **What is JINY?**.
 - Jiny is a Thin/Tiny unix like kernel with interface similar to linux, linux app can directly run on it without recompiling.
 - Designed from ground up: It is designed from the ground up to give superior performance on virtual machine(VM). The performance gain will come from reducing memory and cpu overhead when compare to traditional os like linux,freebsd etc.
 - High priority versus normal priority apps: The apps running on Jiny OS are divided in to high priority and normal priority. 
     - **High priority App**: Runs single app in ring-0, kernel cooperates with app to give maximum performance by reducing memory and cpu overhead. Any linux app can be converted in to high priority app by recompiling without any modification. Recompilation is required as the syscalls in libc need to be modified. When app is wrapped  by a  thin kernel like Jiny and launched as vm, it can give better performance even when compare to the same app on the metal. The two key reasons for the gain in performance is app will be running in a ring-0  with a co-operative thin kernel in a vcpu and virtulization hardware will take the job in protecting the system from app malfunctioning.Currently Jiny can accommodate only one high priority app. The performance will be always high when compare to the similar app in the linux/freebsd vm. In apps that have high system call usage the performance will be better even when compare to the same app on the metal. JVM, memcached etc  are well suitable to run as high priority app.  
     - **Normal priority app**: can run large in number like any traditional unix system in ring-3 with performance less when compare to high priority app.  

2. **How different is Jiny from OS like Linux?**
 - **Thin kernel** : Jiny is thin kernel, it is having very small set of drivers based on virtio, since it runs on hypervisor. Currently it supports only on x86_64 architecture. this makes the size of code small when compare to linux.
 - **OS for Cloud**: designed from groundup  to run on hypervisor, So it runs faster when compare to linux.
 - **Object oriented**: Most of subsystems will be in object oriented language like c++11.
 - **Single app inside the vm**: Designed to run single application efficiently when compare to traditional os.
 - **Network packet processing**: Most of cycles in packet processing is spent in the app context(i.e at the edge) as against in the bottom half in linux, this will minimizing the locks in the SMP. Detailed description is present in the [VanJacbson paper](http://www.lemis.com/grog/Documentation/vj/lca06vj.pdf)
   
3. **For What purpose Jiny can be used?**
 - Running single apps like  JVM( tomcat or any java server), memcached  etc inside the Jiny vm as high priority app. Single app can be wrapped by a thin os like Jiny to enhance the performance.  Here the app will run much faster when compare to the same app in other vm's like freebsd or linux. Thin OS like JIny along with virtulization hardware can act like a performance enhancer for the apps on the metal.
 - Running multiple normal priority application like any traditional unix like systems with optimizations for vm. 

4. **What is the development plan and current status?**.

  **Completed**: Developing traditional unix like kernel with small foot print(Completed)
 - bringing kernel up on x86_64 without any user level app, cli using kernel shell.
 - Implementing most of the linux system calls, so that  app's on linux can run as it is. Currently app's like busybox can run as it is.
 - Running high priority app as kernel module.
    
  **In progress:** 
  - Traditional OS changes:  
     - Converting most of subsystems from c to c++11.
     - Running jvm's like jamvm,openjdk: this need to implement some missing system call like futext etc.
   -  High priority app related changes:
     - Modifying the  virtual memory layer to make high priority app run in the same space as that of the kernel.
     - Providing additional API's to app's like JVM for improving the Garbage collection.
      - Scheduler changes: To disable/minimize the interrupts on the cpu that is loaded with high priority app. This is to reduce the cpu overhead and locks.
      - Making lockless or minimizing the contention.
      - changes to libc: This will make high prority app run as app instead of loading as a kernel module.

 
###Benchmark-1(CPU centric):
**Overview:** An application execution time on the metal is compared with the same app wrapped using thin OS like Jiny and launched as vm. The single app running in Jiny vm outperforms by completing in 6 seconds when compare to the same running on the metal that took 44sec. The key reason for superior performance in Jiny os is, it accelerates the app by allowing  it to run in ring-0 so that the overhead of system calls and context switches are minimized. The protection of system from app malfunctioning is left to virtulization hardware. To run in Jiny vm, the app need to recompile without any changes using the modified c library.

**Application(app) used for testing:** A simple C application that read and writes in to a /dev/null file repeatedly in a tight loop is used, this is a system call intensive application. [The app](../master/modules/test_file/test_file.c) is executed in four environments on the metal, inside the Jiny as High priority, as normal priority inside Jiny  and inside the linux vm.   

####Completion time of app in different environments:

1. **case-1:** app as high priority inside Jiny vm:      6 sec
2. **case-2:** same app on the metal(linux host): 44 sec
3. **case-3:** same app as normal priority in Jiny vm: 55 sec
4. **case-4:** same app on the linux vm:          43 sec

####Reasons for High priority app superior performance when compare to the same app on metal:

- **From cpu point of view**: when app runs inside jiny vm(case-1), virtualization hardware(like intel VT-x) is active and it will protect system  from app malfunctioning, here app runs in ring-0 along with Jiny kernel. means if the app crashes it will not bring down the host, but only the vm crashes at the maximum. when app on the metal(case-2) runs, virtualization hardware is passive/disabled and the os surrounding(i.e host os) will make sure that the app malfunctioning will not bring down the host by running the app in ring-3. 

 One of key difference between case-1 and case-2 is, in case-1 vt-x hardware is used while in case-2 host os software does the job of system protection from app malfunctioning. Jiny OS will allow the app to run in the same address space, it does not spend cpu cycles to protect the system or os from app malfunciton, at the maximum the vm goes down since only one app is running it is equal to single app crashing without effecting the system.

        
- **From end user point of view**: To run the app inside the Jiny vm as high priority app, it need to recompile so the syscall located in libc will replaced with corresponding function calls. app is not required any change , only libc will be modified. start time in the first case will be slightly slower since vm need to be started before the app. so long running applications like servers will be suitable for high priority app.

####Benchmark-1 summary
 1. Virtulization hardware along with thin OS like Jiny can function as hardware assit  layer to speedup the app on metal. Launching single app using Jiny Vm will be useful not only from virtulization point of view but also to increase the speed.
 2. In the test,I have used syscall intensive app that as shown huge improvement when compare to app on metal, but other workload like io intensive may not give that large improvement.  Speeds of virtulization io path are improving continuously both in software or hardware,  so  io intensive  apps also will become better in the future.
 3. Most of apps as  high priority app in Jiny will  show big performance improvement when compare the same app in linux or freebsd vm's. 

###Benchmark-2(Network centric):
Network relaed app comparisons with other OS like linux. Networking in jiny is based on the  [VanJacbson paper](http://www.lemis.com/grog/Documentation/vj/lca06vj.pdf). Udp client and server is used to test the maxumum throughput of udp servers on the vm. udp client on the host sends the packet to the udp server inside the vm, udp server responds back the packet. The below shows the maximum throughput:

1. ubuntu 14(linux vm) :  94M
2. Jiny  : 155M
3. Jiny with delay in send door bell : 170M

Packet size used by udp client: 200 bytes. 
Number of cores in the linux/Jiny Vm are 2. 

####Benchmark-1 summary
 1. Between  test-2(155M) and test-3(170M):  For every packete send on the NIC, issuing the door bell cost MMIO opearation, that is causing the vm exit. postponing doorbell for few packets as improved the throughput, but this is not practical as it cause extra delay in holding the send packet. 

## Features currently Available:

- Page Cache:  LRU and MRU based (based on the published paper in opencirrus) 
- File Systems: 
   - 9p+virtio
   - Host based file system based on ivshm(Inter Vm Shared Memory) 
- Virtualization Features:
   - HighPriority Apps: very basic features is available.
   - Zerop page optimization works along with KSM.
   - Elastic Cpu's: depending on the load, some of the cpu's will be rested.
   - Elastic Memory: depending on the load, some amount of physical memory can be disabled, so as host/other vm's can use.
- Virtualization drivers:
    - Xen : xenbus, network driver using xen paravirtualised.
    - KVM : virtio + P9 filesystem
    - KVM : virtio + Network (test server with udp stack(tcp/ip))
    - KVM : virtio + memory ballooning
    - KVM : clock
- SMP: APIC,MSIX
- Networking:  Used third party tcp/ip stacks.
     - TCP/ip stack from uip ( from [AdamDunkels](https://github.com/adamdunkels/uip)  as kernel module. The above Benchamark-2 is with uip : currently only udp is supported, need to add tcp.
     - LWIP4.0 as a kernel module: 
- Debugging features:
   - memoryleak detection.
   - function tracing or call graph.
   - syscall debugging.
   - stack capture at any point. 
   - code profiling. 
- Loadable Modules:  Supports loadable kernel module. Lwip tcp/ip stack compiled as kernel module.
- User level:
   - Statically and dynamically compiled user app can be launched from kernel shell or busy box.
   - busybox shell can successfully run on top of Jiny kernel, network apps can able to use socket layer.
- Hardware: It was fully tested for x86/64. partially done for x86_32.


##Papers:
 -   [Page cache optimizations for Hadoop, published in open cirrus-2011 summit](../master/doc/PageCache-Open-Cirrus.pdf) .
 -   [Memory optimization techniques](../master/doc/malloc_paper_techpulse_submit_final.pdf).
 -   [Jiny pagecache implementation](../master/doc/pagecache.txt)

##Related Projects:
 -[Vmstate](https://github.com/naredula-jana/vmstate): Virtualmachine state capture and analysis.