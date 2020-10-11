#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int main (int argc, char *argv[]){

int sockfd,errcode;
int c;
ssize_t n;
socklen_t addrlen;
struct addrinfo hints,*res;
struct sockaddr_in servaddr;
char recvline[128];
char buffer[128];
char asip[50] = "localhost";
char asport[20] = "58046";
char fsip[20] = "localhost";
char fsport[20] = "59046";

 while ((c = getopt (argc, argv, ":n:p:m:q")) != -1)
  switch(c){
    case 'n':
      strcpy(asip, optarg);
      break;

    case 'p':
      strcpy(asport, optarg);
      break;

    case 'm':
      strcpy(fsip, optarg);
      break;

    case 'q':
      strcpy(fsport, optarg);
      break;

    default:
      printf("Error message: implement usage later\n" );
      exit(EXIT_FAILURE);
  }


  res= (struct addrinfo*)malloc(sizeof(struct addrinfo));
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd<0)
    perror("Could not create socket!");
  bzero(&servaddr, sizeof(servaddr));
  memset(&hints,0,sizeof(hints));
  hints.ai_family=AF_INET;
  hints.ai_socktype=SOCK_STREAM;


  if(getaddrinfo(asip, asport, &hints, &res) < 0 )
    perror("getaddrinfo");

  if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0 )
    perror("Connect:");
  sprintf(buffer, "LOG 12345 password\n");

  int sendbytes= strlen(buffer);

  if(write(sockfd, buffer, sendbytes) != sendbytes )
    perror("Couldn't write to socket!");

  memset(recvline, 0, 128);
  int a=0;
  while( (a= read(sockfd, recvline, 127) > 0)){
    printf("%s", recvline);
  }

  if (a < 0)
    perror("Couldn't read");

  exit(0);


}
