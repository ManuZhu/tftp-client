#include <time.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <windows.h>
#include <winsock2.h>
using namespace std;

//socket是文件描述符，所以可以用int类型来表示
int getUDPSocket(){
	WORD ver = MAKEWORD(2, 2);
	WSADATA WSADATA;
	int state = WSAStartup(ver, &WSADATA);
	if(state != 0) return -1;
	int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if(udp_socket == INVALID_SOCKET) return -2;
	return udp_socket;
}

sockaddr_in getAddr(const char *ip, int port){
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.S_un.S_addr = inet_addr(ip);
	return addr;
}

char *downloadPack(const char *content,int &d){
	int len = strlen(content);
	char *buf = new char[len + 2 + 2 + 5];
	buf[0] = 0x00;
	buf[1] = 0x01;
	memcpy(buf + 2, content, len);
	memcpy(buf + 2 + len, "\0", 1);
	memcpy(buf + 2 + len + 1, "octet", 5);
	memcpy(buf + 2 + len + 1 + 5, "\0", 1);
	d = len + 2 + 2 + 5;
	return buf;
}

int sendAck(SOCKET sock, sockaddr_in server, int len, int no){
	char ack[4];
	ack[0] = 0x00;
	ack[1] = 0x04;
	memcpy(ack + 2, &no, 2);
	int sendlen = sendto(sock, ack, 4, 0, (sockaddr *)&server, len);
	return sendlen;
}

bool download(const char *ip, int port, const char *filename){
	SOCKET sock = getUDPSocket();
	sockaddr_in addr = getAddr(ip, port);
	timeval tv = {3000, 0};
	int32_t b = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(timeval));

	int dataLen;
	char *sendData = downloadPack(filename, dataLen);
	int addrlen;
	int res = sendto(sock, sendData, dataLen, 0, (sockaddr *)&addr, sizeof(addr));

	if(res == SOCKET_ERROR){
		cout << "send error:" << WSAGetLastError() << endl;
		return 0;
	}
	delete[]sendData;

	FILE *f = fopen(filename, "wb");
	if(f == NULL){
		cout << "File open failed!" << endl;
		return 0;
	}

	int now = 0;
	long long ti = time(0), limit = 1;

	int tot = 0;//接受的包总量(字节数)
	clock_t start = clock();

	while(true){
		char buf[1024];
		sockaddr_in server;
		int len = sizeof(server);
		res = recvfrom(sock, buf, 1024, 0, (sockaddr *)&server, &len);
		if(res > 0){
			short flag;
			memcpy(&flag, buf, 2);
			flag = ntohs(flag);
			if(flag == 3){
				short no;
				memcpy(&no, buf + 2, 2);
				if(ntohs(no) == ntohs(now) + 1) fwrite(buf + 4, res - 4, 1, f);
				tot += 4 + res;
				cout << "Pack No" << ntohs(no) << " has received" << endl;
				now = no;
				int sendlen = sendAck(sock, server, len, no);
				if(sendlen != 4){
					cout << "send ACK error:" << WSAGetLastError() << endl;
					fclose(f);
					return 0;
				}
				if(res < 516){
					cout << "download finished!" << endl;
					break;
				}
			}
			else if(flag == 5){
				short errorcode;
				memcpy(&errorcode, buf + 2, 2);
				errorcode = ntohs(errorcode);
				cout << errorcode << endl;
				char errMsg[1024];
				int iter = 0;
				while(*(buf + iter + 4) != 0){
					memcpy(errMsg + iter, buf + iter + 4, 1);
					++iter;
				}
				cout << "Error" << errorcode << " " << errMsg << endl;
				fclose(f);
				return 0;
			}
			limit = 1;
			ti = time(0);//重新开始计时
		}
		if(time(0) - ti >= limit){
			if(!ntohs(now)){
				sendData = downloadPack(filename, dataLen);
				res = sendto(sock, sendData, dataLen, 0, (sockaddr *)&addr, sizeof(addr));
				if(res == SOCKET_ERROR){
					cout << "send error:" << WSAGetLastError() << endl;
					fclose(f);
					return 0;
				}
				delete[]sendData;
			}
			else{
				cout << "Pack No" << ntohs(now) << " has received" << endl;
				int sendlen = sendAck(sock, server, len, now);
				if(sendlen != 4){
					cout << "send ACK error:" << WSAGetLastError() << endl;
					fclose(f);
					return 0;
				}
			}
			limit *= 2;
		}
		if(limit == 8){
			cout << "time out!" << endl;
			fclose(f);
			return 0;
		}

		double during = (double)(clock() - start)/CLOCKS_PER_SEC;
		cout << "download speed: " << (int) (tot / during / 1024) << "KB/s" << endl; 
	}
	fclose(f);
	return 1;
}

char *uploadPack(const char *content,int &d){
	int len = strlen(content);
	char *buf = new char[len + 2 + 2 + 5];
	buf[0] = 0x00;
	buf[1] = 0x02;
	memcpy(buf + 2, content, len);
	memcpy(buf + 2 + len, "\0", 1);
	memcpy(buf + 2 + len + 1, "octet", 5);
	memcpy(buf + 2 + len + 1 + 5, "\0", 1);
	d = len + 2 + 2 + 5;
	return buf;
}

