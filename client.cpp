#include "util.h"

unsigned char seq = 0; // 初始化8位序列号
RTOCalculator rto_calculator;

HANDLE mutex = NULL;//互斥量
int head = 0, tail = -1; // head-tail是已经发送尚未确认的包

int win_status[WINDOW_SIZE];//每个窗口对应的状态
int win_timer[WINDOW_SIZE];//每个窗口对应的计时器
int win2num[WINDOW_SIZE];//窗口号码to包号码
int num2win[WINDOW_SIZE];//包号码to窗口号码
int win_counter = 0;//窗口后移的计数器

void SendPkg(Package p, SOCKET sockClient, SOCKADDR_IN addrServer)
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
	while (sendto(sockClient, (char*)&p, sizeof(p), 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR)) == -1)
	{
		printf("Client Send [%s] failed, seq=%d\n", Type, p.hm.seq);

	}
	printf("Client Send [%s] success, seq=%d\n", Type, p.hm.seq);

	if (!strcmp(Type, "ACK") || !strcmp(Type, "PSH"))
		return;
	// 开始计时
	clock_t start = clock();
	// 等待接收消息
	Package p1;
	int addrlen = sizeof(SOCKADDR);

	while (true) {
		if(recvfrom(sockClient, (char*)&p1, sizeof(p1), 0, (SOCKADDR*)&addrServer, &addrlen) > 0 && clock() - start <= WAIT_TIME) {
			// 收到消息需要验证消息类型、确认号和校验和
			u_short checkSum = checkSumVerify((u_short*)&p1, sizeof(p1));
			if ((p1.hm.type == SYN_ACK && !strcmp(Type, "SYN")) && p1.hm.ack == seq && checkSum == 0)
			{
				cout<< "Client receive [SYN_ACK] from Server, ack=" << (int)p1.hm.ack << endl; 
				return;
			}
			else if (p1.hm.type == ACK && (!strcmp(Type, "FIN")) && p1.hm.ack == 0 && checkSum == 0)
			{
				cout << "Client receive [ACK] from Server, ack=" << (int)p1.hm.ack << endl;
				return;
			}
			else {
				printf("\n11Error! Client Resend [%s], seq=%d\n\n", Type, p.hm.seq);
				//rto_calculator.handleTimeout();
				SendPkg(p, sockClient, addrServer);
				return;
			}
		}
		else {
			SendPkg(p, sockClient, addrServer);
			return;
			// 超时重传并重新计时
		}
	}

}

bool HandShake(SOCKET sockClient, SOCKADDR_IN addrServer)
{
	string con;
	cin >> con;

	cout << "====================Start Connection====================" << endl;
	Package p1;
	p1.hm.type = SYN;
	p1.hm.seq = seq;
	p1.hm.checkSum = 0;
	p1.hm.checkSum = checkSumVerify((u_short*)&p1, sizeof(p1));
	int len = sizeof(SOCKADDR);
	SendPkg(p1, sockClient, addrServer);

	seq = (seq + 1) % 256;
	p1.hm.type = ACK;
	p1.hm.ack = seq;
	p1.hm.checkSum = 0;
	p1.hm.checkSum = checkSumVerify((u_short*)&p1, sizeof(p1));

	if (sendto(sockClient, (char*)&p1, sizeof(p1), 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR)) != -1)
	{
		printf("Client send [ACK] success, ack=%d\n", seq);
		seq = (seq + 1) % 256;
		return true;
	}
	else
	{
		printf("Client send [ACK] filed\n");
		return false;
	}
}

bool WaveHand(SOCKET sockClient, SOCKADDR_IN addrServer)
{
	cout << "====================Start Disconnection====================" << endl;
	Package p1;
	p1.hm.type = FIN;
	p1.hm.seq = 0;
	p1.hm.checkSum = 0;
	p1.hm.checkSum = checkSumVerify((u_short*)&p1, sizeof(p1));
	int len = sizeof(SOCKADDR);

	SendPkg(p1, sockClient, addrServer);
	Package p4;

	while (true)
	{
		if (recvfrom(sockClient, (char*)&p4, sizeof(p4), 0, (SOCKADDR*)&addrServer, &len) > 0)
		{
			if (p4.hm.type == FIN)
			{
				printf("Client receive [FIN] from Server\n");
				p4.hm.type = ACK;
				p4.hm.ack = 1;
				p4.hm.checkSum = 0;
				p4.hm.checkSum = checkSumVerify((u_short*)&p4, sizeof(p4));
				SendPkg(p4, sockClient, addrServer);
				break;
			}
			else
			{
				printf("Server receive [FIN] ERROR\n");
				return false;
			}
		}
	}

	return true;
}

