==========================
选项
==========================

-------------------
常用选项
-------------------

以下是一些常用选项，更多的选项可以参考 《QEMU Emulator User Documentation》。

.. option:: -help

    显示帮助信息。

.. option:: -version

    显示版本信息。

.. option:: -machine

    选择模拟的开发板，可以输入 -machine help 获取一个完整的开发板列表。

.. option:: -cpu

    选择 CPU 类型（例如 -cpu c907），可以输入 -cpu help 获取完整的 CPU 列表。

.. option:: -nographic

    禁止所有的图形输出，模拟的串口将会重定向到命令行。

.. option:: -gdb tcp::port

    设置连接 GDB 的端口，（例如 -gdb tcp::23333, 将23333作为 GDB 的连接端口）。

.. option:: -S

    在启动时冻结 CPU ，（例如与 -gdb 配合，通过 GDB 控制继续执行）。

.. option:: -CPF

    使用网络端口 8810 输出 trace 信息给 CPF 使用。

.. option:: -csky-trace

    控制 CPF trace 的详细选项，可以输入 -csky-trace help 获取一个完整的选项列表。
