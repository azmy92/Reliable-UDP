/*
 udp-server: UDP/IP sockets example
 keep reading data from the socket, echoing
 back the received data.

 usage:	echoserver [-d] [-p port]
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>	/* needed for os x */
#include <string.h>	/* for memset */
#include <sys/socket.h>
#include <arpa/inet.h>	/* defines inet_ntoa */
#include <netinet/in.h>
#include <sys/errno.h>   /* defines ERESTART, EINTR */
#include <sys/wait.h>    /* defines WNOHANG, for wait() */
#include <unistd.h>
#include "port.h"       /* defines default port */
#include <sys/types.h>
#include <vector>
#include <pthread.h>
#include <sys/time.h>
#include <fstream>
typedef unsigned char uchar;
#ifndef ERESTART
#define ERESTART EINTR
#endif

int TIMEOUT = 5000;
int TIMERSTOP = -100;

using namespace std;

extern int errno;

ofstream logFile;
ifstream infoFile;

timeval time1;
timeval time2;

int timeCount = 0;
double startTime;
struct ack_packet {
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t ackno;
};
/* Data-only packets */
struct packet {
	/* Header */
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t seqno;
	/* Data */
	char data[500]; /* Not always 500 bytes, can be less */
};
int port = 0;
void sig_func(int);
int prepareFile(char*);
void chunkizeAndSend(char*);
void serve(int port); /* main server function */
void disconn(void);
void convertDataPacketToByte(packet*, char*);
void parseAckPacket(char *, int, ack_packet*);
void* receiveAck(void *);
void* receiveAckStopAndWait(void *);
void chunkizeAndSendStopAndWait(char*);
void *SelectiveTimerCallBack(void *time);
FILE* fp;

vector<char*> *frames = new vector<char*>;
vector<bool> *ackedFrames = new vector<bool>;
vector<int> *times = new vector<int>;
vector<int> *lens = new vector<int>;

int MaxBufferSize = 500;
int base = 0;
int MaxWindow = 100;
int curr = 0;
int portIncreamenter = 1;
int childSocket;
bool stopAndWait = false;
pthread_t recThread;
pthread_t stopAndWaitTimerThread;
pthread_t SelectiveThread;

bool stopAndWaitAcked = false;
bool recThreadIsCreated = false;

pthread_mutex_t Selectivemutex;

pthread_mutex_t StopAndWaitmutex;

pthread_mutex_t StopAndWaitmutexTimer;

pthread_mutex_t StopAndWaitRecThreadCreationMutex;

pthread_mutex_t lock1;
pthread_mutex_t lock2;

/* serve: set up the service */
struct sockaddr_in my_addr; /* address of this service */
struct sockaddr_in child_addr; /* address of this service */
struct sockaddr_in client_addr; /* client's address */
int svc; /* listening socket providing service */
int rqst; /* socket accepting the request */
int dropCount = 0;

float plp = 0.01;
bool isdropped() {
	float dropCountFloat = dropCount;
	float drop = dropCountFloat * plp;

	if (drop >= 0.9) {
		dropCount = 0;
		return true;
	} else {
		return false;
	}

}

void *receiveAckStopAndWait2(void *threadid) {

	while (true) {
		pthread_mutex_lock(&lock2);

		//stopAndWaitAcked = false;
		int packetCount = (int) threadid;
		socklen_t alen = sizeof(client_addr); /* length of address */
		int reclen;
		char Buffer[8];
		cout << "waiting for ack #" << packetCount << endl;
		bool waitEnd = false;

		while (!waitEnd) {
			stopAndWaitAcked = false;
			if (reclen = (recvfrom(childSocket, Buffer, 8, 0,
					(struct sockaddr *) &client_addr, &alen)) < 0)
				perror("recvfrom() failed");

			pthread_mutex_lock(&StopAndWaitmutexTimer);
			//parse to ack_packet
			ack_packet packet;
			parseAckPacket(Buffer, 8, &packet);
			cout << "ack #" << packet.ackno << " received" << endl;

			//dublicate ack ignore
			if (packet.ackno != packetCount) {
				cout << "out of order ack...wait for right sequence # "
						<< packetCount << endl;
				pthread_mutex_unlock(&StopAndWaitmutexTimer);
				//pthread_mutex_unlock(&StopAndWaitmutexTimer);
			} else {

				cout << "killing Timer" << endl;
				//right ack received

				//pthread_kill(stopAndWaitTimerThread, 0);
				waitEnd = true;
				stopAndWaitAcked = true;
				//kill the timerthread

			}
		}
		pthread_mutex_unlock(&StopAndWaitmutexTimer);

		//unlock the sender
		pthread_mutex_unlock(&lock1);
		pthread_mutex_lock(&lock2);

	}
}