char *uploadDataPack(short no, char *content, int len){
	char *buf = new char[len + 2 + 2];
	buf[0] = 0x00;
	buf[1] = 0x03;
	buf[2] = (no & 65280) >> 8;
	buf[3] = (no & 255);
	memcpy(buf + 4, content, len);
	return buf;
}

bool upload(const char *ip, int port, const char *filename){
	SOCKET sock = getUDPSocket();
	sockaddr_in addr = getAddr(ip, port);
	timeval tv = {3000, 0};
	int32_t b = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(timeval));

	int dataLen;
	char *sendData = uploadPack(filename, dataLen);
	int addrlen;
	int res = sendto(sock, sendData, dataLen, 0, (sockaddr *)&addr, sizeof(addr));
	if(res == SOCKET_ERROR){
		cout << "send error:" << WSAGetLastError() << endl;
		return 0;
	}
	delete[]sendData;

	FILE *f = fopen(filename, "rb");
	if(f == NULL){
		cout << "File open failed!" << endl;
		return 0;
	}

	int now = -1;
	long long ti = time(0), limit = 1;

	int tot = 0;//发送的包总量(字节数)
	clock_t start = clock();

	char data[1024];
	int cnt, sendlen;
	while(true){
		char buf[1024];
		sockaddr_in server;
		int len = sizeof(server);
		res = recvfrom(sock, buf, 1024, 0, (sockaddr *)&server, &len);
		if(res > 0){
			short flag;
			memcpy(&flag, buf, 2);
			flag = ntohs(flag);
			if(flag == 4){
				short no;
				memcpy(&no, buf + 2, 2);
				no = ntohs(no);
				//cout << now << endl;
				cout << "Pack No" << no << " has uploaded" << endl;
				if(no == now + 1){
					now = no;
					cnt = fread(data, 1, 512, f);
					sendData = uploadDataPack(no + 1, data, cnt);
					sendlen = sendto(sock, sendData, cnt + 4, 0, (sockaddr *)&server, len);
					tot += cnt + 4;
					delete[]sendData;
					if(sendlen != cnt + 4){
						cout << "send DATA error:" << WSAGetLastError() << endl;
						fclose(f);
						return 0;
					}
					if(cnt < 512) break;
					limit = 1;
					ti = time(0);//重新开始计时
				}
			}
			else if(flag == 5){
				short errorcode;
				memcpy(&errorcode, buf + 2, 2);
				errorcode = ntohs(errorcode);
				char errMsg[1024];
				int iter = 0;
				while(*(buf + iter + 4) != 0){
					memcpy(errMsg + iter, buf + iter + 4, 1);
					++iter;
				}
				cout << "Error" << errorcode << " " << errMsg << endl;
				fclose(f);
				return 0;
			}
		}
		if(time(0) - ti >= limit){
			if(now == -1){
				sendData = uploadPack(filename, dataLen);
				res = sendto(sock, sendData, dataLen, 0, (sockaddr *)&addr, sizeof(addr));
				if(res == SOCKET_ERROR){
					cout << "send error:" << WSAGetLastError() << endl;
					fclose(f);
					return 0;
				}
				delete[]sendData;
			}
			else{
				cout << "Pack No" << now << " has uploaded" << endl;
				sendData = uploadDataPack(now + 1, data, cnt);
				sendlen = sendto(sock, sendData, cnt + 4, 0, (sockaddr *)&server, len);
				tot += cnt + 4;
				delete[]sendData;
				if(sendlen != cnt + 4){
					cout << "send DATA error:" << WSAGetLastError() << endl;
					fclose(f);
					return 0;
				}
			}
			limit += 1;
		}
		if(limit == 10){
			cout << "time out!" << endl;
			fclose(f);
			return 0;
		}

		double during = (double)(clock() - start)/CLOCKS_PER_SEC;
		cout << "upload speed: " << (int) (tot / during) << "B/s" << endl; 
	}
	fclose(f);
	return 1;
}

int main(int argc, char **argv){
	char ip[16];
	char filename[16];
	int f = 1, port = 69;
	memset(ip, 0, sizeof(ip));
	memset(filename, 0, sizeof(filename));

	for(int i = 1; i < argc; i += 2){
		int len = strlen(argv[i + 1]);
		if(argv[i][1] == 'i') memcpy(ip, argv[i + 1], len);
		else if(argv[i][1] == 'd'){
			memcpy(filename, argv[i + 1], len);
			f = 1;
		}
		else if(argv[i][1] == 'u'){
			memcpy(filename, argv[i + 1], len);
			f = -1;
		}
		else if(argv[i][1] == 'p'){
			port = 0;
			for(int j = 0; j < len; j++) (port *= 10) += argv[i + 1][j] - '0';
		}
	}

	FILE *log = fopen("tftp.log", "a");
	time_t now_time = time(0);
	char tmp[64];
    strftime(tmp, sizeof(tmp), "%Y/%m/%d %X %A ", localtime(&now_time));
	fprintf(log, "%s connection with %s %d %s %s\n", tmp, ip, port, f==1?"download":"upload", filename);

	bool flag;
	if(f == 1) flag = download(ip, port, filename);
	else if(f == -1) flag = upload(ip, port, filename);
	if(flag) fprintf(log, "%s success!\n", f==1?"download":"upload");
	else fprintf(log, "%s fail!\n", f==1?"download":"upload");
	return 0;
}