//  Project4	Socks(Proxy)
//
//  Created by Maydaycha on 2013/12/29.
//  Copyright (c) 2013年 Maydaycha. All rights reserved.
//

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

using std::cout;
using std::cerr;
using std::endl;
using std::string;

//declare function----------------------
void err_dump(const char *msg);
int passiveSock(int port, int qlen);
int passiveTCP(int port, int qlen);
int socksServer(int sockfd, struct sockaddr_in *client_addr);
void printUnsign(string des, unsigned char msg);
void printPacket(struct SOCKS4_REQUEST *request);
void setRequestInfo(struct SOCKS4_REQUEST *request, char *buf, int n);
int readline(int fd, char *ptr, int maxlen);
int splitStr(char *input, char **output, char *splitdelim);
bool checkRule(string dst_ip, string dst_port, char *permit_dst_IpPort);
bool strCompare(string str1, string str2);
int socksReplyConnectMsg(int sockfd, bool granted, SOCKS4_REQUEST *socks_request);
int socksReplyBindMsg(int sockfd, bool granted, int port_bind);
void redirectPacket(int browser_sockfd, int dstServer_sockfd);
void showRequestInfo(struct SOCKS4_REQUEST *request, bool granted, char* src_ip, int src_port, char *permit_dstIpPort);
//--------------------------------------

//define constant-----------------------
#define DEFAULT_PORT 50000
#define MAX_USERS 10
//--------------------------------------

enum SOCKS4_CONFIRM{ GRANTED, FAILED };

//socks4 structure-------------------
struct SOCKS4_REQUEST
{
	unsigned char VN;
	unsigned char CD;
	unsigned char DST_PORT[2];
	unsigned char DST_IP[4];
	char* USER_ID;
};
//--------------------------------------
int main(int argc, const char * argv[])
{
	int msockfd;
	if(argc<2)
	{
		msockfd = passiveTCP(DEFAULT_PORT, MAX_USERS);
		cerr << "Sock server start with default port: " << DEFAULT_PORT << endl;
	}
	else
	{
		msockfd = passiveTCP(atoi(argv[1]), MAX_USERS);
		cerr << "Sock server start with  port: " << argv[1] << endl;
	}
	
	while(true)
	{
		int ssockfd, pid;
		
		/* client address structure */
		socklen_t clientlen;
		sockaddr_in client_addr;
		clientlen = sizeof(client_addr);
		
		/* waiting for connection request */
		ssockfd = accept(msockfd, (struct sockaddr*)&client_addr, (socklen_t*)&clientlen);
		if(ssockfd < 0 && errno!=EINTR)
			err_dump("accept error");
		
		cerr << "********client connect successfully********" "sockfd: " << ssockfd<< endl;
		
		if((pid = fork()) < 0)
			err_dump("fork error");
		else if(pid == 0)	/* child process*/
		{
			close(msockfd);
			while(1)
			{
				socksServer(ssockfd, &client_addr);
				//				cerr << "close" << endl;
				//				close(ssockfd);
			};
			
		}
		else
		{
			close(ssockfd);
		}
	}
	
	
    return 0;
}

