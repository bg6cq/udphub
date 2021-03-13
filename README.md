### UDP 转发程序

```
本程序用来转发UDP数据包，数据包头前14个字节分别是
发送设备CPUID（7字节）
接收设备CPUID（7字节）
```

### 程序使用：

```
1. 下载程序

cd /usr/src/
git clone https://github.com/bg6cq/udphub.git

2. 编译程序

cd /usr/src/udphub
make

3. 执行程序

/usr/src/udphub 

```
注：

以上程序启动后，会在UDP 60050 端口接收并处理数据包，如果需要使用其他端口，请用 -p xxxx 指定

请注意系统的防火墙要允许 UDP 60050 端口通信，常见的系统操作如下：

CentOS 7
```
firewall-cmd --zone=public --permanent --add-port=60050/udp
firewall-cmd --reload
```



