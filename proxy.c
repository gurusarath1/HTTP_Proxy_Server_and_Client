
#include <stdio.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#define HTTP_VER "HTTP/1.0"
#define MAX_CACHE_SIZE 10
#define MAX_BUFFER_SIZE 8000
#define BACKLOG 10

#define MAX_DOMAIN_NAME_SIZE 200
#define MAX_PATH_SIZE 500
#define MAX_URL_SIZE 700
#define FIRST_CACHE_ENTRY_NAME_STRING "cacheEntry_a"
#define MAX_DATE_FORMAT_SIZE 30
#define NO_DATA_STRING "<NO_DATA>"


enum HTTP_Req_ops {

	OPTIONS,
	GET,
	HEAD,
	POST,
	PUT,
	DELETE,
	TRACE,
	CONNECT

};

enum cacheStatus {
	HIT_NOT_EXPIRED,
	HIT_EXPIRED,
	MISS_CACHE_NOT_FULL,
	MISS_CACHE_FULL
};

typedef struct CacheEntry {

  char DomainName[MAX_DOMAIN_NAME_SIZE];
  char path[MAX_PATH_SIZE];
  FILE* cacheFileP;
  
  char Expires[MAX_DATE_FORMAT_SIZE];
  char Last_Modified[MAX_DATE_FORMAT_SIZE];
  char Last_Accessed[MAX_DATE_FORMAT_SIZE];

  time_t lastAccessTime;
  

} CacheEntry;


int WriteData(int sock_d, char Buff[], int len);
int getLine(int sock_d, char Buff[], int len);
int decodeRequest(char Buf[], char URL[], char DomainName[], char Path[]);
int ConnectToWebHost (char *host);
int getFullFile_SendFile_CacheFile(int sock, FILE* tempFile, int outSock, char Domain[], char path[], char additionalDataToSend[]);
int getFullFile(int sock, FILE* tempFile);
void extractDateRelatedHeaders(char HeaderBuf[], char Last_Modified_return[], char Expires_return[], char Last_Accessed_return[]);
int checkIf300Code(char buf[]);

int init_cache();
int updateCacheEntry(FILE* content, char Domain[], char path[], char Last_Modified[], char Expires[], char Last_Accessed[]);
int resolveCacheUpdate(char Domain[], char path[], int returnArray[]);
void printCache();

int ProxyServerPort;
char* ProxyServerIP;

CacheEntry ProxyServerCache[MAX_CACHE_SIZE];
int NumberOfElementsInCache = 0;


char receiveBuffer[MAX_BUFFER_SIZE];