int socksServer(int sockfd, struct sockaddr_in *client_addr)
{
	//1. read socks4_request( VN CD DST.port DST.ip ) packet
	//1-1. read user_id ( user_id + NULL ) packet
	char buf[100000];
	bool granted;
	memset(buf, 0, sizeof(buf));
	ssize_t n = read(sockfd, buf, sizeof(buf)-1);
//	cout << "read :" << n << "bytes" << endl;
	if(n < 0)
		err_dump("read error");
	
	struct SOCKS4_REQUEST *request;
	request = (struct SOCKS4_REQUEST*)malloc(sizeof(struct SOCKS4_REQUEST));
	setRequestInfo(request, buf, (int)n);
	
//	printPacket(request);
	
	//2. check firewall
	char dst_ip[50], dst_port[10], permit_dstIpPort[50];
	memset(permit_dstIpPort, 0, sizeof(permit_dstIpPort));
	
	sprintf(dst_ip, "%d.%d.%d.%d", request->DST_IP[0], request->DST_IP[1], request->DST_IP[2], request->DST_IP[3]);
	sprintf(dst_port, "%d", request->DST_PORT[0]*256 + request->DST_PORT[1]);
	
	if(checkRule(dst_ip, dst_port, permit_dstIpPort))	granted = true;
	else	granted = false;
	
	showRequestInfo(request, granted, inet_ntoa(client_addr->sin_addr), client_addr->sin_port, permit_dstIpPort);
	
	//3. if permit ==> see request is CONNECT(1) mode or BIND(2) mode
	int dsockfd;
	if(request->CD == 1) /* connect mode */
	{
		cerr << "~~~~~~~~~~~~~~~~~~ connect mode ~~~~~~~~~~~~~~~~~~~~~~~" << endl;
		// send socks4_reply to source
		if(granted)
			socksReplyConnectMsg(sockfd, true, request);
		else
		{
			socksReplyConnectMsg(sockfd, false, request);
			return 0;
		}
		if((dsockfd = socket(AF_INET, SOCK_STREAM, 0))< 0)
			err_dump("open TCP socket error");
		
		struct sockaddr_in socks_addr;
		bzero((char*)&socks_addr, sizeof(socks_addr));
		socks_addr.sin_family = AF_INET;
		socks_addr.sin_addr.s_addr = inet_addr(dst_ip);
		socks_addr.sin_port = htons(atoi(dst_port));
		
		if(connect(dsockfd, (struct sockaddr*)&socks_addr, sizeof(socks_addr)) < 0)
			err_dump("socks connect error");
		
		// redirect packet
		redirectPacket(sockfd, dsockfd);
	}
	else if(request->CD == 2) /* BIND mode */
	{
		cerr << "~~~~~~~~~~~~~~~~~~ bind mode ~~~~~~~~~~~~~~~~~~~~~~~" << endl;
		int port_bind = rand()%10000+1024;
		
		int sockfd_waitFTP = passiveTCP(port_bind, 5);
		
		cerr << "bind mode, port " << port_bind << " is selected" << endl;
		
		socksReplyBindMsg(sockfd, true, port_bind);
		
		socklen_t FTPserver_len;
		sockaddr_in FTPserver_addr;
		FTPserver_len = sizeof(FTPserver_addr);
		
		/* waiting for connection request */
		dsockfd = accept(sockfd_waitFTP, (struct sockaddr*)&FTPserver_addr, (socklen_t*)&FTPserver_len);
		if(dsockfd < 0 && errno!=EINTR)
			err_dump("accept error");
		cerr << "********FTP server connect successfully********" "sockfd: " << dsockfd << endl;
		
		
		// after accept from FTP server, send reply to client again
		socksReplyBindMsg(sockfd, true, port_bind);
		// redirect packet
		redirectPacket(sockfd, dsockfd);
	}
	
	cerr << "************socks server close***********" << endl;
	return 0;
}


/**
 *param: port number, max_users
 *@reutrn sockfd;
 */
