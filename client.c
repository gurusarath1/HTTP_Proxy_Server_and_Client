#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>

#define HTTP_VER HTTP/1.0


int WriteData(int sock_d, char Buff[], int len);
int getLine(int sock_d, char Buff[], int len);

int create_GET_request(char* GET_request, char* URL);
int getFullFile(int sock, FILE* tempFile);


int ProxyServerPort;
char* ProxyServerIP;
char* URL_to_retrieve;



int main(int argc, char* argv[])
{

    if(argc < 4)
    {
        printf("CLI Format: client <proxy address> <proxyport> <URLto retrieve>\n");
        return 1;

    } else {
		
		ProxyServerIP = argv[1];
        ProxyServerPort = atoi(argv[2]);
        URL_to_retrieve = argv[3];

        printf("\n--------------- Request Summary ---------------");
        printf("\nURL: %s", URL_to_retrieve);
        printf("\nProxy IP Address: %s", ProxyServerIP);
        printf("\nProxy Port: %d", ProxyServerPort);
        printf("\n-----------------------------------------------\n");
    }



    int sd_Client;
    struct sockaddr_in server;


    //Create socket
	sd_Client = socket(AF_INET , SOCK_STREAM , 0);
	if (sd_Client == -1)
	{
		printf("\nCould not create socket");
		return 1;
	}

	//server.sin_addr.s_addr = inet_addr(host);
	server.sin_addr.s_addr = inet_addr(ProxyServerIP);
	server.sin_family = AF_INET;
	server.sin_port = htons(ProxyServerPort);

	//Connect to remote server
	if ( connect(sd_Client , (struct sockaddr *)&server , sizeof(server)) < 0)
	{
		printf("\nConnect error");
		return 1;
	}
	
	puts("\nConnected");


	// Create the GET request
	char GET_req[500];
	memset(GET_req, 0, 500);
	create_GET_request(GET_req, URL_to_retrieve);
	puts(GET_req);

	WriteData(sd_Client, GET_req, strlen(GET_req));

	FILE* fp, *FinalOutputFile;
	fp = fopen("temp", "w");
	getFullFile(sd_Client, fp);
	fclose(fp);

	close(sd_Client);

	fp = fopen("temp", "r");
	FinalOutputFile = fopen("FinalFile.html", "w");

	char tempBuf[10000];

	int endOFHeaderFlag = 0;

	printf("\n---------------------------------------------");
	while(!feof(fp))
    {

        fgets(tempBuf, 10000, fp);

        if(!endOFHeaderFlag)
        	puts(tempBuf);

        if(endOFHeaderFlag)
        	fputs(tempBuf, FinalOutputFile);

        if(tempBuf[0] == '\r' && tempBuf[1] == '\n')
        	endOFHeaderFlag = 1;
    }

    fclose(fp);
    fclose(FinalOutputFile);

    printf("\n---------------------------------------------\n\n");


	return 0;
}

int create_GET_request(char* GET_request, char* URL)
{
	sprintf(GET_request, "GET %s HTTP/1.0\r\n", URL);
	return 0;
}

int WriteData(int sock_d, char Buff[], int len)
{
    int n;
    n = send(sock_d, Buff, len, 0);
    return n;
}

int getLine(int sock_d, char Buff[], int len)
{
	int n;
	n = recv(sock_d, Buff, len-1, 0);
	return n;
}

int getFullFile(int sock, FILE* tempFile)
{
	int buf_size = 10000;
	char Buf[buf_size];
	int numberOfBytesRead = 0;
	int numberOfBytesRead_Total = 0;

	int i = 1;
	do {

		memset(Buf, 0, sizeof(Buf));
		numberOfBytesRead = read(sock, Buf, buf_size);
		printf("\nPacket:%d Size:%d", i, numberOfBytesRead_Total);

		if (numberOfBytesRead > 0)
		{		
			fprintf(tempFile, "%s" ,Buf);
			numberOfBytesRead_Total += numberOfBytesRead;
		}

		if(Buf[numberOfBytesRead - 1] == EOF) break;

		i++;

	} while (numberOfBytesRead);

	return numberOfBytesRead_Total;

}
