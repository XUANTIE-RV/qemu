==========================
使用示例
==========================

-----------------
简易示例
-----------------

下面以 CDS/剑池 CDK 中的示例 C-SKY UART 程序，来演示系统模式的使用。

该例子中如果 UART 正常工作，将在终端上输出一系列的字母等信息，否则会出现错误信息，甚至没有任何提示。

编译 UART 示例程序的具体过程可以参考 CDS/剑池 CDK 的用户手册，下面我们用 玄铁 QEMU 运行该示例编译出的 elf 文件。

.. code-block:: none

  qemu-system-riscv32 -machine smartl -kernel  /path/of/Uart.elf -nographic

QEMU会在终端，将UART示例执行过程的打印，显示如下：

  .. figure:: /figure/uart_output.png
    :align: center
    :scale: 100%
    :name: uart_output

    UART 输出


-----------------
使用 GDB 调试
-----------------

下面仍然以 QEMU 运行 UART 示例程序为例，来说明如何在使用 GDB 来调试 QEMU 上执行的程序。

玄铁 QEMU 使用与上例类似参数，并追加调试的参数，打开端口23333，等待远程 GDB 调试终端链接，具体如下命令：

.. code-block:: none

  qemu-system-riscv32 -machine smartl -kernel /path/of/Uart.elf -nographic -gdb tcp::23333 -S


如上，玄铁 QEMU 在等待远程连接到端口23333。从其他命令行窗口中，用 csky-gdb 接需要调试的 elf 文件：

.. code-block:: none

  riscv64-unknown-elf-gdb /path/of/uart_test/Uart.elf


在 GDB 的提示后，输入以下命令连接 QEMU：

.. code-block:: none

  (cskygdb) target remote localhost:23333

本例当中 GDB 连接的目标端口是由参数 -gdb tcp::23333 指定为 23333。

接下来便可以与调试硬件开发板一样使用 GDB 进行调试了。例如设断点，单步执行，查看寄存器值等操作。