int main(int argc, char* argv[])
{
                	

    if(argc < 3)
    {
        printf("CLI Format: proxy <ip to bind> <port to bind>\n");
        return 1;

    } else {
		
		ProxyServerIP = argv[1];
        ProxyServerPort = atoi(argv[2]);

        printf("\n--------------- Proxy Server ---------------");
        printf("\nProxy IP Address: %s", ProxyServerIP);
        printf("\nProxy Port: %d", ProxyServerPort);
        printf("\n--------------------------------------------\n");
    }

   /* MAIN CODE STARTS FROM HERE */

    init_cache();


    struct sockaddr_in ServerAddr_in, clientX;
    int sd_server;

	// Set all memory locations to zero
    memset(&ServerAddr_in, 0, sizeof(ServerAddr_in));
    ServerAddr_in.sin_family = AF_INET; //IPv4
    ServerAddr_in.sin_addr.s_addr = INADDR_ANY;
    ServerAddr_in.sin_port = htons( ProxyServerPort ); // Host to network order conversion


    if( (sd_server = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) // Create socket
    {
        puts("Could not create socket");
        return 1;

    }

    if(bind(sd_server, (struct sockaddr *)&ServerAddr_in, sizeof(ServerAddr_in)) < 0) // Bind to socket
    {
        puts("\nBind action failed !");
        return 1;
    }

    if(listen(sd_server, BACKLOG) < 0)
    {
        puts("\nListen Error !");
        return 1;
    }

    fd_set read_fds, master_copy_read_fds; //Multiplex IO 
    int sd_connection, maxValueOfSD = 0;
    int rcv_len;

    FD_ZERO(&read_fds);
    FD_ZERO(&master_copy_read_fds);
    FD_SET(sd_server, &master_copy_read_fds);
    if(sd_server > maxValueOfSD) maxValueOfSD = sd_server;

    while(1)
    {

        puts("\n\nListening ... ... ... ");
        read_fds = master_copy_read_fds;
        select(maxValueOfSD + 1, &read_fds, NULL, NULL, NULL);

        // New Connection Request
        if (FD_ISSET(sd_server, &read_fds))
        {
			FD_ZERO(&read_fds);

            int sizeX = sizeof(struct sockaddr_in);
			
			sd_connection = accept(sd_server, (struct sockaddr *)&clientX, (socklen_t*)&sizeX);

			if(sd_connection < 0)
			{
				puts("\nConnection Failed !");
				return 1;	
			}

			if(sd_connection > maxValueOfSD) maxValueOfSD = sd_connection;
			FD_SET(sd_connection, &master_copy_read_fds);
			printf("\nNew Connection on %d   Max = %d", sd_connection, maxValueOfSD);

			continue;
        }

        // Read Request
        for(int sd=0; sd<=maxValueOfSD; sd++)
        {

        	if (FD_ISSET(sd, &read_fds) && sd != sd_server)
            {
				
				FD_ZERO(&read_fds);
				
                memset(receiveBuffer, '\0', MAX_BUFFER_SIZE);

                // Read the received message
                rcv_len = getLine(sd, receiveBuffer, MAX_BUFFER_SIZE);

                if(rcv_len == 0)
                {
                	printf("\n%d Closed!",sd);
                	close(sd);

                } else {

                	printf("\nReceived Message: %s      From: %d", receiveBuffer, sd);


                	char URL[MAX_URL_SIZE], domainName[MAX_DOMAIN_NAME_SIZE], path[MAX_PATH_SIZE];
                	decodeRequest(receiveBuffer,URL, domainName, path);

                	printf("\nURL: %s",URL);
                	printf("\nDomain: %s",domainName);
                	printf("\nPath: %s",path);

                	

                	int web_sockfd;

                	if ((web_sockfd = ConnectToWebHost (domainName)) == -1)
                	{
                		printf("\nError Could Not Connect to webSever");
                		return 1;

                	} else {

                		printf("\nConnected to Web Server");
                		char req[1000];


						int cacheInfoArray[3];
						resolveCacheUpdate(domainName, path, cacheInfoArray);

						if(cacheInfoArray[0] == HIT_EXPIRED || cacheInfoArray[0] == HIT_NOT_EXPIRED)
						{
							printf("\nCache HIT");
							printf("\nConditional GET Request -----------");
							if(strcmp(ProxyServerCache[cacheInfoArray[1]].Expires, NO_DATA_STRING))
							{
								snprintf(req, 1000, "%s %s %s\r\nHost: %s\r\nIf-Modified-Since: %s\r\n\r\n", "GET", path, "HTTP/1.0", domainName, ProxyServerCache[cacheInfoArray[1]].Expires);
								
							} else if (strcmp(ProxyServerCache[cacheInfoArray[1]].Last_Modified, NO_DATA_STRING))
							{
								snprintf(req, 1000, "%s %s %s\r\nHost: %s\r\nIf-Modified-Since: %s\r\n\r\n", "GET", path, "HTTP/1.0", domainName, ProxyServerCache[cacheInfoArray[1]].Last_Modified);

							} else if(strcmp(ProxyServerCache[cacheInfoArray[1]].Last_Accessed, NO_DATA_STRING))
							{
								snprintf(req, 1000, "%s %s %s\r\nHost: %s\r\nIf-Modified-Since: %s\r\n\r\n", "GET", path, "HTTP/1.0", domainName, ProxyServerCache[cacheInfoArray[1]].Last_Accessed);	
							}

							write(web_sockfd, req, 1000);
                			printf("\n");
                			puts(req);

                			char rcvBuf[MAX_BUFFER_SIZE];
                			int numberOfBytesRead = read(web_sockfd, rcvBuf, MAX_BUFFER_SIZE);
                			//rcvBuf[MAX_BUFFER_SIZE - 1] = '\0';


                			printf("\nConditional GET Request Complete-----------");

                			//puts(rcvBuf);

                			if(checkIf300Code(rcvBuf))
                			{

                				printf("\nCache Entry is Fresh :)");

                				char CachefileName[13] = FIRST_CACHE_ENTRY_NAME_STRING;
								CachefileName[11] += cacheInfoArray[1];

								printf("\nCache File: %s", CachefileName);
                				FILE* fp_cache = fopen(CachefileName, "r");

                				char tempBuf[MAX_BUFFER_SIZE];

                				while(!feof(fp_cache))
                				{

                					fgets(tempBuf, MAX_BUFFER_SIZE, fp_cache);
                					//puts(tempBuf);
                					WriteData(sd, tempBuf, strlen(tempBuf));
                					if(feof(fp_cache)) break;

                				}


                			} else {

                				printf("\nCache Entry is Stale :)");
                				getFullFile_SendFile_CacheFile(web_sockfd, NULL, sd, domainName, path, rcvBuf);
                				return 0;

                			}


	                		printCache();
	                		close(sd);
	                		FD_CLR(sd, &master_copy_read_fds);



						} else {

							printf("\nCache Miss !");

	                		snprintf(req, 1000, "%s %s %s\r\nHost: %s\r\n\r\n", "GET", path, "HTTP/1.0", domainName);//http://web.mit.edu/dimitrib/www/datanets.html
	                		write(web_sockfd, req, 1000);
	                		printf("\nHTTP Request: ");
	                		puts(req);

	                		//FILE* fp;
	                		//fp = fopen("HTTPtempWeb","w");
	                		getFullFile_SendFile_CacheFile(web_sockfd, NULL, sd, domainName, path, NULL);
	                		//getFullFile(web_sockfd, fp);
	                		//fclose(fp);
	                		

	                		printCache();
	                		close(sd);
	                		FD_CLR(sd, &master_copy_read_fds);


						}


                	}


                }

            }

        }




    } // While (1) End


    close(sd_server);
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

/*
This function decodes the GET request
*/
int decodeRequest(char Buf[], char URL[], char DomainName[], char Path[])
{
	if(strncmp(Buf, "GET", 3) == 0)
	{
		printf("\nGET Request\n");

		int Buffer_index = 4;
		int DomainName_index = 0;
		int URL_index = 0;
		int Path_index = 0;


		char ch;

		do {

			ch = Buf[Buffer_index++];
			if(ch == '/')
			{
				Buffer_index--;
				DomainName[DomainName_index++] = '\0';
				break;
			}

			DomainName[DomainName_index++] = ch;
			URL[URL_index++] = ch;

		} while(ch != '/');

		do {

			ch = Buf[Buffer_index++];
			Path[Path_index++] = ch;
			URL[URL_index++] = ch;

		} while(ch != ' ');

		Path[Path_index] = '\0';
		URL[URL_index] = '\0';


		return 0;

	}

	return 1;
}

/*
This function connects to the web site server
*/
int ConnectToWebHost (char *host) {
  

  int x;
  int sockWeb;
  struct addrinfo temp_addr;
  struct addrinfo *ai, *tempAi;

  memset(&temp_addr, 0, sizeof(temp_addr) );
  temp_addr.ai_family = AF_INET;
  temp_addr.ai_socktype = SOCK_STREAM;

  if ((x = getaddrinfo(host, "http", &temp_addr, &ai)) != 0) {
    printf("\nSERVER ERROR !!");
    return 1;
  }
  for(tempAi = ai; tempAi != NULL; tempAi = tempAi->ai_next) {

    sockWeb = socket(tempAi->ai_family, tempAi->ai_socktype, tempAi->ai_protocol);
    if (sockWeb >= 0 && (connect(sockWeb, tempAi->ai_addr, tempAi->ai_addrlen) >= 0)) 
    {
      break;
    }

  }

  if (tempAi == NULL)
  {
  	sockWeb = -1;
  }
  freeaddrinfo(ai);

  return sockWeb;
  
}

/*
This function initializes the cache
*/
int init_cache()
{
	NumberOfElementsInCache = 0;

	for(int i=0; i<MAX_CACHE_SIZE; i++)
	{
		ProxyServerCache[i].DomainName[0] = '\0';
		ProxyServerCache[i].path[0] = '\0';
		ProxyServerCache[i].cacheFileP = NULL;

		//ProxyServerCache[i].cacheFileP = fopen(fileName, "w");
		//fclose(ProxyServerCache[i].cacheFileP);
		//fileName[11] += 1;
	}
}

/*
This function reads the file from the web server and does a cache update
*/
int getFullFile_SendFile_CacheFile(int sock, FILE* tempFile, int outSock, char Domain[], char path[], char additionalDataToSend[])
{
	int buf_size = 10000;
	char Buf[buf_size];
	int numberOfBytesRead = 0;
	int numberOfBytesRead_Total = 0;

	char CachefileName[13] = FIRST_CACHE_ENTRY_NAME_STRING;
	int cacheInfoArray[3];
	resolveCacheUpdate(Domain, path, cacheInfoArray);
	CachefileName[11] += cacheInfoArray[1];
	FILE* cacheFilePointer = NULL;




	char Expires_formatted[MAX_DATE_FORMAT_SIZE];
  	char Last_Modified_formatted[MAX_DATE_FORMAT_SIZE];
  	char Last_Accessed_formatted[MAX_DATE_FORMAT_SIZE];

  	if(additionalDataToSend != NULL)
  	{
  		WriteData(outSock, additionalDataToSend, MAX_BUFFER_SIZE);
  	}


	int i=0;
	do {

		memset(Buf, 0, sizeof(Buf));
		numberOfBytesRead = read(sock, Buf, buf_size);
		//printf("\n%d", numberOfBytesRead_Total);

		if (numberOfBytesRead > 0)
		{

			if(i == 0)
			{
				char HeaderBuf[800];

				for(int j=0; j<800; j++)
				{
					HeaderBuf[j] = Buf[j];
				}

				if(additionalDataToSend == NULL)
				extractDateRelatedHeaders(HeaderBuf, Last_Modified_formatted, Expires_formatted, Last_Accessed_formatted);

  				printf("\nDATE: %s", Last_Accessed_formatted);
  				printf("\nEXP: %s", Expires_formatted);
  				printf("\nLAST MOD: %s", Last_Modified_formatted);

  				if(!(!strcmp(Last_Modified_formatted,NO_DATA_STRING) && !strcmp(Expires_formatted,NO_DATA_STRING) && !strcmp(Last_Accessed_formatted,NO_DATA_STRING)))
  				{
  					cacheFilePointer = fopen(CachefileName, "w");
  				}

			}



			
			if (tempFile != NULL)
				fprintf(tempFile, "%s" ,Buf);

			if(!(!strcmp(Last_Modified_formatted,NO_DATA_STRING) && !strcmp(Expires_formatted,NO_DATA_STRING) && !strcmp(Last_Accessed_formatted,NO_DATA_STRING)))
			{
				fprintf(cacheFilePointer, "%s" ,Buf);
			}

			WriteData(outSock, Buf, numberOfBytesRead);
			numberOfBytesRead_Total += numberOfBytesRead;
		}

		i++;

	} while (numberOfBytesRead);

	fclose(cacheFilePointer);

	updateCacheEntry(NULL, Domain, path, Last_Modified_formatted, Expires_formatted, Last_Accessed_formatted);

	return numberOfBytesRead_Total;

}

/*
This function updates the entry in the cache
*/
int updateCacheEntry(FILE* content, char Domain[], char path[], char Last_Modified[], char Expires[], char Last_Accessed[])
{

	int cacheUpdateInfo[3];
	int x = resolveCacheUpdate(Domain, path, cacheUpdateInfo);


	// Do not update the cache if no date related header is present
	if(!strcmp(Last_Modified,NO_DATA_STRING) && !strcmp(Expires,NO_DATA_STRING) && !strcmp(Last_Accessed,NO_DATA_STRING))
	{
		printf("\nNo Date Related field found!!");
		printf("\nCache Entry Dropped!");
		return 1;
	}



		if(cacheUpdateInfo[0] == HIT_NOT_EXPIRED)
		{

			//printf("\nHIT NOT EXPIRED");

			ProxyServerCache[cacheUpdateInfo[1]].cacheFileP = content;
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].DomainName, Domain);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].path, path);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].Last_Accessed, Last_Accessed);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].Last_Modified, Last_Modified);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].Expires, Expires);
			ProxyServerCache[cacheUpdateInfo[1]].lastAccessTime = time(NULL);

		}

		if(cacheUpdateInfo[0] == MISS_CACHE_NOT_FULL)
		{

			//printf("\nMISS NOT FULL");

			ProxyServerCache[NumberOfElementsInCache].cacheFileP = content;
			strcpy(ProxyServerCache[NumberOfElementsInCache].DomainName, Domain);
			strcpy(ProxyServerCache[NumberOfElementsInCache].path, path);
			strcpy(ProxyServerCache[NumberOfElementsInCache].Last_Accessed, Last_Accessed);
			strcpy(ProxyServerCache[NumberOfElementsInCache].Last_Modified, Last_Modified);
			strcpy(ProxyServerCache[NumberOfElementsInCache].Expires, Expires);
			ProxyServerCache[NumberOfElementsInCache].lastAccessTime = time(NULL);

			NumberOfElementsInCache++;
			if (NumberOfElementsInCache > MAX_CACHE_SIZE)
			{
				NumberOfElementsInCache = MAX_CACHE_SIZE - 1;

			}
		}

		if(cacheUpdateInfo[0] == MISS_CACHE_FULL)
		{
			//printf("\nMISS FULL");
			
			ProxyServerCache[cacheUpdateInfo[1]].cacheFileP = content;
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].DomainName, Domain);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].path, path);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].Last_Accessed, Last_Accessed);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].Last_Modified, Last_Modified);
			strcpy(ProxyServerCache[cacheUpdateInfo[1]].Expires, Expires);
			ProxyServerCache[cacheUpdateInfo[1]].lastAccessTime = time(NULL);

		}


	return NumberOfElementsInCache;
}