void *StopAndWaittimerCallBack3(void *time) {

	int sleeptime = 3 * 100000;
	//sleep(10);
	int timeHolder = sleeptime;
	while (true) {
		pthread_mutex_lock(&StopAndWaitRecThreadCreationMutex);
		sleep(0.2);
		pthread_mutex_lock(&StopAndWaitmutexTimer);
		sleeptime--;
		if (stopAndWaitAcked == true) {
			// reset ur self
			//cout << "Timer is Looping = " << sleeptime << endl;
			sleeptime = timeHolder;
			//cout << "Timer is Looping = " << sleeptime << endl;

		} else if (sleeptime <= 0) {
			sleeptime = timeHolder;
			timeCount++;
			cout << "TIMEEEE OUTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT" << endl;
			pthread_kill(recThread, 0);
			recThreadIsCreated = false;
			//sleep(1);
			stopAndWaitAcked = false;
			pthread_mutex_unlock(&lock1);
			pthread_mutex_unlock(&lock2);

			//pthread_mutex_unlock(&StopAndWaitmutex);

		}
		pthread_mutex_unlock(&StopAndWaitmutexTimer);
		pthread_mutex_unlock(&StopAndWaitRecThreadCreationMutex);
	}

}

void chunkizeAndSendStopAndWait2(char* fileName) {
	bool timerStarted = false;
	int size = prepareFile(fileName);
	packet currPacket;

	cout << "File Size : " << size << endl;
	int packetCount = 0;

	int couny = 0;
	dropCount = 0;
	for (int i = 0; i < size;) {
		dropCount++;
		dropCount = dropCount % 101;
		currPacket.cksum = 1;
		currPacket.len = fread(currPacket.data, 1, 100, fp);
		currPacket.seqno = packetCount;
		//	cout << "******		read Size data = " << currPacket.len << endl;
		i += currPacket.len;
		//cout << "I = " << i << endl;
		/////////////////// send currPacket
		char buffer[108];
		//cout << "Sending packet " << i << " to " << client_addr.sin_addr
		//	<< " port " << SERVICE_PORT << "\n" << endl;
		unsigned int sendFlag = 1;
		convertDataPacketToByte(&currPacket, buffer);
		bool acked = false;

		stopAndWaitAcked = false;
		while (!acked) {
			//cout << "Packet Not Acked yet" << endl;
			//send packet
			if (!isdropped()) {
				while (1) {
					sendFlag = sendto(childSocket, buffer, currPacket.len + 8,
							0, (struct sockaddr *) &client_addr,
							sizeof(client_addr));
					if (sendFlag == 0) {
						cout << "send flag = zeroooooooooooo" << endl << endl;
						break;/////============================================================================ ///////////////////////////////
					} else if (sendFlag < -1)
						break;
					else if (sendFlag == -1)
						perror("sendto");
				}
				cout << "******		" << packetCount << " Packet Sent" << endl;
			} else {
				cout << "Package Dropped" << endl;
			}
			////////////////////////////////////////////
			//TODO start timer

			pthread_mutex_lock(&lock1);

			pthread_mutex_lock(&StopAndWaitRecThreadCreationMutex);
			if (recThreadIsCreated == false) {
				while (1) {
					int rc = pthread_create(&recThread, NULL,
							receiveAckStopAndWait2, (void *) packetCount);
					recThreadIsCreated = true;
					pthread_detach(recThread);
					if (rc) {
						cout << "error in starting the Stop and wait ack thread"
								<< endl;
						sleep(1);
					} else {
						break;
					}
				}
				recThreadIsCreated = true;
			} else {

				pthread_mutex_unlock(&lock2);

			}
			pthread_mutex_unlock(&StopAndWaitRecThreadCreationMutex);
			//wait for receiver

			if (!timerStarted) {
				timerStarted = true;
				int timeout = 3;
				while (1) {	// until succeed in creation of pthread
					int rc2 = pthread_create(&stopAndWaitTimerThread, NULL,
							StopAndWaittimerCallBack3, (void *) timeout);
					if (rc2) {
//					cout << "error in starting the Stop and wait timer thread"
//							<< endl;
						//	sleep(1);
					} else {
						pthread_detach(stopAndWaitTimerThread);
						break;

					}
				}
			}
			++couny;
			cout << "---------------- " << couny << endl;
			//open thread
			//force order

			pthread_mutex_lock(&lock1);

			cout << "-----------------SENDER UNLOCKED1--------------" << endl;
			pthread_mutex_lock(&StopAndWaitmutexTimer);

			cout << "-----------------SENDER UNLOCKED2--------------" << endl;

			if (stopAndWaitAcked == true) {
				//ack is received
				stopAndWaitAcked = false;
				acked = true;
			} else {
				//ack is lost resend packet
				acked = false;
			}

			pthread_mutex_unlock(&lock1);
			pthread_mutex_unlock(&StopAndWaitmutexTimer);

			//////////////////////////////////////////
		}

		packetCount++;
		packetCount = packetCount % 2;

	}
	////////////////////////////////// sending finish file packet

	currPacket.cksum = 1;
	currPacket.len = 150;
	currPacket.seqno = packetCount;
	cout << "******		Sending finish packet." << endl;

	/////////////////// send currPacket

	char dataFinish[8];
	//cout << "Sending packet " << i << " to " << client_addr.sin_addr
	//	<< " port " << SERVICE_PORT << "\n" << endl;
	int sendFlag = 1;
	convertDataPacketToByte(&currPacket, dataFinish);

	sendFlag = sendto(childSocket, dataFinish, sizeof(dataFinish), 0,
			(struct sockaddr *) &client_addr, sizeof(client_addr));

	if (sendFlag == -1)
		perror("sendto");

////////////////////
	pthread_kill(stopAndWaitTimerThread, 0);
	exit(0);
////////////////////////////////////////////////// finish
	cout << "******		Sending finish packet DONE." << endl;
//	cout << "Out Of order Count = " << outOfOrder << endl;
}

