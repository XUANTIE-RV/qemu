==========================
Linux 运行与调试
==========================

-------------
运行 Linux
-------------

运行RISC-V Linux操作系统( Image 从 https://github.com/XUANTIE-RV/zero_stage_boot/releases 下载，Rootfs 从 https://github.com/c-sky/buildroot/releases 下载)

.. code-block:: none

  qemu-system-riscv64 -M virt -cpu c908v -bios fw_dynamic.bin -kernel Image -append "rootwait root=/dev/vda ro" -drive file=rootfs.ext2,format=raw,id=hd0 -device virtio-blk-device,drive=hd0 -nographic -smp 1

-------------------
9pfs共享目录
-------------------

在主机中创建共享的目录

.. code-block:: none

  mkdir -p /home/csky/shared

启动QEMU，增加命令行参数

.. code-block:: none

  -fsdev local,security_model=mapped-xattr,id=fsdev0,path=/home/csky/shared -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=hostshare

内核启动后，创建目录，并和主机的目录建立共享关系

.. code-block:: none

  mkdir -p /root/host_shared
  mount -t 9p -o trans=virtio,version=9p2000.L hostshare /root/host_shared/

这样就可以通过/root/host_shared/访问主机上的/home/csky/shared

-----------------
tftp服务器
-----------------

还可以通过用户模式网络中搭建tftp服务器的方式，传入测试文件。

启动QEMU，增加命令行参数

.. code-block:: none

  -device virtio-net-device,netdev=net0 -netdev user,id=net0,tftp=/mnt/ssd/hello

主机目录 /mnt/ssd/hello 作为 ftp 服务器根目录。Guest 可通过如下命令，下载 /mnt/ssd/hello 中的 hello.elf

.. code-block:: none

  tftp -g -r hello.elf 10.0.2.2

-----------------------------
gdbserver 调试应用程序
-----------------------------

gdbserver 通过 9pfs 或 ftp 服务器方式传入 Guest Linux 中。gdbserver 通过一个网络端口对外提供服务。

启动QEMU，增加命令行参数

.. code-block:: none

  -netdev user,hostfwd=tcp:192.168.0.60:50222-10.0.2.15:1001,id=net0

命令中 “-device virtio-net-device,netdev=net0“，创建一个 virtio 的虚拟网卡。这个网卡的后端由 "-netdev user,hostfwd=tcp:192.168.0.60:50222-10.0.2.15:1001,id=net0" 描述，user 表明使用 usermode 网络。hostfwd 描述了转发 host 端口到 guest 端口的转发规则。tcp 表明对 tcp 协议转发，192.168.0.60 是 host 机 ip 地址，50222 是 host 的端口，10.0.2.15 是 guest 机 ip 地址，1001 是 guest 用于 ssh 通信的端口。

qemu启动 linux 系统后，需要设置网卡 ip

.. code-block:: none

  ifconfig eth0 10.0.2.15

增加默认路由

.. code-block:: none

  route add default gw 10.0.2.2

调试所需工具和网络环境都已配置完成，下面开始调试应用程序。

在Guest Linux中运行被调试程序，

.. code-block:: none

  gdbserver 10.0.2.15:1001 /root/host_shared/jdk/bin/java。

这时命令行端口中输出

.. code-block:: none

  Process ./jdk2/bin/java created; pid = 110
  Listening on port 1001
  Remote debugging from host 192.168.0.60, port 45920

等待gdb 连接。

在Host 中使用 gdb连接程序

.. code-block:: none

  tar remote 192.168.0.60:50222

这样就可以连接上了。
