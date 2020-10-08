#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "58000"

int main (int argc, char *argv[]){

int fd,errcode;
int c;
ssize_t n;
socklen_t addrlen;
struct addrinfo hints,*res;
struct sockaddr_in addr;
char buffer[128];
char asip[20] = "localhost";
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




fd=socket(AF_INET,SOCK_STREAM,0);
if (fd==-1) exit(1); //error


memset(&hints,0,sizeof hints);
hints.ai_family=AF_INET;
hints.ai_socktype=SOCK_STREAM;

errcode=getaddrinfo("localhost",PORT,&hints,&res);
if(errcode!=0)/*error*/exit(1);

n=connect(fd,res->ai_addr,res->ai_addrlen);
if(n==-1)/*error*/exit(1);

/*
  Client should work continuosly
  while(1){

  }
*/

n=write(fd,"Hello!\n",7);
if(n==-1)/*error*/exit(1);

n=read(fd,buffer,128);
if(n==-1)/*error*/exit(1);

freeaddrinfo(res);
close(fd);
}