/*
This function determines HIT or MISS and also implements LRU cache eviction policy
*/
int resolveCacheUpdate(char Domain[], char path[], int returnArray[])
{
	int DomainMatch = 0;
	int PathMatch = 0;

	// This loop will detect Cache HIT
	for(int i=0; i < NumberOfElementsInCache; i++)
	{
		DomainMatch = !strcmp(ProxyServerCache[i].DomainName, Domain);
		PathMatch = !strcmp(ProxyServerCache[i].path, path);

		if(DomainMatch && PathMatch)
		{
			//printf("\nHIT");
			returnArray[0] = HIT_NOT_EXPIRED;
			returnArray[1] = i;
			return 0;
		}
	}

	// MISS
	if(NumberOfElementsInCache < MAX_CACHE_SIZE)
	{
		//printf("\nMISS - Cache Not Full");
		returnArray[0] = MISS_CACHE_NOT_FULL;
		returnArray[1] = NumberOfElementsInCache;
		return 0;

	} else {

		//printf("\nMISS - Cache Full ");
		returnArray[0] = MISS_CACHE_FULL;


		long int MintimeX = ProxyServerCache[0].lastAccessTime;
		for(int i=1; i < NumberOfElementsInCache; i++)
		{
			if(ProxyServerCache[i].lastAccessTime < MintimeX)
			{
				MintimeX = ProxyServerCache[i].lastAccessTime;
				returnArray[1] = i;
			}
		}

		return 0;
	}
	return -1;
}

