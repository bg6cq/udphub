/* 2021.03.13 bg6cq james@ustc.edu.cn

程序功能：
   在60050 端口接收UDP包
   发送给最近100秒钟发过数据的机器
   
   v1.0 转发给所有其他设备
   v1.1 根据数据包头前14字节转发
        数据包头前14字节是: 发送设备序列号7字节 + 接收设备序列号7字节
        根据数据包头信息转发给对应的设备
   v1.2 增加IPv6支持
   v1.3 数据包头格式修改
        NRL2  4 byte 固定的 "NRL2"
        XX    2 byte 包长度 
        CPUID 7 byte 发送设备序列号
        CPUID 7 byte 接收设备序列号

*/

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

// 客户端有效期时间，默认100秒
#define CTIMEOUT 100

// 最长数据包
#define MAXLEN 1460
#define CPUIDLEN 7

int daemon_proc = 0;
int debug = 0;
int port = 60050;
int ipv6 = 0;
char bind_addr[MAXLEN];

#define MAXCLIENTS 1000
int total_clients = 0;

struct {
	unsigned char CPUID[CPUIDLEN];
	struct sockaddr_storage rmt;
	int slen;
	time_t last_tm;
	unsigned long int recv_pkts, recv_bytes;
	unsigned long int send_pkts, send_bytes;
} clients[MAXCLIENTS];

// 根据CPUID 返回client编号，并更新last_tm时间和IP地址和端口信息
// 如果找不到返回 -1
int find_and_update_client(unsigned char *cpuid, struct sockaddr_storage *r, int slen)
{
	int i;
	for (i = 0; i < total_clients; i++)
		if (memcmp(&clients[i].CPUID, cpuid, CPUIDLEN) == 0) {
			memcpy((void *)&clients[i].rmt, r, slen);
			clients[i].last_tm = time(NULL);
			if (debug)
				printf("find_client: return %d\n", i);
			return i;
		}
	if (debug)
		printf("find_client: not found, return %d\n", -1);
	return -1;
}

// 将对方CPUID、IP地址和端口号信息加入client
// 会覆盖 last_tm > CTIMEOUT 的表相
// 或者在最后添加
// 返回添加的client编号
// 如果客户端数量到了MAXCLIENTS，返回-1
int add_client(unsigned char *cpuid, struct sockaddr_storage *r, int slen)
{
	int i;
	time_t tm;
	tm = time(NULL);
	for (i = 0; i < total_clients; i++)
		if (clients[i].last_tm < tm - CTIMEOUT) {
			memcpy((void *)&clients[i].CPUID, cpuid, CPUIDLEN);
			memcpy((void *)&clients[i].rmt, r, slen);
			clients[i].last_tm = tm;
			clients[i].slen = slen;
			if (debug)
				printf("add_client: rewrite, return %d\n", i);
			return i;
		}
	if (total_clients < MAXCLIENTS) {
		i = total_clients;
		memcpy((void *)&clients[i].CPUID, cpuid, CPUIDLEN);
		memcpy((void *)&clients[i].rmt, r, slen);
		clients[i].last_tm = tm;
		clients[i].slen = slen;
		total_clients++;
		if (debug)
			printf("add_client: new, return %d\n", i);
		return i;
	}
	if (debug)
		printf("add_client: client full, return %d\n", -1);
	return -1;
}

void diep(char *s)
{
	if (daemon_proc)
		syslog(LOG_CRIT, "%s: %s\n", s, strerror(errno));
	else
		perror(s);
	exit(1);
}

void daemon_init(void)
{
	int i;
	pid_t pid;
	if ((pid = fork()) != 0)
		exit(0);	/* parent terminates */
	/* 41st child continues */
	setsid();		/* become session leader */
	signal(SIGHUP, SIG_IGN);
	if ((pid = fork()) != 0)
		exit(0);	/* 1st child terminates */
	chdir("/");		/* change working directory */
	umask(0);		/* clear our file mode creation mask */
	for (i = 0; i < 3; i++)
		close(i);
	daemon_proc = 1;
	openlog("relayudp", LOG_PID, LOG_DAEMON);
}

char *print_addr(struct sockaddr *r, socklen_t rlen)
{
	static char remoteaddr[200];
	if (r->sa_family == AF_INET) {
		int rp;
		struct sockaddr_in *rv4;
		rv4 = (struct sockaddr_in *)r;
		rp = ntohs(rv4->sin_port);
		inet_ntop(AF_INET, &rv4->sin_addr, remoteaddr, 200);
		sprintf(remoteaddr + strlen(remoteaddr), ":%d", rp);
		return remoteaddr;
	} else if (r->sa_family == AF_INET6) {
		int rp;
		struct sockaddr_in6 *rv6;
		rv6 = (struct sockaddr_in6 *)r;
		rp = ntohs(rv6->sin6_port);
		inet_ntop(AF_INET6, &rv6->sin6_addr, remoteaddr, 200);
		sprintf(remoteaddr + strlen(remoteaddr), ":%d", rp);
		return remoteaddr;
	}
	strcpy(remoteaddr, "Unknow Addr");
	return remoteaddr;
}

