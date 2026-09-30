# LHandProLib 2026-09-21

此目录保留厂家原始 C/C++ 头文件，以及 Linux aarch64、x86_64 的 Release 动态库。二进制未修改，不包含调试库或厂家示例。

来源：工作区附件 `LHandProLib-API-Linux-aarch64-20260921` 与 `LHandProLib-API-Linux-x86_64-20260921`。两种架构的 C++ 头文件相同，统一放在 `include/`。厂家文件的权利及分发条件仍由厂家规定。

CMake 根据目标架构选择库，复制到对应 CMake 构建目录的 `lib/`，安装时随 C++、Python 或 ROS 产物携带，并配置相对运行时搜索路径。无需外部厂家 SDK 目录；仍需要系统 C/C++ 运行库和 Linux SocketCAN。统一构建入口与安装布局见 [构建指南](../../docs/build.md)。
