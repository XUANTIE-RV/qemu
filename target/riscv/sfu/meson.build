# qemu/meson.build 的开头部分添加
add_languages('cpp')  # 添加 C++ 支持

cpp = meson.get_compiler('cpp')

# 定义 C++ 编译参数（如 C++11 标准）
cpp_args = ['-std=c++11', '-Wno-error']

# 将 C++ 文件编译为静态库或直接链接到主程序
libsfu = static_library(
  'sfu',
  sources: 'sfu_cmodel.cpp',
  cpp_args: cpp_args,
  include_directories: include_directories('../../../include'),  # 头文件路径
  dependencies: [cpp.find_library('stdc++')],  # 依赖项（如需要链接其他库）
)

# 将库链接到主程序
libsfu_dep = declare_dependency(
    link_with: libsfu,
    include_directories: ('../../../include'),
    compile_args: [])

