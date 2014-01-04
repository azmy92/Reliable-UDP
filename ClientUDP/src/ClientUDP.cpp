/*
 udp-client: a demo of UDP/IP socket communication

 usage:	client [-h serverhost] [-p port]
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>	/* needed for os x*/
#include <string.h>	/* for strlen */
#include <netdb.h>      /* for gethostbyname() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <fstream>
#include "port.h"       /* defines default port */
#include <vector>
#define FILE_FIN 150

typedef unsigned char uchar;

using namespace std;
int port = SERVICE_PORT; /* default: whatever is in port.h */
ifstream infoFile;
ofstream fp;
ofstream fout;
char* fileName;
bool stopAndWait = false;
/* Data-only packets */
struct packet {
	/* Header */
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t seqno;
	/* Data */
	char data[500]; /* Not always 500 bytes, can be less */
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
	uint16_t cksum; /* Optional bonus part */
	uint16_t len;
	uint32_t ackno;

};

vector<packet*> buffer;
int bufferCount = 0;
void recieveStopAndWait();
void recieve();
int conn(char *host, int port);
void disconn(void);
int getPacketLength(char[]);
void extractPacket(packet*, char[], int);
void sendAck(packet*);
void makeAckChunk(char[], ack_packet*);
int AppendPacketDataToFile(ofstream*, packet*);
void openFile(char*, ofstream*);