void *StopAndWaittimerCallBack2(void *time) {

	int sleeptime = 3 * 100000;
	//sleep(10);
	int timeHolder = sleeptime;
	while (true) {
		pthread_mutex_lock(&StopAndWaitRecThreadCreationMutex);
		//sleep(0.2);
		pthread_mutex_lock(&StopAndWaitmutexTimer);
		sleeptime--;
		if (stopAndWaitAcked == true) {
			// reset ur self
			//cout << "Timer is Looping = " << sleeptime << endl;
			sleeptime = timeHolder;
			//cout << "Timer is Looping = " << sleeptime << endl;

		} else if (sleeptime <= 0) {
			sleeptime = timeHolder;
			timeCount++;
			cout << "TIMEEEE OUTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT" << endl;
			logFile << "-----------------TIME OUT-------------------------"
					<< endl;
			pthread_cancel(recThread);
			//sleep(1);
			stopAndWaitAcked = false;
			pthread_mutex_unlock(&StopAndWaitmutex);

			//pthread_mutex_unlock(&StopAndWaitmutex);

		}
		pthread_mutex_unlock(&StopAndWaitmutexTimer);
		pthread_mutex_unlock(&StopAndWaitRecThreadCreationMutex);
	}

}

void getInformation() {
	infoFile.open("server.in");
	char read[50];

	infoFile.read(read, 50);
	char ports[10];
	int i = 0;
	for (; i < 50; i++) {
		if (read[i] != '\n') {
			ports[i] = read[i];

		} else {
			break;
		}
	}
	port = atoi(ports);
	char maxWins[10];
	int j = 0;
	i++;
	for (; i < 50; i++) {
		if (read[i] != '\n') {
			maxWins[j] = read[i];
			j++;
		} else {
			break;
		}
	}
	MaxBufferSize = atoi(maxWins);
	char wins[10];
	j = 0;
	i++;
	for (; i < 50; i++) {
		if (read[i] != '\n') {
			wins[j] = read[i];
			j++;
		} else {
			break;
		}
	}
	MaxWindow = atoi(wins);
	char probs[10];
	j = 0;
	i++;
	for (; i < 50; i++) {
		if ((read[i] >= '0' && read[i] <= '9') || read[i] == '.') {
			probs[j] = read[i];
			j++;
		} else {
			break;
		}
	}
	plp = atof(probs);

}

int main(int argc, char** argv) {

	logFile.open("log.txt");
	getInformation();
	serve(port);
}

void convertAckPacketToByte(ack_packet *packet, char* buffer) {

	//chksum

	buffer[0] = packet->cksum >> 8;
	buffer[1] = (packet->cksum) & 255;

	//len field

	buffer[2] = packet->len >> 8;
	buffer[3] = (packet->len) & 255;

	//seqnumber

	buffer[4] = packet->ackno >> 24;
	buffer[5] = (packet->ackno >> 16) & 255;
	buffer[6] = (packet->ackno >> 8) & 255;
	buffer[7] = (packet->ackno) & 255;
}

void sendNewPortServer() {
	char incremData[8];
	ack_packet packet;
	packet.cksum = 1;
	packet.len = portIncreamenter;
	packet.ackno = 0;
	convertAckPacketToByte(&packet, incremData);
	cout << "Sending the New Port Ack to client" << endl;
	logFile << "Sending the New Port Ack to client" << endl;
	int sendFlag = sendto(childSocket, incremData, 8, 0,
			(struct sockaddr *) &client_addr, sizeof(client_addr));
	if (sendFlag == -1)
		perror("receive New Port Server ACK Error");
	cout << "New Port Ack to client sent : " << sendFlag << endl;

}
void receivevNewPortServerACK() {
	char incremData[8];
	socklen_t alen; /* length of address structure */
	alen = sizeof(client_addr);

	cout << "Wait the New Port Ack from client" << endl;
	int sendFlag = recvfrom(childSocket, incremData, 8, 0,
			(struct sockaddr *) &client_addr, &alen);
	if (sendFlag == -1)
		perror("receive New Port Server ACK Error");

	cout << "New Port Ack from client : " << sendFlag << endl;
}

