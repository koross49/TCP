#include <winsock2.h> 
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include <WS2tcpip.h>
#include <ctime>
#include <math.h>
#include <queue>
#include <vector>
using namespace std;
#pragma comment(lib,"ws2_32.lib")
#pragma warning(disable:4996)

#define SYN 64 // 请求连接
#define SYN_ACK 72 // 对SYN的响应，同时表示自己也请求连接
#define ACK 8 // 确认数据包
#define FIN 128 // 请求断开连接
#define PSH 16 // 传输数据包
#define PACKET_SIZE 12000 // 数据包大小

#define SEND_UNCONFIRMED 1
#define SEND_CONFIRMED 2
// #define UNSEND 3
// #define FORBIDDEN 4

#define FILE_NAME_LEN 64

const int WAIT_TIME = 300;//客户端等待事件的时间，单位ms

const int WINDOW_SIZE = 20;//窗口大小

struct SockStruct {
    SOCKET sockClient;
    SOCKADDR_IN addrServer;
};

struct HeadMsg {
	u_long dataLen;
	u_short len;			// 数据长度，16位 最长65535位 8191个字节
	u_short checkSum;		// 校验和，16位
	unsigned char type;		// 消息类型
	unsigned char seq;		// 序列号, 实际只需要两位
	unsigned char ack;      // 确认号
	char fileName[FILE_NAME_LEN]; // 文件名
};

struct Package {
	HeadMsg hm;
	char data[PACKET_SIZE];
};

struct RecvData {
	char* data;
	int dataLen;
	char fileName[FILE_NAME_LEN]; // 文件名
};

// 校验和：每16位相加后取反，接收端校验时若结果为全0则为正确消息
u_short checkSumVerify(u_short* msg, int length) {
	int count = (length + 1) / 2; // 计算16位字的数量,sizeof取出来的是字节大小8bit
	u_long checkSum = 0;// 初始化32位校验和

	// 遍历消息中的16位字，累加到校验和
	while (count--) {
		checkSum += *msg++;

		// 如果校验和的高16位不为零，进行溢出处理
		if (checkSum & 0xffff0000) {
			checkSum &= 0xffff; // 取低16位
			checkSum++; // 溢出处理
		}
	}
	// 返回反码
	return ~(checkSum & 0xffff);
}

class RTOCalculator {
private:
    double alpha;
    double beta;
    double srtt;    // 平滑的往返时间
    double rttvar;  // 往返时间变化的方差
    double rto;     // RTO值

public:
    // 构造函数，初始化参数和初始RTO值
    RTOCalculator(double alpha = 0.125, double beta = 0.25, double initial_rto = 1)
        : alpha(alpha), beta(beta), srtt(-1), rttvar(-1), rto(initial_rto) {}

    // 更新RTO的方法，传入样本往返时间
    void updateRTO(double sampleRtt) {
        // 初始化
        if (srtt == -1) {
            srtt = sampleRtt;
            rttvar = sampleRtt / 2;
        } else {
            // 更新srtt和rttvar
            rttvar = (1 - beta) * rttvar + beta * std::abs(srtt - sampleRtt);
            srtt = (1 - alpha) * srtt + alpha * sampleRtt;
        }

        // 计算RTO
        rto = srtt + 4 * rttvar;

        // RTO的下限
        if (rto < 1) {
            rto = 1;
        }
    }

    void handleTimeout() {
        rto *= 2;
    }

    // 获取当前计算的RTO值
    double getRTO() const {
        return rto;
    }
};