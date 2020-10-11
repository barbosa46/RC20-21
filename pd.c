#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "pd.h"


int fd, errcode;
ssize_t n;
socklen_t addrlen;
struct addrinfo hints, *res;
struct sockaddr_in addr;
char buffer[128];

char pdip[16], pdport[6];
char asip[16], asport[6];

void usage() {
    fputs("usage: ./pd PDIP [-d PDport] [-n ASIP] [-p ASport]\n", stderr);
    exit(1);
}


void parse_args(int argc, char const *argv[]) {
    int opt;

    if (argc == 1 || argc > 8) usage();

    strcpy(pdip, argv[1]);
    strcpy(pdport, "57046");
    strcpy(asip, argv[1]);
    strcpy(asport, "58046");

    while ((opt = getopt(argc, (char * const*) argv, "d:n:p:")) != -1) {
        if (optarg[0] == '-') usage();

        switch (opt) {
            case 'd':
                strcpy(pdport, optarg);
                break;

            case 'n':
                strcpy(asip, optarg);
                break;

            case 'p':
                strcpy(asport, optarg);
                break;

            default:
                usage();
        }
    }
}


void connect_to_as() {
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) exit(1);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    errcode = getaddrinfo(asip, asport, &hints, &res);
    if (errcode != 0) { fputs("Error: Could not connect to AS", stderr); exit(1); }
}


void disconnect_from_as() {
    freeaddrinfo(res);
    close(fd);
}


void register_user(char *uid, char *pass) {
    char request[42];

    sprintf(request, "REG %s %s %s %s\n", uid, pass, pdip, pdport);

    n = sendto(fd, request, strlen(request), 0, res->ai_addr, res->ai_addrlen);
    if (n == -1) fputs("Error: Could not send request", stderr);

    addrlen = sizeof(addr);
    n = recvfrom(fd, buffer, 128, 0, (struct sockaddr*) &addr, &addrlen);
    if (n == -1) fputs("Error: Could not get response from server", stderr);

}


void read_commands() {
    char command[20];
    char action[5], uid[6], pass[9];

    while (1) {
        fgets(command, sizeof command, stdin);
        sscanf(command, "%s %s %s\n", action, uid, pass);

        if (strcmp(action, "reg") == 0) register_user(uid, pass);
        else if (strcmp(action, "exit") == 0) return;
        else fputs("Invalid action!\n", stdout);
    }
}


int main(int argc, char const *argv[]) {
    parse_args(argc, argv);

    connect_to_as();

    read_commands();

    disconnect_from_as();

    return 0;
}
