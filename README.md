# pipy-modbus
## 1. 概述
实现了使用pipy读取modbus数据并发送到消息服务器功能。  
本目录下包含的主要文件：  
pipy: pipy可执行程序  
main.js: 实现脚本   
modbus-nmi.so: pipy本地模块，可以通过 Makefile 编译  
libmodbus.so:  由 libmodbus-3.1.10.tar.gz 编译  
test.sh: 测试脚本  

## 2. 硬件接口
运行本程序的设备需要接入 rs485 设备接口