/*
Prints the list of cached websites
*/
void printCache()
{
	printf("\n-------------CACHE----------------");
	for(int i=0; i < NumberOfElementsInCache; i++)
	{
		printf("\n%d - URL: %s%s", i+1, ProxyServerCache[i].DomainName, ProxyServerCache[i].path);
		printf("\n    Last Accessed: %s", ProxyServerCache[i].Last_Accessed);
		printf("\n    Last Modified: %s", ProxyServerCache[i].Last_Modified);
		printf("\n    Expiries: %s", ProxyServerCache[i].Expires);
	}
	printf("\n----------------------------------");
}

/*
Gets full file from the web server
*/
int getFullFile(int sock, FILE* tempFile)
{
	int buf_size = 1000;
	char Buf[buf_size];
	int numberOfBytesRead = 0;
	int numberOfBytesRead_Total = 0;

	
	do {

		memset(Buf, 0, sizeof(Buf));
		numberOfBytesRead = read(sock, Buf, buf_size);
		printf("\n%d", numberOfBytesRead_Total);

		if (numberOfBytesRead > 0)
		{		
			fprintf(tempFile, "%s" ,Buf);
			numberOfBytesRead_Total += numberOfBytesRead;
		}

	} while (numberOfBytesRead);

	return numberOfBytesRead_Total;

}

