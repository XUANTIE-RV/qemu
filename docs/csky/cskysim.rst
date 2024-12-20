==========================
cskysim
==========================

cskysim 是为了简化 玄铁 QEMU 系统模式使用，提供的一个 QEMU 启动程序。
cskysim 用 xml 文件整合了常用的 QEMU 参数，为用户提供了 玄铁 虚拟环境的典型参数。
另外，cskysim 跟 QMEU 配合，还提供了动态加载外设等功能。

------------------
Linux 平台下的使用
------------------

Linux的安装目录下，soccfg 中，可以找到默认的 xml 文件，这些文件提供了通用的 cskysim 配置。

以运行 E906 Smartl SDK(从 https://www.xrvm.cn/ 下载) 为例，可以用 -soc 参数指定 xml 配置文件，用 -kernel 指定 elf 文件。如果需要调整 QEMU 参数，可以修改 xml 文件，或者直接在命令行后直接加更多的 QEMU 参数。

.. code-block:: none

    cskysim -soc soccfg/riscv32/smartl_906_cfg.xml -kernel path/of/yours.elf -nographic

--------------------
Windows 平台下的使用
--------------------

以运行 E906 Smartl SDK 为例，-soc 参数指定 xml 配置文件，其他参数用法与 QEMU 相同。

.. code-block:: none

    cskysim.exe -soc soccfg/riscv32/smartl_906_cfg.xml -kernel path/of/yours.elf -nographic

cskysim 中 xml 文件，可以来自 QEMU 二进制包中的 soccfg，也可以使用 CDS/CDK 等集成开发环境自定义配置，具体参见 CDS 或 CDK 的用户手册。