// 参数: 发送的数据包, 客户端套接字, 服务器地址, 数据长度, 文件号
bool SendMsg(char* data, SOCKET sockClient, SOCKADDR_IN addrServer, int dataLen, const char* fileName)
{
	int pack_num = dataLen / PACKET_SIZE + 1;
	head = 0, tail = -1;
	while (head != pack_num - 1 || ((head == pack_num-1) && win_status[num2win[head % WINDOW_SIZE]] == SEND_UNCONFIRMED))
	{
		WaitForSingleObject(mutex, INFINITE);
		if (tail - head +1 < WINDOW_SIZE && tail != pack_num-1) //如果没超过窗口大小，且没发到结尾
		{
			// 设置信息头
			Package p;
			p.hm.dataLen = dataLen;
			p.hm.seq = (tail + 1) % 256;
			p.hm.type = PSH;
			p.hm.checkSum = 0;
			strcpy(p.hm.fileName, fileName);
			
			if (tail != pack_num - 2)
				p.hm.len = PACKET_SIZE;
			else
				p.hm.len = dataLen % PACKET_SIZE;

			printf("ack=%d, datalen=%d\n", p.hm.seq, p.hm.dataLen);

			memcpy(p.data, data + (tail+1) * PACKET_SIZE, p.hm.len); //把本个包的数据存进去
			// 计算校验和
			p.hm.checkSum = checkSumVerify((u_short*)&p, sizeof(p));
			SendPkg(p, sockClient, addrServer);
			tail++;

			win_status[win_counter] = SEND_UNCONFIRMED;
			win_timer[win_counter] = clock();
			win2num[win_counter] = tail;
			num2win[tail%WINDOW_SIZE] = win_counter;

			win_counter = (win_counter + 1) % WINDOW_SIZE;

			printf("Client: HEAD: %d, TAIL: %d, TOTAL NUM: %d\n", head, tail, pack_num);
		}
		ReleaseMutex(mutex);


		// 超时重传
		WaitForSingleObject(mutex, INFINITE);
		for(int i=0;i<WINDOW_SIZE;i++)
		{
			if(win_status[i]==SEND_UNCONFIRMED)
			{
				if((clock() - win_timer[i]) > WAIT_TIME)
				{
					Package p;
					memset(p.data,0,sizeof(p.data));
					p.hm.dataLen = dataLen;
					p.hm.seq = win2num[i] % 256;
					p.hm.type = PSH;
					p.hm.checkSum = 0;
					strcpy(p.hm.fileName, fileName);
					
					if (win2num[i] != pack_num - 1)
						p.hm.len = PACKET_SIZE;
					else
						p.hm.len = dataLen % PACKET_SIZE;

					memcpy(p.data, data + win2num[i] * PACKET_SIZE, p.hm.len); //把本个包的数据存进去
					// 计算校验和
					p.hm.checkSum = checkSumVerify((u_short*)&p, sizeof(p));
					SendPkg(p, sockClient, addrServer);

					printf("\nTimeout! Resend Package: %d\n", win2num[i]);
					//printf("\nTimeout! Client: HEAD: %d, TAIL: %d, TOTAL NUM: %d\n", head, tail, pack_num);
					win_timer[i] = clock();
				}
			}
		}
		ReleaseMutex(mutex);

	}

	while(win_status[head%WINDOW_SIZE] == SEND_UNCONFIRMED);
	return true;
}