/*
extractDateRelatedHeaders extracts data from header of HTTP 
*/
void extractDateRelatedHeaders(char HeaderBuf[], char Last_Modified_return[], char Expires_return[], char Last_Accessed_return[])
{
				char *Expires;
  				char *Last_Modified;
  				char *Last_Accessed;



				int k,j;

  				Last_Accessed = strstr(HeaderBuf, "Date: ");
  				Expires = strstr(HeaderBuf, "Expires: ");
  				Last_Modified = strstr(HeaderBuf, "Last-Modified: ");

  				
  				if(Last_Accessed != NULL)
  				{
  					k = strlen("Date: ") ;
  					j = 0;
	  				while(Last_Accessed[k] != '\r')
	  				{
	  					Last_Accessed_return[j] = Last_Accessed[k];
	  					k++;
	  					j++;

	  					if(j >= MAX_DATE_FORMAT_SIZE) break;
  					}  					
  				} else {
  					strcpy(Last_Accessed_return, NO_DATA_STRING);
  				}



  				if(Expires != NULL)
  				{
  					k = strlen("Expires: ") ;
  					j = 0;
	  				while(Expires[k] != '\r')
	  				{
	  					Expires_return[j] = Expires[k];
	  					k++;
	  					j++;

	  					if(j >= MAX_DATE_FORMAT_SIZE) break;
	  				}  					
  				} else {
  					strcpy(Expires_return, NO_DATA_STRING);
  				}



  				if(Last_Modified != NULL)
  				{
  					k = strlen("Last-Modified: ") ;
  					j = 0;
	   				while(Last_Modified[k] != '\r')
	  				{
	  					Last_Modified_return[j] = Last_Modified[k];
	  					k++;
	  					j++;

	  					if(j >= MAX_DATE_FORMAT_SIZE) break;
	  				} 					
  				} else {
  					strcpy(Last_Modified_return, NO_DATA_STRING);
  				}
}

/*
Checks the response of a conditional GET
3XX or 2XX code in the header
*/
int checkIf300Code(char buf[])
{

	int i = 0;

	if(buf[0] != 'H')
	{
		printf("\nNOT HTTP HEADER!");
		return 0;
	}

	while(buf[i] != '\r')
	{
		if(buf[i] == '3')
		{
			return 1;
		}

		i++;
	}

	return 0;
}