int passiveSock(int port, int qlen)
{
	int sockfd;
	sockaddr_in serv_addr;
	
	/* create socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0)
		err_dump("open TCP socket error");
	
	/* struct server address */
	bzero((char*)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	
	/* Tell the kernel that you are willing to re-use the port anyway */
    int yes=1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
		err_dump("setsockpt");
	
	/* bind to socket and server address */
	if(bind(sockfd,(struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        err_dump("server: cant bind local address");
	if(listen(sockfd, qlen) < 0)
		err_dump("listen error");
	
	return sockfd;
}

/**
 *param: port number, max_users
 *@reutrn sockfd(binded and listening...);
 */
int passiveTCP(int port, int qlen)
{
	return passiveSock(port, qlen);
}

void printUnsign(string des, unsigned char msg)
{
	//	cout << std::isprint(request->VN) << endl;
	cerr << des << static_cast<unsigned>(msg) << endl;
}

void setRequestInfo(struct SOCKS4_REQUEST *request, char *buf, int n)
{
	int i,k;
	request->VN = buf[0];
	request->CD = buf[1];
	request->DST_PORT[0] = buf[2]; request->DST_PORT[1] = buf[3];
	request->DST_IP[0] = buf[4]; request->DST_IP[1] = buf[5];
	request->DST_IP[2] = buf[6]; request->DST_IP[3] = buf[7];
	request->USER_ID = (char*)malloc(sizeof(char*)*(n-8));
	for(i = 0, k = 8; i< (n-1); i++)
		request->USER_ID[i] = buf[k];
	request->USER_ID[i] = '\0';
}


bool checkRule(string dst_ip, string dst_port, char *permit_dst_IpPort)
{
	char *output[10];
	bool allow = true;
	int x = 0;
	
	string filepath = "socks.conf";
	FILE* fw_conf = fopen(filepath.c_str(), "r");
	if(!fw_conf)
		err_dump("open file error");
	
	char  buffer[1000];
	
	readline(fileno(fw_conf), buffer, 1000);
//	cerr << buffer << endl;
	
	splitStr(buffer, output, (char*)" ");
	string action(output[0]); string name(output[1]); string srcIp(output[2]); string dstIp(output[3]);
	string srcPort(output[4]); string dstPort(output[5]);
	string tmpPort;
	if(strCompare(dstPort,"-"))	tmpPort = "*";
	else	tmpPort = dstPort;
	sprintf(permit_dst_IpPort, "%s(%s)", output[3], tmpPort.c_str());
	
	char* dst_ip_split[4];
	char* permit_dstIp[4];
	
	if(strCompare(action,"permit"))
	{
		if(strCompare(dstIp,"-"))
		{
			//			cerr << "is here? " << endl;
			if(strCompare(dstPort,"-")) allow = true;
			else if(strCompare(dstPort,dst_port)) allow = true;
			else allow = false;
			//			cerr << "now allow ? " << allow << endl;
		}
		else if((x = splitStr(output[3], permit_dstIp, (char*)".")) > 0)
		{
			if(strCompare(dstPort,"-")) allow = true;
			else if(strCompare(dstPort,dst_port)) allow = true;
			else allow = false;
			
			splitStr((char*)dst_ip.c_str(), dst_ip_split, (char*)".");
			
			for(int i=0; i<4; i++)
			{
				if(strncmp(permit_dstIp[i], "*", 1)!=0)
					if(strcmp(dst_ip_split[i], permit_dstIp[i]) != 0)
					{
						allow = false;
						break;
					}
			}

		}
		else allow = false;
	}
	else
	{
		if(strCompare(dstIp,"-")) allow = false;
		else if(strCompare(dstIp,dst_ip)) allow = false;
		else
		{
			if(strCompare(dstPort,"-")) allow = false;
			else if(strCompare(dstPort,dst_port)) allow = false;
			else allow = true;
		}
	}
	return allow;
}

int socksReplyConnectMsg(int sockfd, bool granted, SOCKS4_REQUEST *socks_request)
{
	char package[8];
	unsigned char CD;  // 90 request granted or 91 request reject or failed
	if(granted == true) CD = 90;
	else CD = 91;
	
	package[0] = 0;
    package[1] = CD;
	package[2] = socks_request->DST_PORT[0] / 256;
	package[3] = socks_request->DST_PORT[1] % 256;
	for(int i=0; i<4; i++) 
	    package[4+i] = socks_request->DST_IP[i];
	ssize_t n = write(sockfd, package, 8);
	if (n < 0)	return -1;
	else	return 1;
}

int socksReplyBindMsg(int sockfd, bool granted, int port_bind)
{
	char package[8];
	unsigned char CD;
	
	if(granted == true) CD = 90;
	else CD = 91;
	
	package[0] = 0;
    package[1] = CD;
	package[2] = port_bind / 256;
	package[3] = port_bind % 256;
	for(int i=0; i<4; i++)
		package[4+i] = 0;
	
	ssize_t n = write(sockfd, package, 8);
	if (n < 0)	return -1;
	else	return 1;
}

void redirectPacket(int browser_sockfd, int dstServer_sockfd)
{
	// redirect packet
	fd_set rfds, afds;
	FD_ZERO(&rfds); FD_ZERO(&afds);
	
	FD_SET(dstServer_sockfd, &afds);  // from dest server
	FD_SET(browser_sockfd, &afds);  // from browser
	
	while(true)
	{
		memcpy(&rfds, &afds, sizeof(fd_set));
		
		if(select(sizeof(fd_set), &rfds, 0, 0, 0) < 0)
			err_dump("select error");
		
		// data from dest server
		char buffer[50000];
		//		cerr << "size if buffer: " << sizeof(buffer) << endl;
		if(FD_ISSET(dstServer_sockfd, &rfds))
		{
			memset(buffer, 0, sizeof(buffer));
			ssize_t n = read(dstServer_sockfd, buffer, sizeof(buffer));
			cerr << "read: " << n << " bytes from dest server, " << buffer << endl;
			if(n < 0)
				err_dump("read error");
			else if(n == 0)	break;
			
			write(browser_sockfd, buffer, n);
//			cerr << "write " << n << " bytes to browser" << endl;
		}
		
		if(FD_ISSET(browser_sockfd, &rfds))
		{
			memset(buffer, 0, sizeof(buffer));
			ssize_t n = read(browser_sockfd, buffer, sizeof(buffer));
			cerr << "read: " << n << " bytes from browser, " << buffer << endl;
			if(n < 0)
				err_dump("read error");
			else if(n == 0) break;
			
			write(dstServer_sockfd, buffer, n);
//			cerr << "write " << n << " bytes to dst server" << endl;
		}
	}
	close(browser_sockfd);
	close(dstServer_sockfd);
}

void err_dump(const char *msg)
{
	perror(msg);
	fprintf(stderr, "%s, errno = %d\n", msg, errno);
	exit(1);
}

int readline(int fd, char *ptr, int maxlen)
{
    int n;
	ssize_t rc;
    char c;
    for(n = 0; n<maxlen; n++){
        rc = read(fd, &c, 1);
        if(rc==1){
            *ptr++=c;
            if(c=='\n') break;
        }
        else if(rc ==0){
            if(n==0) return 0;   /*END of File*/
            else break;
        }
        else return -1;
    }
    *ptr = 0;
    return n;
}

void showRequestInfo(struct SOCKS4_REQUEST *request, bool granted, char* src_ip, int src_port, char *permit_dstIpPort)
{
	string msg;
	char dst_ip[50];
	int dst_port = (request->DST_PORT[0]*256 + request->DST_PORT[1]);
	char permitDistIpPort[50];
	memset(permitDistIpPort, 0, sizeof(permitDistIpPort));
	sprintf(dst_ip, "%d.%d.%d.%d", request->DST_IP[0], request->DST_IP[1], request->DST_IP[2], request->DST_IP[3]);
	
	if(granted)
	{
		msg = "SOCKS_CONNECT GRANTED....\n";
		sprintf(permitDistIpPort, "%s(%d)", dst_ip, dst_port);
	}
	else
	{
		msg = "SOCKS_CONNECT DENIED....\n";
		strcpy(permitDistIpPort, permit_dstIpPort);
	}
	
	fprintf(stderr, "VN: %d, ", request->VN);
	fprintf(stderr, "CD: %d, ", request->CD);
	fprintf(stderr, "DST IP: %s, ", dst_ip);
	fprintf(stderr, "DST PORT: %d, ", dst_port);
	fprintf(stderr, "Permit Src = %s(%d), ",src_ip, src_port);
	fprintf(stderr, "Dst = %s, \n", permitDistIpPort);
	cerr << msg << endl;
	
	
}


void printPacket(struct SOCKS4_REQUEST *request)
{
	printUnsign("VN: ", request->VN);
	printUnsign("CD: ", request->CD);
	char IPdotdec[20];
	//	inet_ntop(AF_INET, (void*)&request->DST_IP, IPdotdec,16);
	//	cerr << "IP: " << IPdotdec << endl;
	fprintf( stderr, "DST = %d.%d.%d.%d:%d, ", request->DST_IP[0], request->DST_IP[1], request->DST_IP[2], request->DST_IP[3], (request->DST_PORT[0])*256+request->DST_PORT[1] );
	cerr << "PORT: " << request->DST_PORT[0]*256 + request->DST_PORT[1] << endl;
	cerr <<  "USERID: " <<  request->USER_ID << endl;
	
}


int splitStr(char *input, char **output, char *splitdelim)
{
	output[0] = strtok(input, splitdelim);
	int word = 0;
	while(output[word]!=NULL){
		word++;
		output[word] = strtok(NULL, splitdelim);
	}
	return word;
}

bool strCompare(string str1, string str2)
{
	return !strncmp(str1.c_str(), str2.c_str(), str2.length());
}



