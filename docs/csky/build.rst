==========================
编译
==========================

------------
推荐环境
------------

推荐使用以下操作系统版本：

* ubuntu 20.04 64位
* windows 10 64位

---------------------
获取可执行程序
---------------------

玄铁 QEMU 可执行程序有以下几种获取方式：

1. CDS安装后，以windows默认安装为例，可以在D:\\C-Sky\\CDS\\qemu下找到。
2. CDK安装后，以windows默认安装为例，可以在D:\\C-Sky\\CDK\\qemu下找到。
3. 从https://www.xrvm.cn/芯片开放社区获取。

---------------------
获取源代码
---------------------

源代码在玄铁github站点维护：
https://github.com/XUANTIE-RV/qemu

---------------------
编译方法
---------------------

.. code-block:: none

    git clone git@github.com:XUANTIE-RV/qemu.git
    mkdir build
    cd build
    ../configure --target-list="riscv64-softmmu riscv32-softmmu riscv64-linux-user riscv32-linux-user cskyv2-softmmu cskyv1-softmmu cskyv1-linux-user cskyv2-linux-user"
    make -j8