// 接收消息
DWORD WINAPI recvMessage(LPVOID Iparam)
{
	// 获取socket
	struct SockStruct* ss = (struct SockStruct*)Iparam;
	SOCKADDR_IN addrServer = ss->addrServer;
	SOCKET sockClient = ss->sockClient;

	Package p1;
	int addrlen = sizeof(SOCKADDR);
	while (true)
	{
		if (recvfrom(sockClient, (char*)&p1, sizeof(p1), 0, (SOCKADDR*)&addrServer, &addrlen) > 0)
		{
			int ck = checkSumVerify((u_short*)&p1, sizeof(p1));
			WaitForSingleObject(mutex, INFINITE);
			// 类型错误或校验和错误
			if (p1.hm.type != ACK || ck != 0)  
			{
				printf("\nError! Client receive wrong Packet from Server, ack = %d\n", p1.hm.ack);
				printf("Client: HEAD: %d, TAIL: %d\n", head, tail);
				continue;
			}
			else
			{
				int ack_num = 0;
				int head_seq = head % 256;
				int tail_seq = tail % 256;

				if(head_seq <= tail_seq)// head_seq和tail_seq在同一侧
				{
					if((p1.hm.ack >= head_seq && p1.hm.ack <= tail_seq) || head == tail)
					{
						ack_num = p1.hm.ack - head_seq ;
						int rec_num = head + ack_num;
						int rec_win = num2win[rec_num%WINDOW_SIZE];

						win_status[rec_win] = SEND_CONFIRMED;


						printf("Client receive [ACK] from Server, ack = %d\n", p1.hm.ack);
						printf("Client: HEAD: %d, TAIL: %d\n", head, tail);
					}
				}
				else// head_seq和tail_seq不在同一侧
				{
					if((p1.hm.ack >= head_seq && p1.hm.ack <= 256) || (p1.hm.ack >= 0 && p1.hm.ack <= tail_seq))
					{
						if(p1.hm.ack >= head_seq)
							ack_num = p1.hm.ack - head_seq ;
						else
							ack_num = 256 - head_seq + p1.hm.ack ;

						int rec_num = head + ack_num;
						int rec_win = num2win[rec_num%WINDOW_SIZE];

						win_status[rec_win] = SEND_CONFIRMED;

						printf("Client receive [ACK] from Server, ack = %d\n", p1.hm.ack);
						printf("Client: HEAD: %d, TAIL: %d\n", head, tail);

					}
				}

				int head_win = num2win[head % WINDOW_SIZE];
				int tail_win = num2win[tail % WINDOW_SIZE];

				for(int i=head_win; win_status[i]==SEND_CONFIRMED && i != tail_win; i=(i+1)%WINDOW_SIZE)
				{
					printf("Confirmed package num = %d\n", head);
					head++;
					if(i == tail_win&&win_status[i]==SEND_CONFIRMED)
						printf("Confirmed package num = %d\n", head);
				}

				printf("head: %d tail: %d\n",head, tail);
			}
			ReleaseMutex(mutex);
		}
	}
	return 0;
}

int main()
{	
	//协商使用wsa版本
	WSADATA wsaData; // 可用socket详细信息
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	printf("WSAStartup success\n");

	struct timeval timeo = { 20,0 };
	socklen_t lens = sizeof(timeo);

	SOCKET sockClient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// 设置服务器
	SOCKADDR_IN addrServer = { 0 };
	addrServer.sin_family = AF_INET;
	// 这里填写的是路由器的ip和端口
	addrServer.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");//设置为本地回环地址
	addrServer.sin_port = htons(4001);

	SOCKADDR_IN addrClient = { 0 };
	addrClient.sin_family = AF_INET;
	addrClient.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	addrClient.sin_port = htons(8081);

	if (bind(sockClient, (SOCKADDR*)&addrClient, sizeof(SOCKADDR)) != SOCKET_ERROR)
		printf("Client bind success\n");
	else
		printf("Client bind filed\n");

	int len = sizeof(SOCKADDR);

	// 三次握手
	if (HandShake(sockClient, addrServer) == true)
	{
		cout << "====================Connection Successful====================" << endl;
	}

	printf("Please input the file name which you want to send(input \"quit\" to quit):\n");

	float delay, tp;
	long long start_time, end_time, f;
	QueryPerformanceFrequency((LARGE_INTEGER*)&f);

	mutex = CreateMutex(NULL, FALSE, TEXT("screen"));
	struct SockStruct ss;
	ss.sockClient = sockClient;
	ss.addrServer = addrServer;
	HANDLE hThread = CreateThread(NULL, 0, recvMessage, (LPVOID)&ss, 0, 0);

	while (1)
	{

		string fileName;
		cin >> fileName;
		string fileDir = "test\\";
		string filePath = fileDir + fileName;
		char* data;

		if(fileName == "quit")
			break;
		else 
		{
			ifstream in(filePath, ifstream::in | ios::binary);
			int dataLen = 0;
			if (!in)
			{
				printf("Open the file failed\n");
				continue;
			}
			// 文件读取到data
			BYTE t = in.get();
			char* data = new char[100000000];
			memset(data, 0, sizeof(data));
			while (in)
			{
				data[dataLen++] = t;
				t = in.get();
			}
			in.close();

			QueryPerformanceCounter((LARGE_INTEGER*)&start_time);
			const char* c = fileName.c_str();

			SendMsg(data, sockClient, addrServer, dataLen, c);
			QueryPerformanceCounter((LARGE_INTEGER*)&end_time);
			delay = (end_time - start_time) * 1000.0 / f;
			tp = (double)dataLen / (delay/1000);
			cout<<"Client send "<<fileName;
			printf(" Finish, delay: %.4fms, throughput: %.4fbyte/s\n", delay, tp);
		}
	}

	TerminateThread(hThread, 0); //这里只能强制停止了
	CloseHandle(hThread);

	// 四次挥手
	WaitForSingleObject(mutex, INFINITE);
	if (WaveHand(sockClient, addrServer) == true)
	{

		cout << "====================DISCONNECTION SUCCESSFUL====================" << endl;
	}
	ReleaseMutex(mutex);

	closesocket(sockClient);

	WSACleanup();

	return 0;
}