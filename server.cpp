#include "util.h"

unsigned char lastAck = -1;
int rec_num = 0;
RTOCalculator rto_calculator;
Package recbuf[WINDOW_SIZE];


void SendPkg(Package p, SOCKET sockSrv, SOCKADDR_IN addrClient)
{
	char Type[10];
	switch (p.hm.type) 
	{
		case SYN: strcpy(Type, "SYN"); break;
		case SYN_ACK: strcpy(Type, "SYN_ACK"); break;
		case ACK: strcpy(Type, "ACK"); break;
		case FIN: strcpy(Type, "FIN"); break;
		case PSH:strcpy(Type, "PSH"); break;
	}
	// 发送消息
	while (sendto(sockSrv, (char*)&p, sizeof(p), 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR)) == -1)
	{
		printf("Server send [%s] failed, ack=%d\n", Type, p.hm.ack);

	}
	printf("Server send [%s] success, ack=%d\n", Type, p.hm.ack);

	if (!strcmp(Type, "ACK"))
		return;
	// 开始计时
	clock_t start = clock();
	// 等待接收消息
	Package p1;
	int addrlen = sizeof(SOCKADDR);

	while (true) {
		if (recvfrom(sockSrv, (char*)&p1, sizeof(p1), 0, (SOCKADDR*)&addrClient, &addrlen) > 0 && clock() - start <= WAIT_TIME) {
			u_short ckSum = checkSumVerify((u_short*)&p1, sizeof(p1));
			// 收到消息需要验证消息类型、序列号和校验和
			if (p1.hm.type == ACK && ckSum == 0)
			{
				printf("Server receive [ACK] from Client\n");
				return;
			}
			else {
				SendPkg(p, sockSrv, addrClient);
				return;
				// 差错重传并重新计时
			}
		}
		else {
			//rto_calculator.handleTimeout();
			SendPkg(p, sockSrv, addrClient);
			return;
			// 超时重传
		}
	}
}

bool HandShake(SOCKET sockSrv, SOCKADDR_IN addrClient)
{
	Package p2;
	int len = sizeof(SOCKADDR);
	while (true)
	{
		if (recvfrom(sockSrv, (char*)&p2, sizeof(p2), 0, (SOCKADDR*)&addrClient, &len) > 0)
		{
			cout << "====================CONNECTION====================" << endl;
			int ck = checkSumVerify((u_short*)&p2, sizeof(p2));
			if (p2.hm.type == SYN && ck == 0)
			{
				printf("Server receive [SYN] from Client\n");
				Package p3;
				p3.hm.type = SYN_ACK;
				p3.hm.ack = (lastAck + 1) % 256;
				lastAck = (lastAck + 2) % 256;
				p3.hm.checkSum = 0;
				p3.hm.checkSum = checkSumVerify((u_short*)&p3, sizeof(p3));
				SendPkg(p3, sockSrv, addrClient);
				break;
			}
			else
			{
				printf("Server receive [SYN] filed\n");
				return false;
			}
		}
	}
	return true;
}

bool WaveHand(SOCKET sockSrv, SOCKADDR_IN addrClient, Package p2)
{
	int len = sizeof(SOCKADDR);

	u_short ckSum = checkSumVerify((u_short*)&p2, sizeof(p2));
	if (p2.hm.type == FIN && ckSum == 0)
	{
		printf("Server receive [FIN] from Client\n");
		Package p4;
		p4.hm.type = ACK;
		p4.hm.ack = 0;
		p4.hm.checkSum = 0;
		p4.hm.checkSum = checkSumVerify((u_short*)&p4, sizeof(p4));
		SendPkg(p4, sockSrv, addrClient);
	}
	else
	{
		printf("Server receive [FIN] failed\n");
		return false;
	}
	Package p3;
	p3.hm.type = FIN;
	p3.hm.seq = 1;
	p3.hm.checkSum = 0;
	p3.hm.checkSum = checkSumVerify((u_short*)&p2, sizeof(p2));
	SendPkg(p3, sockSrv, addrClient);

	return true;
}