void serve(int port) {
	signal(SIGSEGV, sig_func); // Register signal handler before going multithread
//	sleep(2); // Leave time for initialisation

	socklen_t alen; /* length of address structure */
	int sockoptval = 1;

	char hostname[128] = "localhost"; /* host name, for debugging */

	/* get a tcp/ip socket */
	/*   AF_INET is the Internet address (protocol) family  */
	/*   with SOCK_STREAM we ask for a sequenced, reliable, two-way */
	/*   conenction based on byte streams.  With IP, this means that */
	/*   TCP will be used */

	if ((svc = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("cannot create socket");
		exit(1);
	}

	/* set up our address */
	/* htons converts a short integer into the network representation */
	/* htonl converts a long integer into the network representation */
	/* INADDR_ANY is the special IP address 0.0.0.0 which binds the */
	/* transport endpoint to all IP addresses on the machine. */

	memset((char*) &my_addr, 0, sizeof(my_addr)); /* 0 out the structure */
	my_addr.sin_family = AF_INET; /* address family */
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY );

	/* bind to the address to which the service will be offered */
	if (bind(svc, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0) {
		perror("bind failed");
		exit(1);
	}

	/* loop forever - wait for connection requests and perform the service */
	for (;;) {

		alen = sizeof(client_addr); /* length of address */

		///////////////////////////////////////////////////////////////////////
		/* Block until receive message from a client */
		cout << "Server is now Ready to RECEIVE " << endl;
		char echoBuffer[100];
		if ((recvfrom(svc, echoBuffer, 100, 0, (struct sockaddr *) &client_addr,
				&alen)) < 0)
			perror("recvfrom() failed");
		///////////////////////////////////////////////////////////////////////
		echoBuffer[21] = '\0';
		cout << "received buffer : " << echoBuffer << endl;

		logFile << "A request is received for file : " << echoBuffer << endl;
		//fork a new process here
		logFile << "FORKING NEW PROCESS TO HANDLE THE SENDING" << endl;
		int pid = fork();
		//child process will enter
		if (pid == 0) {
			logFile << "child process created " << endl;
			//create an ack receiver thread
			pthread_t ackThread;
			int rc;
			long t = 0;

			///////////// send the new server port to the client

			logFile << "creating new socket to handel the connection" << endl;
			if ((childSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
				perror("cannot create socket");
				exit(1);
			}

			memset((char*) &child_addr, 0, sizeof(child_addr)); /* 0 out the structure */
			child_addr.sin_family = AF_INET; /* address family */
			child_addr.sin_addr.s_addr = htonl(INADDR_ANY );
			child_addr.sin_port = htons(port + portIncreamenter);

			if (bind(childSocket, (struct sockaddr *) &child_addr,
					sizeof(child_addr)) < 0) {

				perror("bind failed  ");
				exit(1);
			}
			cout << "Child Socket Created" << endl;
			logFile << "Child Socket Created" << endl;
			sendNewPortServer();
			receivevNewPortServerACK();

			///////////////////////////////////////////////////

			if (!stopAndWait) {
				logFile << "INTIALIZING SELECTIVE REPEAT PROTOCOL" << endl;

				rc = pthread_create(&ackThread, NULL, receiveAck, (void *) t);

				int rc1 = pthread_create(&SelectiveThread, NULL,
						SelectiveTimerCallBack, (void *) t);

				if (rc || rc1) {
					cout
							<< "error in starting the ack thread and timer Selective"
							<< endl;
				} else {
					pthread_detach(ackThread);
					gettimeofday(&time1, NULL);
					long t1 = time1.tv_sec;

					chunkizeAndSend(echoBuffer);
					gettimeofday(&time2, NULL);
					long t2 = time2.tv_sec;
					t2 = t2 - t1;
					logFile << "Time Taken is " << t2 << " secs" << endl;
					logFile.close();
					exit(0);
					shutdown(rqst, 2); /* 3,590,313 close the connection */
				}
			} else {
				logFile << "INTIALIZING STOP AND WAIT PROTOCOL" << endl;
				gettimeofday(&time1, NULL);
				long t1 = time1.tv_sec;
				chunkizeAndSendStopAndWait(echoBuffer);

				gettimeofday(&time2, NULL);
				long t2 = time2.tv_sec;
				t2 = t2 - t1;
				logFile << "Time Taken is " << t2 << " secs" << endl;
				logFile.close();
				exit(0);

			}
		} else if (pid > 0) {
			portIncreamenter++;

			//parent will enter here

		} else if (pid == -1) {
			cout << "error forking the client process" << endl;
		}
	}
}

int prepareFile(char* fileName) {
	fp = fopen(fileName, "r");
	if (fp == NULL) {
		perror("Server- File NOT FOUND 404");
		return -1;
	}
	fseek(fp, 0L, SEEK_END);
	long size = ftell(fp);

	fseek(fp, 0L, SEEK_SET);

	return size;

}

void *StopAndWaittimerCallBack(void *time) {

	int sleeptime = (int) time;
	sleep(1);
	pthread_mutex_lock(&StopAndWaitmutexTimer);
	timeCount++;
	//cout << "TIMEEEE OUTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT" << endl;
	pthread_kill(recThread, 0);
	stopAndWaitAcked = false;
	pthread_mutex_unlock(&StopAndWaitmutexTimer);
	pthread_mutex_unlock(&StopAndWaitmutex);
	pthread_exit(NULL);

}
int outOfOrder = 0;
void *receiveAckStopAndWait(void *threadid) {

	//stopAndWaitAcked = false;
	int packetCount = (int) threadid;
	socklen_t alen = sizeof(client_addr); /* length of address */
	int reclen;
	char Buffer[8];
	cout << "waiting for ack #" << packetCount << endl;
	logFile << "waiting for ack #" << packetCount << endl;
	bool waitEnd = false;

	while (!waitEnd) {

		stopAndWaitAcked = false;
		if (reclen = (recvfrom(childSocket, Buffer, 8, 0,
				(struct sockaddr *) &client_addr, &alen)) < 0)
			perror("recvfrom() failed");

		pthread_mutex_lock(&StopAndWaitmutexTimer);
		//parse to ack_packet
		ack_packet packet;
		parseAckPacket(Buffer, 8, &packet);
		cout << "ack #" << packet.ackno << " received" << endl;
		logFile << "ack #" << packet.ackno << " received" << endl;

		//dublicate ack ignore

		uint16_t temp;
		if (packetCount == 0)
			temp = 0;
		else
			temp = 1;
		if (packet.ackno != temp) {
			cout << "Received is " << packet.ackno
					<< "...out of order ack...wait for right Packet # "
					<< packetCount << endl;

			logFile << "Received is " << packet.ackno
					<< "...out of order ack...wait for right Packet # "
					<< packetCount << endl;
			outOfOrder++;
			pthread_mutex_unlock(&StopAndWaitmutexTimer);
			//pthread_mutex_unlock(&StopAndWaitmutexTimer);
		} else {

			cout << "killing Timer" << endl;
			//right ack received

			//pthread_kill(stopAndWaitTimerThread, 0);
			waitEnd = true;
			stopAndWaitAcked = true;
			//kill the timerthread

		}
	}
	pthread_mutex_unlock(&StopAndWaitmutexTimer);

	//unlock the sender
	pthread_mutex_unlock(&StopAndWaitmutex);
	pthread_exit(NULL);
}

void extractPacket(packet* packet, char buffer[], int buffLength) {

	//convert char to unsigned char
	uchar b0 = buffer[0];
	uchar b1 = buffer[1];
	uchar b2 = buffer[2];
	uchar b3 = buffer[3];
	uchar b4 = buffer[4];
	uchar b5 = buffer[5];
	uchar b6 = buffer[6];
	uchar b7 = buffer[7];
	//checksum combine first two bytes
	packet->cksum = (b0 << 8) | b1;
	//len combine second two bytes
	packet->len = (b2 << 8) | b3;
	//seq_no combine third four bytes
	packet->seqno = (b4 << 24) | (b5 << 16) | (b6 << 8) | (b7);
	for (int i = 8; i < buffLength; ++i) {
		packet->data[i - 8] = buffer[i];
	}

}

void resend(int i) {
	int sendFlag = 0;

	sendFlag = sendto(childSocket, frames->at(i), lens->at(i) + 8, 0,
			(struct sockaddr *) &client_addr, sizeof(client_addr));

	packet p;
	extractPacket(&p, frames->at(i), lens->at(i));
	cout << "Resending Timer , RESEND SEQ NUM = " << p.seqno << endl;
	logFile << "Resending PACKET WITH SEQ NUM = " << p.seqno << endl;

}

void *SelectiveTimerCallBack(void *time) {

	while (true) {
		pthread_mutex_lock(&Selectivemutex);
		//sleep(0.2);

		for (int i = 0; i < times->size(); ++i) {
			times->at(i)--;

			// resend
			if
(			times->at(i)==0) {
				cout<<"I = "<<i<<endl;

				for(int j=0;j<frames->size();++j) {

					packet p;
					extractPacket(&p,frames->at(j),lens->at(j));
					cout << "SEQ NUM = " <<p.seqno<< endl;

				}
				cout<<"TIME OUT PACKET: "<<base+i<<endl;
				logFile<<endl;
				logFile<<"++++++++++++TIME OUT DETECTED++++++++++++++"<<endl;
				logFile<<endl;
				logFile<<"DECREASING WINDOW SIZE FROM "<<MaxWindow<<" TO "<<MaxWindow/2<<endl;
				times->at(i)= TIMEOUT;
				resend(i);
			}
		}

		pthread_mutex_unlock(&Selectivemutex);
	}

}

void *receiveAck(void *threadid) {

	while (1) {

		//cout << "-------------------	ACK Worker thread is opened" << endl;
		socklen_t alen = sizeof(client_addr); /* length of address */
		int reclen;
		char Buffer[8];
		//cout << "-------------------	thread waiting for acks" << endl;
		if (reclen = (recvfrom(childSocket, Buffer, 8, 0,
				(struct sockaddr *) &client_addr, &alen)) < 0)
			perror("recvfrom() failed");

//		for (int j = 0; j < frames->size(); j++) {
//
//			packet p;
//			extractPacket(&p, frames->at(j), lens->at(j));
//			cout << "SEQ NUM = " << p.seqno << endl;
//
//		}

		//parse to ack_packet   3,760,412
		ack_packet packet;
		parseAckPacket(Buffer, 8, &packet);
//		cout << "-------------------	Ack num : " << packet.ackno << " received"
		//			<< endl;
		int frameIndex = packet.ackno - base;
		//detect dublicates
		//cout << "---------------------------------		frame Index : "
		//	<< frameIndex << endl;
		pthread_mutex_lock(&Selectivemutex);
		cout << "ack " << packet.ackno << " received" << endl;
		logFile << "ACK " << packet.ackno << " received" << endl;
		cout << "FrameIndex : " << frameIndex << endl;

		if (frameIndex < 0 || ackedFrames->size() == 0) {
			cout << "dublicate ack is detected ... ignore" << endl;
			logFile << "dublicate ACK is detected ... ignore" << endl;

		} else if (ackedFrames->size() != 0
				&& ackedFrames->at(frameIndex) == true) {
			cout << "dublicate ack is detected ... ignore" << endl;
			logFile << "dublicate ACK is detected ... ignore" << endl;
			//dublicates
		} else {
			cout << "NO Dublication" << endl;
			if (MaxWindow < MaxBufferSize) {
				MaxWindow++;
				logFile << "INCREASE WIDOW SIZE FROM  " << MaxWindow - 1
						<< " TO " << MaxWindow << endl;
			}
			cout << "Buffers size = " << ackedFrames->size() << endl;
			ackedFrames->at(frameIndex) = true;
			//TODO stop timer
			times->at(frameIndex) = TIMERSTOP;

			if (ackedFrames->at(0) == true) {
				//slide window as much as possible
				cout
						<< "-------------- the base packet is acked..sliding the window"
						<< endl;

				logFile << "the base packet is acked...sliding the window"
						<< endl;
				for (int i = 0; i <= curr - base; ++i) {
					if (ackedFrames->size() == 0)
						break;
					if (ackedFrames->at(0) == true) {
						//TODO stoptimer
						frames->erase(frames->begin());
						ackedFrames->erase(ackedFrames->begin());
						times->erase(times->begin());
						lens->erase(lens->begin());
						++base;
						cout
								<< "Size ============================================="
								<< frames->size() << endl;
					} else {
						break;
					}
				}
				cout << "------------  new base = " << base << endl;
				logFile << "new base pointer = " << base << endl;
			}
		}
		pthread_mutex_unlock(&Selectivemutex);
	}
}

void chunkizeAndSend(char* fileName) {

	int size = prepareFile(fileName);
	packet currPacket;

	cout << "File Size : " << size << endl;
	logFile << "File Size : " << size << endl;
	int packetCount = 0;

	for (int i = 0; i < size;) {

		dropCount++;
		dropCount = dropCount % 101;

		currPacket.cksum = 1;
		currPacket.len = fread(currPacket.data, 1, 100, fp);
		currPacket.seqno = packetCount;
		//	cout << "******		read Size data = " << currPacket.len << endl;
		i += currPacket.len;
		//cout << "I = " << i << endl;
		/////////////////// send currPacket
		char *buffer = new char[108];
		//cout << "Sending packet " << i << " to " << client_addr.sin_addr
		//	<< " port " << SERVICE_PORT << "\n" << endl;
		unsigned int sendFlag = 1;
		convertDataPacketToByte(&currPacket, buffer);

		while (1) {
			pthread_mutex_lock(&Selectivemutex);
			if (curr - base < MaxWindow - 1) {
				if (!isdropped()) {
					cout << "SENDING... current = " << curr << " packet = "
							<< packetCount << " And Window size = " << MaxWindow
							<< endl;
					sendFlag = sendto(childSocket, buffer, currPacket.len + 8,
							0, (struct sockaddr *) &client_addr,
							sizeof(client_addr));
					if (sendFlag < -1)
						break;
					else if (sendFlag == -1)
						perror("sendto");
				} else {
					if (MaxWindow > 2) {
						MaxWindow = MaxWindow / 2;
						cout << "KARSAAAAA ======== :D :D el window size b "
								<< MaxWindow << endl;

					} else if (MaxWindow < 2) {
						MaxWindow = 2;
						cout << "KARSAAAAA ======== :D :D el window size b "
								<< MaxWindow << endl;

					}
					cout << "PACKET :" << packetCount
							<< " IS DROPPED DONT BELEIVE OTHERS" << endl;

					logFile << endl;
					logFile << "PACKET :" << packetCount
							<< " IS DROPPED DONT BELEIVE OTHERS" << endl;
					logFile << endl;
					break;
				}
			} else {
				pthread_mutex_unlock(&Selectivemutex);
			}
		}
//		cout << "packet number:  " << packetCount << "--- seqnum :"
//				<< currPacket.seqno << " is sent" << endl;

		logFile << "packet number:  " << currPacket.seqno << " is sent" << endl;

		curr = currPacket.seqno;
		frames->push_back(buffer);
		ackedFrames->push_back(false);
		times->push_back(TIMEOUT);
		lens->push_back(currPacket.len);

//		for (int j = 0; j < frames->size(); ++j) {
//
//			packet p;
//			extractPacket(&p, frames->at(j), lens->at(j));
//			cout << "SEQ NUM after send = " << p.seqno << endl;
//
//		}

		pthread_mutex_unlock(&Selectivemutex);
		//// TO DO START TIMER

//		cout << "******		" << packetCount << " Packet Sent" << endl;
		packetCount++;

		////////////////////

	}
////////////////////////////////// sending finish file packet

	currPacket.cksum = 1;
	currPacket.len = 150;
	currPacket.seqno = packetCount;
	cout << "******		Sending finish packet." << endl;
	logFile << "******		Sending finish packet." << endl;

	gettimeofday(&time2, NULL);
	long t2 = time2.tv_sec;
	t2 = t2 - time1.tv_sec;
	logFile << "Time Taken is " << t2 << " secs" << endl;
	logFile.close();

/////////////////// send currPacket

	char *dataFinish = new char[8];
//cout << "Sending packet " << i << " to " << client_addr.sin_addr
//	<< " port " << SERVICE_PORT << "\n" << endl;
	int sendFlag = 1;
	convertDataPacketToByte(&currPacket, dataFinish);

	while (1) {
		if (curr - base < MaxWindow - 1) {
			sendFlag = sendto(childSocket, dataFinish, sizeof(dataFinish), 0,
					(struct sockaddr *) &client_addr, sizeof(client_addr));
			break;
		}
	}
	pthread_mutex_lock(&Selectivemutex);

	curr = currPacket.seqno;
	frames->push_back(dataFinish);
	ackedFrames->push_back(false);
	lens->push_back(currPacket.len);
	times->push_back(TIMEOUT);
	pthread_mutex_lock(&Selectivemutex);

	if (sendFlag == -1)
		perror("sendto");

////////////////////

////////////////////////////////////////////////// finish
	cout << "******		Sending finish packet DONE." << endl;
	cout << "******		Finish Window size =" << MaxWindow << endl;
	logFile << "******		Finish Window size =" << MaxWindow << endl;



}

void sig_func(int sig) {

//terminate the thread
//cout << "Exiting" << endl;
//	if (pthread_self() == recThread) {
//		cout << "rec thread is killlled" << endl;
//	} else {
//		cout << "karsaaaaaaaaaa" << endl;
//	}
	cout << "rec thread is killlled" << endl;
	pthread_exit(NULL);
}
void chunkizeAndSendStopAndWait(char* fileName) {
	bool recThreadIsCreated = false;
	bool timerStarted = false;
	int size = prepareFile(fileName);
	packet currPacket;

	cout << "File Size : " << size << endl;
	logFile << "File Size : " << size << endl;
	int packetCount = 0;

	int couny = 0;
	dropCount = 0;
	for (int i = 0; i < size;) {
		dropCount++;
		dropCount = dropCount % 101;
		currPacket.cksum = 1;
		currPacket.len = fread(currPacket.data, 1, 100, fp);
		currPacket.seqno = packetCount;
		//	cout << "******		read Size data = " << currPacket.len << endl;
		i += currPacket.len;
		//cout << "I = " << i << endl;
		/////////////////// send currPacket
		char buffer[108];
		//cout << "Sending packet " << i << " to " << client_addr.sin_addr
		//	<< " port " << SERVICE_PORT << "\n" << endl;
		unsigned int sendFlag = 1;
		convertDataPacketToByte(&currPacket, buffer);
		bool acked = false;

		stopAndWaitAcked = false;
		while (!acked) {
			//cout << "Packet Not Acked yet" << endl;
			//send packet
			if (!isdropped()) {
				while (1) {
					sendFlag = sendto(childSocket, buffer, currPacket.len + 8,
							0, (struct sockaddr *) &client_addr,
							sizeof(client_addr));
					if (sendFlag == 0) {
						cout << "send flag = zeroooooooooooo" << endl << endl;
						break;/////============================================================================ ///////////////////////////////
					} else if (sendFlag < -1)
						break;
					else if (sendFlag == -1)
						perror("sendto");
				}
				cout << "******		" << packetCount << " Packet Sent" << endl;
				logFile << "PACKET: " << packetCount << " Sent" << endl;
			} else {
				cout << "Package Dropped" << endl;
				logFile << "----------------Package Dropped---------------"
						<< endl;
			}
			////////////////////////////////////////////
			//TODO start timer

			pthread_mutex_lock(&StopAndWaitmutex);

			pthread_mutex_lock(&StopAndWaitRecThreadCreationMutex);
			if (recThreadIsCreated == true) {
				pthread_kill(recThread, 0);
				recThreadIsCreated = false;
			}
			while (1) {
				int rc = pthread_create(&recThread, NULL, receiveAckStopAndWait,
						(void *) packetCount);
				recThreadIsCreated = true;
				pthread_detach(recThread);
				if (rc) {
					cout << "error in starting the Stop and wait ack thread"
							<< endl;
					sleep(1);
				} else {
					break;
				}
			}
			pthread_mutex_unlock(&StopAndWaitRecThreadCreationMutex);
			//wait for receiver

			if (!timerStarted) {
				timerStarted = true;
				int timeout = 3;
				while (1) {	// until succeed in creation of pthread
					int rc2 = pthread_create(&stopAndWaitTimerThread, NULL,
							StopAndWaittimerCallBack2, (void *) timeout);
					if (rc2) {
//					cout << "error in starting the Stop and wait timer thread"
//							<< endl;
						//	sleep(1);
					} else {
						pthread_detach(stopAndWaitTimerThread);
						break;

					}
				}
			}
			++couny;
			cout << "---------------- " << couny << endl;
			//open thread
			//force order

			pthread_mutex_lock(&StopAndWaitmutex);
			cout << "-----------------SENDER UNLOCKED1--------------" << endl;
			pthread_mutex_lock(&StopAndWaitmutexTimer);

			cout << "-----------------SENDER UNLOCKED2--------------" << endl;

			if (stopAndWaitAcked == true) {
				//ack is received
				stopAndWaitAcked = false;
				acked = true;
			} else {
				//ack is lost resend packet
				acked = false;
			}
			pthread_mutex_unlock(&StopAndWaitmutex);
			pthread_mutex_unlock(&StopAndWaitmutexTimer);

			//////////////////////////////////////////
		}

		packetCount++;
		packetCount = packetCount % 2;

	}
////////////////////////////////// sending finish file packet

	currPacket.cksum = 1;
	currPacket.len = 150;
	currPacket.seqno = packetCount;
	cout << "******		Sending finish packet." << endl;

/////////////////// send currPacket

	char dataFinish[8];
//cout << "Sending packet " << i << " to " << client_addr.sin_addr
//	<< " port " << SERVICE_PORT << "\n" << endl;
	int sendFlag = 1;
	convertDataPacketToByte(&currPacket, dataFinish);

	sendFlag = sendto(childSocket, dataFinish, sizeof(dataFinish), 0,
			(struct sockaddr *) &client_addr, sizeof(client_addr));

	if (sendFlag == -1)
		perror("sendto");

////////////////////
	pthread_kill(stopAndWaitTimerThread, 0);
	//exit(0);
////////////////////////////////////////////////// finish
	cout << "******		Sending finish packet DONE." << endl;
	cout << "Out Of order Count = " << outOfOrder << endl;
}

void convertDataPacketToByte(packet *packet, char* buffer) {
//chksum

	buffer[0] = packet->cksum >> 8;
	buffer[1] = (packet->cksum) & 255;

//len field

	buffer[2] = packet->len >> 8;
	buffer[3] = (packet->len) & 255;

//seqnumber

	buffer[4] = packet->seqno >> 24;
	buffer[5] = (packet->seqno >> 16) & 255;
	buffer[6] = (packet->seqno >> 8) & 255;
	buffer[7] = (packet->seqno) & 255;

//data

	for (int i = 8; i < packet->len + 8; ++i) {
		buffer[i] = packet->data[i - 8];
	}

}
void parseAckPacket(char *buffer, int buffLength, ack_packet* packet) {
//convert char to unsigned char
	uchar b0 = buffer[0];
	uchar b1 = buffer[1];
	uchar b2 = buffer[2];
	uchar b3 = buffer[3];
	uchar b4 = buffer[4];
	uchar b5 = buffer[5];
	uchar b6 = buffer[6];
	uchar b7 = buffer[7];
//checksum combine first two bytes
	packet->cksum = (b0 << 8) | b1;
//len combine second two bytes
	packet->len = (b2 << 8) | b3;
//seq_no combine third four bytes
	packet->ackno = (b4 << 24) | (b5 << 16) | (b6 << 8) | (b7);
}