char *print_cpuid(unsigned char *cpuid)
{
	static char cpuidstr[CPUIDLEN * 2 + 1];
	int i;
	for (i = 0; i < CPUIDLEN; i++)
		sprintf(cpuidstr + i * 2, "%02X", cpuid[i]);
	cpuidstr[CPUIDLEN * 2] = 0;
	return cpuidstr;
}

void usage()
{
	printf("Usage: udphub [ -h ] [ -d ] [ -6 ] [ -b bind_addr ] [ -p port ]\n");
	printf("    -6              enable ipv6 support\n");
	printf("    -b bind_addr    listen address, default is 0.0.0.0\n");
	printf("    -p udp_port     UDP port, default is 60050\n");
	printf("    -d              print debug info\n");
	printf("    -h              print this help\n\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	int c;
	while ((c = getopt(argc, argv, "hd6p:b:")) != EOF)
		switch (c) {
		case 'h':
			usage();
		case 'd':
			debug = 1;
			break;
		case '6':
			ipv6 = 1;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'b':
			snprintf(bind_addr, MAXLEN - 1, optarg);
			break;
		}

	struct sockaddr_storage si_me, r;
	int s, rlen = sizeof(r);
	if (debug == 0)
		daemon_init();

	memset((char *)&si_me, 0, sizeof(si_me));
	if (ipv6 == 0) {
		struct sockaddr_in *si_mev4;
		if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
			diep("socket");

		si_mev4 = (struct sockaddr_in *)&si_me;
		si_mev4->sin_family = AF_INET;
		si_mev4->sin_port = htons(port);
		if (bind_addr[0])
			si_mev4->sin_addr.s_addr = inet_addr(bind_addr);
		if (bind(s, (const struct sockaddr *)&si_me, sizeof(si_me)) == -1)
			diep("bind");
	} else {
		struct sockaddr_in6 *si_mev6;
		if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) == -1)
			diep("socket");
		si_mev6 = (struct sockaddr_in6 *)&si_me;
		si_mev6->sin6_family = AF_INET6;
		si_mev6->sin6_port = htons(port);
		if (bind_addr[0])
			inet_pton(AF_INET6, bind_addr, &si_mev6->sin6_addr);
		if (bind(s, (const struct sockaddr *)&si_me, sizeof(si_me)) == -1)
			diep("bind");
	}

	while (1) {
		unsigned char buf[MAXLEN];
		int len;
		len = recvfrom(s, buf, MAXLEN, 0, (struct sockaddr *)&r, (socklen_t *) & rlen);
		if (len <= 0)
			continue;
		buf[len] = 0;
		if (debug) {
			printf("RECV PKT LEN=%d", len);
			printf(" from: %s", print_addr((struct sockaddr *)&r, rlen));
			printf(" %s -> ", print_cpuid(buf + 6));
			printf("%s\n", print_cpuid(buf + 6 + CPUIDLEN));
		}
		if (len < CPUIDLEN + 6)
			continue;
		if (memcmp(buf, "NRL2", 4) != 0)
			continue;
		int clientidx;
		clientidx = find_and_update_client(buf + 6, &r, rlen);
		if (clientidx == -1) {	// 新客户端
			clientidx = add_client(buf + 6, &r, rlen);
			if (clientidx == -1) {	// 客户端满了，忽略
				if (debug)
					printf("client full\n");
				continue;
			}
		}
		if (len < CPUIDLEN * 2 + 6)
			continue;
		time_t rtm = time(NULL);
		int i;
		for (i = 0; i < total_clients; i++) {	//发送给其他机器
			if (clients[i].last_tm < rtm - CTIMEOUT)	// 跳过时间超过10秒的
				continue;
			if (memcmp(buf + 6 + CPUIDLEN, clients[i].CPUID, CPUIDLEN) == 0) {
				int l;
				l = sendto(s, buf, len, 0, (const struct sockaddr *)&(clients[i].rmt), clients[i].slen);
				if (debug) {
					struct sockaddr_storage *r;
					r = (struct sockaddr_storage *)(&clients[i].rmt);
					int rlen = clients[i].slen;
					printf("SEND PKT LEN=%d", l);
					printf(" to:%s", print_addr((struct sockaddr *)r, rlen));
					printf(" CPUID %s\n\n", print_cpuid(clients[i].CPUID));
				}
				break;
			}
		}
	}
	return 0;
}