RecvData RecvMsg(SOCKET sockSrv, SOCKADDR_IN addrClient)
{
	Package p1;
	int addrlen = sizeof(SOCKADDR);
	int totalLen = 0;
	RecvData recvdata;
	int lastAck = 0;
	recvdata.data = new char[100000000];
	// 等待接收消息
	while (true) {
		// 收到消息需要验证校验和及序列号
		if (recvfrom(sockSrv, (char*)&p1, sizeof(p1), 0, (SOCKADDR*)&addrClient, &addrlen) > 0)
		{
			if (p1.hm.type == FIN)
			{
				cout << "====================DISCONNECTION====================" << endl;
				strcpy(recvdata.fileName, "quit");
				WaveHand(sockSrv, addrClient, p1);
				break;
			}
			Package p2;
			p2.hm.checkSum = p2.hm.dataLen = p2.hm.len = 0;

			int ck = !checkSumVerify((u_short*)&p1, sizeof(p1));

			if (p1.hm.type == PSH && ck == 1)  
			{
				
				if(p1.hm.seq == lastAck)
				{
					p2.hm.type = ACK;
					p2.hm.ack = lastAck;
					p2.hm.checkSum = checkSumVerify((u_short*)&p2, sizeof(p2));
					while (sendto(sockSrv, (char*)&p2, sizeof(p2), 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR)) == -1)
						printf("Server send [ACK] failed, ack=%d\n", p2.hm.ack);
					printf("lastack, Server send [ACK] success, ack=%d\n", p2.hm.ack);

					
					int la_num = rec_num % WINDOW_SIZE;
					recbuf[la_num] = p1;

					for(int i=la_num; recbuf[i].hm.dataLen!=0; i = (i+1)%WINDOW_SIZE)
					{
						printf("commit ack=%d, len=%d\n",lastAck, (int)recbuf[i].hm.len);
						lastAck = (lastAck + 1) % 256;
						rec_num = rec_num + 1;
						memcpy(recvdata.data + totalLen, (char*)&recbuf[i] + sizeof(recbuf[i].hm), recbuf[i].hm.len);
						totalLen += (int)recbuf[i].hm.len;
						recvdata.dataLen = recbuf[i].hm.dataLen;
						strcpy(recvdata.fileName, recbuf[i].hm.fileName);
						memset(&recbuf[i], 0, sizeof(recbuf[i]));
					}

					printf("totalLen=%d, dataLen=%d\n", totalLen, p1.hm.dataLen);
					if (totalLen == p1.hm.dataLen)
					{
						break;
					}
				}
				else
				{
					p2.hm.type = ACK;
					p2.hm.ack = p1.hm.seq;
					p2.hm.checkSum = checkSumVerify((u_short*)&p2, sizeof(p2));
					while (sendto(sockSrv, (char*)&p2, sizeof(p2), 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR)) == -1)
						printf("Server send [ACK] failed, ack=%d\n", p2.hm.ack);
					printf("lastAck=%d, Server send [ACK] success, ack=%d\n", lastAck, p2.hm.ack);

					if(p1.hm.seq < lastAck)
					{
						if((lastAck-p1.hm.seq) > WINDOW_SIZE)
						{
							int rnum = (256 - lastAck + p1.hm.seq) + rec_num;
							recbuf[rnum%WINDOW_SIZE] = p1;
						}
					}
					else
					{
						int rnum = rec_num + (p1.hm.seq - lastAck);
						recbuf[rnum%WINDOW_SIZE] = p1;
					}
				}
			}
		}
	}
	return recvdata;
}

int main()
{
	WSADATA wsaData; //可用socket详细信息
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	printf("WSAStartup success\n");

	struct timeval timeo = { 20,0 };
	socklen_t lens = sizeof(timeo);

	SOCKET sockSrv = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	//服务端
	SOCKADDR_IN addrSrv = { 0 };
	addrSrv.sin_family = AF_INET;//用AF_INET表示TCP/IP协议。
	addrSrv.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");//设置为本地回环地址
	addrSrv.sin_port = htons(8080);

	if (bind(sockSrv, (SOCKADDR*)&addrSrv, sizeof(SOCKADDR)) != SOCKET_ERROR)
		printf("Server bind success\n");
	else
		printf("Server bind failed\n");

	// 这里填写的是路由器的ip和端口
	SOCKADDR_IN addrClient = { 0 };
	addrClient.sin_family = AF_INET;
	addrClient.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	addrClient.sin_port = htons(4001);

	int len = sizeof(SOCKADDR);


	if (HandShake(sockSrv, addrClient) == true)
	{
		cout << "====================CONNECTION SUCCESSFUL====================" << endl;
	}

	string fileDir = "file\\";
	while (1)
	{
		RecvData recvdata;
		recvdata = RecvMsg(sockSrv, addrClient);
		string fileName(recvdata.fileName);

		if (fileName == "quit")
			break;
		
		string filePath = fileDir + fileName;
		ofstream out(filePath, ofstream::binary);
		for (int i = 0; i < recvdata.dataLen; i++)
		{
			out << recvdata.data[i];
		}
		out.close();

		cout << "receive " << fileName << " success" << endl;
	}
	cout << "====================DISCONNECTION SUCCESSFUL====================" << endl;

	closesocket(sockSrv);

	WSACleanup();

	return 0;
}