void getInformation() {
	infoFile.open("client.in");
	char read[50];

	infoFile.read(read, 50);
	char ports[10];
	int j = 0;
	int i = 0;
	for (; i < 50; i++) {
		if (read[i] != '\n') {
			ports[i] = read[i];

		} else {
			break;
		}
	}
	port = atoi(ports);
	char filenameS[100];
	j = 0;
	i++;
	for (; i < 50; i++) {
		if (read[i] != '\n') {
			filenameS[j] = read[i];
			j++;
		} else {
			break;
		}
	}

	filenameS[j] = '\0';
	fileName = new char[j + 1];
	for (int i = 0; i < j; ++i) {

		fileName[i] = filenameS[i];
	}
	fileName[j] = '\0';

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

int main(int argc, char **argv) {

	char *host = "localhost"; /* default: this host */
	getInformation();
	fout.open("log.txt");
	fout << "CONNECTING TO " << host << " " << port << endl;
	printf("connecting to %s, port %d\n", host, port);

	if (!conn(host, port)) /* connect */
		exit(1); /* something went wrong */

	disconn(); /* disconnect */

	return 0;
}

int fd; /* fd is the file descriptor for the connected socket */

/* conn: connect to the service running on host:port */
/* return 0 on failure, non-zero on success */
struct sockaddr_in myaddr; /* our address */
struct sockaddr_in servaddr; /* server address */
socklen_t addrlen;

void sendNewPortServer() {
	char incremData[8];
	cout << "Sending ack to server on new port : " << servaddr.sin_addr.s_addr
			<< endl;
	int sendFlag = sendto(fd, incremData, 8, 0, (struct sockaddr *) &servaddr,
			sizeof(servaddr));
	if (sendFlag == -1)
		perror("receive New Port Server ACK Error");

}
void receiveNewPortServerACK() {
	char incremData[8];
	int sendFlag = recvfrom(fd, incremData, 8, 0, (struct sockaddr *) &servaddr,
			&addrlen);
	if (sendFlag == -1)
		perror("receive New Port Server ACK Error");
	ack_packet ackNewServNumber;
	parseAckPacket(incremData, 8, &ackNewServNumber);

	servaddr.sin_port = htons(ackNewServNumber.len + SERVICE_PORT);
	cout << "new port is received : " << ackNewServNumber.len + SERVICE_PORT
			<< endl;

}

int conn(char *host, int port) {
	struct hostent *hp; /* host information */
	unsigned int alen; /* address length when we get the port number */

	printf("conn(host=\"%s\", port=\"%d\")\n", host, port);

	/* get a tcp/ip socket */
	/* We do this as we did it for the server */
	/* request the Internet address protocol */
	/* and a reliable 2-way byte stream */
	addrlen = sizeof(servaddr);

	if ((fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		perror("cannot create socket");
		return 0;
	}

	int buffsize = 65536555; // this number is 65536
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*) &buffsize, sizeof(buffsize));

	/* bind to an arbitrary return address */
	/* because this is the client side, we don't care about the */
	/* address since no application will connect here  --- */
	/* INADDR_ANY is the IP address and 0 is the socket */
	/* htonl converts a long integer (e.g. address) to a network */
	/* representation (agreed-upon byte ordering */

	memset((char *) &myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = htonl(INADDR_ANY );
	myaddr.sin_port = htons(0);

//	if (bind(fd, (struct sockaddr *) &myaddr, sizeof(myaddr)) < 0) {
//		perror("bind failed");
//		return 0;
//	}

	/* this part is for debugging only - get the port # that the operating */
	/* system allocated for us. */
	alen = sizeof(myaddr);
	if (getsockname(fd, (struct sockaddr *) &myaddr, &alen) < 0) {
		perror("getsockname failed");
		return 0;
	}
	printf("local port number = %d\n", ntohs(myaddr.sin_port));

	/* fill in the server's address and data */
	/* htons() converts a short integer to a network representation */

	memset((char*) &servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);

	/* look up the address of the server given its name */
	hp = gethostbyname(host);
	if (!hp) {
		fprintf(stderr, "could not obtain address of %s\n", host);
		return 0;
	}

	/* put the host's address into the server address structure */
	memcpy((void *) &servaddr.sin_addr, hp->h_addr_list[0], hp->h_length);

	//////////////////////////////////// first Connection Request
	char *dataSend;
	dataSend = fileName;
	int sendFlag = sendto(fd, dataSend, strlen(dataSend), 0,
			(struct sockaddr *) &servaddr, sizeof(servaddr));
	/////////////////////////////////////////////////////////////

	/////////////////////////////////////////////////////////////
	receiveNewPortServerACK();
	sendNewPortServer();
	cout << "Done new Port Send and receive" << endl;

	////////////////////////////////////////////////////////////

	openFile(fileName, &fp);
	if (!stopAndWait) {

		fout << "SELECTIVE REPEAT PROTOCOL IS USED" << endl;
		recieve();
	} else {
		fout << "STOP AND WAIT PROTOCOL IS USED" << endl;
		recieveStopAndWait();
	}
	fp.close();
	fout.close();
	return 1;
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

void sendAck(int seqno) {
	char ackData[8];
	ack_packet Ack;
	Ack.ackno = seqno;
	Ack.cksum = 1;
	Ack.len = 8;
	convertAckPacketToByte(&Ack, ackData);
	//cout<<"Sending ack to server on new port : "<< servaddr.sin_addr.s_addr<<endl;
	int sendFlag = sendto(fd, ackData, 8, 0, (struct sockaddr *) &servaddr,
			sizeof(servaddr));
	if (sendFlag == -1)
		perror("receive New Port Server ACK Error");
}
void recieveStopAndWait() {
	int dublicateCount = 0;
	/* now loop, receiving data and printing what we received */
	bool isfirstRecieve = true;
	char data[2 + 2 + 4 + 100];
	int recvLen = 1;
	int currentlyReceivedBytes = 0;
	int packetLength = 0;
	int packetCount = 0;

	printf("waiting on port %d\n", SERVICE_PORT);

	cout << "Receiving file packets now" << endl; ////////// Start comming packets
	while (1) {

		currentlyReceivedBytes = 0;
		isfirstRecieve = true;
		cout << "Receiving packet No: " << packetCount << endl; ////////// Start comming packets
		fout << "EXPECTED packet No: " << packetCount << endl;
		while (1) {
			recvLen = recvfrom(fd, data, 108, 0, (struct sockaddr *) &servaddr,
					&addrlen);
			if (isfirstRecieve) {
				packetLength = getPacketLength(data) + 8;
				isfirstRecieve = false;
			}
			if (packetLength == FILE_FIN + 8) {
				printf("File received Successfully: %d bytes\n", recvLen);
				fout << "File received Successfully" << endl;
				break;
			}

			currentlyReceivedBytes += recvLen;
			if (currentlyReceivedBytes == packetLength || recvLen == 0) {
				break;
			}
		}

		packet recvdPacket;

		extractPacket(&recvdPacket, data, recvLen);
		cout << "Packet No: " << recvdPacket.seqno << " Received" << endl;
		fout << "Packet No: " << recvdPacket.seqno << " Received" << endl;
		//send acks here
		if (recvdPacket.seqno == packetCount) {
			sendAck(recvdPacket.seqno);
			AppendPacketDataToFile(&fp, &recvdPacket);
			cout << "pkt : " << recvdPacket.seqno << " Acked" << endl;
			fout << "pkt : " << recvdPacket.seqno << " Acked" << endl;
			packetCount++;
			packetCount = packetCount % 2;
		} else {
			dublicateCount++;
			sendAck(recvdPacket.seqno);
			cout << "DUBLICATION DETECTED -------- pkt : " << packetCount
					<< " Acked" << endl;
			fout << "DUBLICATION DETECTED -------- pkt : " << packetCount
					<< " Acked" << endl;
		}

		if (packetLength == FILE_FIN + 8) {
			printf("File received Successfully: %d bytes\n", recvLen);
			fout << "File received Successfully" << endl;
			break;
		}

		printf("received %d bytes\n", recvLen);

	}
	cout << "dublicateCount = " << dublicateCount << endl;
}
void recieve() {
	/* now loop, receiving data and printing what we received */
	bool isfirstRecieve = true;
	char data[2 + 2 + 4 + 100];
	int recvLen = 1;
	int currentlyReceivedBytes = 0;
	int packetLength = 0;
	int packetCount = 0;

	printf("waiting on port %d\n", SERVICE_PORT);

	cout << "Receiving file packets now" << endl; ////////// Start comming packets
	fout << "Receiving file packets now" << endl; ////////// Start comming packets
	while (1) {
		currentlyReceivedBytes = 0;
		isfirstRecieve = true;
		cout << "expected packet No: " << packetCount << endl; ////////// Start comming packets
		fout << "expected packet No: " << packetCount << endl;
		while (1) {
			recvLen = recvfrom(fd, data, 108, 0, (struct sockaddr *) &servaddr,
					&addrlen);
			if (isfirstRecieve) {
				packetLength = getPacketLength(data) + 8;
				isfirstRecieve = false;
			}
			if (packetLength == FILE_FIN + 8) {
				printf("File received Successfully: %d bytes\n", recvLen);
				fout << "File received Successfully size = " << recvLen << endl;
				break;
			}

			currentlyReceivedBytes += recvLen;
			if (currentlyReceivedBytes == packetLength || recvLen == 0) {
				break;
			}
		}
		//cout << "Packet No: " << packetCount << " Received" << endl;

		//int expected = packetCount;
		//packetCount++;

		packet *recvdPacket = new packet;

		extractPacket(recvdPacket, data, recvLen);
		//send acks here
		cout << "Sequence Number Recvd : " << recvdPacket->seqno << endl;
		fout << "Sequence Number Recvd : " << recvdPacket->seqno << endl;
		sendAck(recvdPacket->seqno);
		if (recvdPacket->seqno < packetCount) {
			//duplicate..drop packet
			cout << "................duplicate Packet ... DROP................"
					<< endl;
			fout << "................duplicate Packet ... DROP................"
					<< endl;
		} else if (recvdPacket->seqno > packetCount) {
			//if not expected received buffer
			buffer.push_back(recvdPacket);
			cout << "................BUFFERED................" << endl;
			fout << "................BUFFERED................" << endl;
			++bufferCount;
		} else {

			//append packet to file and check buffer
			AppendPacketDataToFile(&fp, recvdPacket);
			++packetCount; // update expected seqnod
			int tempBufferCount = bufferCount;

			for (int j = 0; j < buffer.size(); ++j) {

				packet *p = buffer.at(j);
				//extractPacket(&p,frames->at(j),lens->at(j));
				cout << ".................SEQ NUM = " << p->seqno << endl;

			}

			for (int i = 0; i < tempBufferCount; ++i) {
				packet* currPacket = buffer.at(0);
				if (currPacket->seqno == packetCount) {
					AppendPacketDataToFile(&fp, currPacket);
					++packetCount; // update expected seqno
					--bufferCount;
					buffer.erase(buffer.begin());
				} else {
					//gap detected
					break;
				}
			}

		}
		cout << "pkt : " << recvdPacket->seqno << " Acked" << endl;
		fout << "pkt : " << recvdPacket->seqno << " Acked" << endl;

		if (packetLength == FILE_FIN + 8) {
			printf("File received Successfully: %d bytes\n", recvLen);
			cout << "File received Successfully size =  " << recvLen << " bytes"
					<< endl;
			break;
		}

		printf("received %d bytes\n", recvLen);

	}
}

/* disconnect from the service */
void disconn(void) {
	printf("disconn()\n");
	shutdown(fd, 2); /* 2 means future sends & receives are disallowed */
}

int getPacketLength(char data[]) {

	uchar b2 = data[2];
	uchar b3 = data[3];
	//length combine second two bytes
	int len = (b2 << 8) | b3;
	return len;

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
void sendAck(packet* pkt) {

	ack_packet ackPacket;

	ackPacket.ackno = pkt->seqno;
	ackPacket.cksum = 1;
	ackPacket.len = 8;

	char data[8];
	makeAckChunk(data, &ackPacket);

	///// sending the ackpkt

	int sendFlag = 1;
	while (1) {
		sendFlag = sendto(fd, data, strlen(data), 0,
				(struct sockaddr *) &servaddr, sizeof(servaddr));
		if (sendFlag == 0)
			break;
		else if (sendFlag == -1)
			perror("sendto");
	}
	///////
	cout << "Ack : " << ackPacket.ackno << " sent" << endl;

}
void makeAckChunk(char* data, ack_packet* ackPkt) {

}

void openFile(char *filename, ofstream *myfile) {
	myfile->open(filename);
}

int AppendPacketDataToFile(ofstream *fp, packet* packet) {
	if (packet->len == FILE_FIN)
		return 3;
	if (fp == NULL) {
		return -1;
	}
	if (fp->is_open()) {
		for (int i = 0; i < packet->len; ++i) {
			*fp << packet->data[i];
		}
	} else {
		//file is not opened yet
		return -2;
	}
	//success
	return 1;
}

