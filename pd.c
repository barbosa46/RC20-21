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


int fd_client, fd_server, errcode;
ssize_t n;
socklen_t addrlen_client, addrlen_server;
struct addrinfo hints_client, hints_server, *res_client, *res_server;
struct sockaddr_in adrr_client, addr_server;
char buffer[128];

char pdip[16], pdport[6];
char asip[16], asport[6];

char uid[6], pass[9];

int registered_user = 0;

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
    fd_client = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_client == -1) { fputs("Error: Could not create socket", stderr); exit(1); }

    memset(&hints_client, 0, sizeof hints_client);
    hints_client.ai_family = AF_INET;
    hints_client.ai_socktype = SOCK_DGRAM;

    errcode = getaddrinfo(asip, asport, &hints_client, &res_client);
    if (errcode != 0) { fputs("Error: Could not connect to AS", stderr); exit(1); }
}


void setup_pdserver() {
    fd_server = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_server == -1) { fputs("Error: Could not create socket", stderr); exit(1); }

    memset(&hints_server, 0, sizeof hints_server);
    hints_server.ai_family = AF_INET;
    hints_server.ai_socktype = SOCK_DGRAM;
    hints_server.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL, pdport, &hints_server, &res_server);
    if (errcode != 0) { fputs("Error: Could not get PD address", stderr); exit(1); }

    n = bind(fd_server, res_server->ai_addr, res_server->ai_addrlen);
    if (n == -1) { fputs("Error: Could not bind socket", stderr); exit(1); }
}


void disconnect_from_as() {
    freeaddrinfo(res_client);
    close(fd_client);
}


void disconnect_pdserver() {
    freeaddrinfo(res_server);
    close(fd_server);
}


void register_user() {
    char request[42];

    sprintf(request, "REG %s %s %s %s\n", uid, pass, pdip, pdport);

    n = sendto(fd_client, request, strlen(request), 0, res_client->ai_addr, res_client->ai_addrlen);
    if (n == -1) fputs("Error: Could not send request", stderr);

    addrlen_server = sizeof(addr_server);
    n = recvfrom(fd_client, buffer, 128, 0, (struct sockaddr*) &addr_server, &addrlen_server);
    if (n == -1) fputs("Error: Could not get response from server", stderr);

    registered_user = 1;
}

void unregister_user() {
    char request[20];

    sprintf(request, "UNR %s %s\n", uid, pass);

    n = sendto(fd_client, request, strlen(request), 0, res_client->ai_addr, res_client->ai_addrlen);
    if (n == -1) fputs("Error: Could not send request", stderr);

    addrlen_server = sizeof(addr_server);
    n = recvfrom(fd_client, buffer, 128, 0, (struct sockaddr*) &addr_server, &addrlen_server);
    if (n == -1) fputs("Error: Could not get response from server", stderr);
}


void read_commands() {
    char command[20];
    char action[5];

    while (1) {
        fgets(command, sizeof command, stdin);
        sscanf(command, "%s %s %s\n", action, uid, pass);

        if (strcmp(action, "reg") == 0) register_user();
        else if (strcmp(action, "exit") == 0) {
            if (registered_user) unregister_user();
            return;
        } else fputs("Invalid action!\n", stdout);
    }
}


int main(int argc, char const *argv[]) {
    parse_args(argc, argv);

    connect_to_as();
    setup_pdserver();

    read_commands();

    disconnect_from_as();
    disconnect_pdserver();

    return 0;
}
