### UDP Forwarder, UDP 转发程序

```
This program is used to forward UDP packets between clients, the first 20 bytes of the packet header are
    NRL2  4 byte fixed "NRL2"
    XX    2 byte packet length
    CPUID 7 byte sending device serial number
    CPUID 7 byte receiving device serial number

本程序用来转发UDP数据包，数据包头前20个字节分别是
    NRL2  4 byte 固定的 "NRL2"
    XX    2 byte 包长度
    CPUID 7 byte 发送设备序列号
    CPUID 7 byte 接收设备序列号
```

### HOW to use / 程序使用：

Download, compile, and execute programs

下载、编译、执行程序

```
# download
cd /usr/src/
git clone https://github.com/bg6cq/udphub.git

# compile (using gcc)
cd /usr/src/udphub
make

# run
/usr/src/udphub/udphub 
```
Note:

After the above program is started, it will receive and process data packets on UDP port 60050. If you need to use other ports, please specify -p xxxx, such as:

注：

以上程序启动后，会在UDP 60050 端口接收并处理数据包，如果需要使用其他端口，请用 -p xxxx 指定，如:
```
/usr/src/udphub/udphub -p 6000
```

Please note that the system's firewall must allow UDP port 60050 communication. Common system operations are as follows:

请注意系统的防火墙要允许 UDP 60050 端口通信，常见的系统操作如下：

CentOS 7
```
firewall-cmd --zone=public --permanent --add-port=60050/udp
firewall-cmd --reload
```



