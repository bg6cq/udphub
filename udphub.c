/* 在60050 端口接收UDP包
   发送给最近10秒钟发过数据的机器

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

// 客户端有效期时间，默认10秒
#define CTIMEOUT 10

// 最长数据包
#define MAXLEN 1460

int daemon_proc = 0;
int debug = 0;
int port = 60050;

#define MAXCLIENTS 1000
int total_clients = 0;

struct {
	struct sockaddr_storage rmt;
	int slen;
	time_t last_tm;
	unsigned long int recv_pkts, recv_bytes;
	unsigned long int send_pkts, send_bytes;
} clients[MAXCLIENTS];

// 根据对方IP和端口号信息，返回client编号，并且找到时更新last_tm时间
// 如果找不到返回 -1
int find_client(struct sockaddr_storage *r, int slen)
{
	int i;
	for (i = 0; i < total_clients; i++)
		if (memcmp(&clients[i].rmt, r, slen) == 0) {
			clients[i].last_tm = time(NULL);
			if (debug)
				printf("find_client: return %d\n", i);
			return i;
		}
	if (debug)
		printf("find_client: not found, return %d\n", -1);
	return -1;
}

// 将对方IP和端口号信息加入client
// 会覆盖 last_tm > CTIMEOUT 的表相
// 或者在最后添加
// 返回添加的client编号
// 如果客户端数量到了MAXCLIENTS，返回-1
int add_client(struct sockaddr_storage *r, int slen)
{
	int i;
	time_t tm;
	tm = time(NULL);
	for (i = 0; i < total_clients; i++)
		if (clients[i].last_tm < tm - CTIMEOUT) {
			memcpy((void *)&clients[i].rmt, r, sizeof(struct sockaddr_storage));
			clients[i].last_tm = tm;
			clients[i].slen = slen;
			if (debug)
				printf("add_client: rewrite, return %d\n", i);
			return i;
		}
	if (total_clients < MAXCLIENTS) {
		i = total_clients;
		memcpy((void *)&clients[i].rmt, r, sizeof(struct sockaddr_storage));
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

void sendudp(char *buf, int len, char *host, int port)
{
	struct sockaddr_in si_other;
	int s, slen = sizeof(si_other);
	int l;
#ifdef DEBUG
	fprintf(stderr, "send to %s,", host);
#endif
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		fprintf(stderr, "socket error");
		return;
	}
	memset((char *)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(port);
	if (inet_aton(host, &si_other.sin_addr) == 0) {
		fprintf(stderr, "inet_aton() failed\n");
		close(s);
		return;
	}
	l = sendto(s, buf, len, 0, (const struct sockaddr *)&si_other, slen);
#ifdef DEBUG
	fprintf(stderr, "%d\n", l);
#endif
	close(s);
}

void usage()
{
	printf("Usage: udphub [ -h ] [ -d ] [ -p port ]\n");
	printf("    -p udp_port     UDP port, default is 60050\n");
	printf("    -d              print debug info\n");
	printf("    -h              print help\n\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	int c;
	while ((c = getopt(argc, argv, "hdp:")) != EOF)
		switch (c) {
		case 'h':
			usage();
		case 'd':
			debug = 1;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		}

	struct sockaddr_storage si_me, r;
	struct sockaddr_in *si_mev4;
	int s, rlen = sizeof(r);
	if (debug == 0)
		daemon_init();

	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		diep("socket");

	memset((char *)&si_me, 0, sizeof(si_me));
	si_mev4 = (struct sockaddr_in *)&si_me;
	si_mev4->sin_family = AF_INET;
	si_mev4->sin_port = htons(port);
	si_mev4->sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, (const struct sockaddr *)&si_me, sizeof(si_me)) == -1)
		diep("bind");

	while (1) {
		char buf[MAXLEN];
		int len;
		len = recvfrom(s, buf, MAXLEN, 0, (struct sockaddr *)&r, (socklen_t *) & rlen);
		if (len <= 0)
			continue;
		buf[len] = 0;
		if (debug) {
			if (r.ss_family == AF_INET) {
				char remoteip[200];
				int rp;
				struct sockaddr_in *rv4;
				rv4 = (struct sockaddr_in *)&r;
				rp = ntohs(rv4->sin_port);
				inet_ntop(AF_INET, &rv4->sin_addr, remoteip, 200);
				printf("RECV PKT LEN=%d from: %s:%d\n", len, remoteip, rp);
			} else {
				printf("unknown packt\n");
			}
		}
		int clientidx;
		clientidx = find_client(&r, rlen);
		if (clientidx == -1) {	// 新客户端
			clientidx = add_client(&r, rlen);
			if (clientidx == -1) {	// 客户端满了，忽略
				if (debug)
					printf("client full\n");
				continue;
			}
		}
		time_t rtm = time(NULL);
		int i;
		for (i = 0; i < total_clients; i++) {	//发送给其他机器
			if (i == clientidx)	// 跳过自己
				continue;
			if (clients[i].last_tm < rtm - CTIMEOUT)	// 跳过时间超过10秒的
				continue;
			int l;
			l = sendto(s, buf, len, 0, (const struct sockaddr *)&(clients[i].rmt), clients[i].slen);
			if (debug) {
				struct sockaddr_storage *r;
				r = (struct sockaddr_storage *)(&clients[i].rmt);
				if (r->ss_family == AF_INET) {
					char remoteip[200];
					int rp;
					struct sockaddr_in *rv4;
					rv4 = (struct sockaddr_in *)r;
					rp = ntohs(rv4->sin_port);
					inet_ntop(AF_INET, &rv4->sin_addr, remoteip, 200);
					printf("SEND PKT LEN=%d to:%d %s:%d\n", l, i, remoteip, rp);
				} else {
					printf("unknown packt\n");
				}
			}
		}
	}
	return 0;
}
