#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "pd.h"


/* Socket vars */
int fd_client, fd_server, errcode;
ssize_t n;
socklen_t addrlen_client, addrlen_server;
struct addrinfo hints_client, hints_server, *res_client, *res_server;
struct sockaddr_in addr_client, addr_server, sa;
char buffer[128];
struct timeval timeout;  // for timeout

/* Comms info */
char pdip[18], pdport[8];
char asip[18], asport[8];

/* User info */
char uid[8], pass[10];

/* Register control */
int registered_user = 0;


void usage() {
    fputs("usage: ./pd PDIP [-d PDport] [-n ASIP] [-p ASport]\n", stderr);
    exit(1);
}


void kill_pdserver(int signum) {  // kill child process if parent exits
    disconnect_pdserver();
    exit(0);
}


void syntax_error(int error) {  // error gives error info
    if (error == IP_INVALID) fputs("Error: Invalid IP address. Exiting...\n", stderr);
    else if (error == PORT_INVALID) fputs("Error: Invalid port. Exiting...\n", stderr);
    else if (error == USER) {
        fputs("Error: UID must be 5 characters long and consist only of numbers. Try again!\n", stderr); return; }
    else if (error == PASS) {
        fputs("Error: Password must be 8 characters long and consist only of alphanumeric characters. Try again!\n", stderr); return; }
    else fputs("Unknown errror. Exiting...\n", stderr);

    exit(1);
}


void message_error(int error) {  // error gives error info
    if (error == REG) fputs("Error: Invalid user ID or password. Try again!\n", stderr);
    else if (error == UNR) { fputs("Error: Unregister unsuccessful. Exiting...\n", stderr); exit(1); }
    else if (error == UNK) fputs("Error: Unexpected protocol message. Might not have performed operation\n", stderr);
    else fputs("Unknown errror. Aborting...\n", stderr);
}


int is_only(int which, char *str) {  // check if string is only numeric, alpha, ...
    if (which == NUMERIC) {
        while (*str) { if (isdigit(*str++) == 0) return 0; }
        return 1;

    } else if (which == ALPHANUMERIC) {
        while (*str) { if (isdigit(*str++) == 0 && isalpha(*(str - 1)) == 0) return 0; }
        return 1;

    } else if (which == IP) {
        int result = inet_pton(AF_INET, str, &(sa.sin_addr));
        return result != 0;

    } return 0;
}


void parse_args(int argc, char const *argv[]) {  // parse flags and flag args
    int opt;

    if (argc == 1 || argc > 8) usage();  // numargs in range

    /* default values */
    strncpy(pdip, argv[1], 16);
    strncpy(pdport, "57046", 6);
    strncpy(asip, argv[1], 16);
    strncpy(asport, "58046", 6);

    if (!is_only(IP, pdip)) syntax_error(IP_INVALID);

    while ((opt = getopt(argc, (char * const*) argv, "d:n:p:")) != -1) {
        if (optarg[0] == '-') usage();

        switch (opt) {
            /* check flag; parse args
               ifs check if args are valid */
            case 'd':
                strncpy(pdport, optarg, 6);
                if (strlen(pdport) == 0 || strlen(pdport) > 5 ||
                    !is_only(NUMERIC, pdport) || atoi(pdport) > 65535)
                    syntax_error(PORT_INVALID);

                break;

            case 'n':
                strncpy(asip, optarg, 16);
                if (!is_only(IP, asip)) syntax_error(IP_INVALID);

                break;

            case 'p':
                strncpy(asport, optarg, 6);
                if (strlen(asport) == 0 || strlen(asport) > 5 ||
                    !is_only(NUMERIC, asport) || atoi(asport) > 65535)
                    syntax_error(PORT_INVALID);

                break;

            default:
                usage();
        }
    }
}


void connect_to_as() {  // standard udp connection setup to as
    fd_client = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_client == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_client, 0, sizeof hints_client);
    hints_client.ai_family = AF_INET;
    hints_client.ai_socktype = SOCK_DGRAM;

    errcode = getaddrinfo(asip, asport, &hints_client, &res_client);
    if (errcode != 0) { fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1); }

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(fd_client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fputs("Error: Could not set up timeout. Exiting...\n", stderr); exit(1);
    }
}


void setup_pdserver() {  // set up server on pd to receive validation codes
    fd_server = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_server == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_server, 0, sizeof hints_server);
    hints_server.ai_family = AF_INET;
    hints_server.ai_socktype = SOCK_DGRAM;
    hints_server.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL, pdport, &hints_server, &res_server);
    if (errcode != 0) { fputs("Error: Could not get PD address. Exiting...\n", stderr); exit(1); }

    n = bind(fd_server, res_server->ai_addr, res_server->ai_addrlen);
    if (n == -1) { fputs("Error: Could not bind socket. Exiting...\n", stderr); exit(1); }
}


void disconnect_from_as() {  // standard udp disconnect from as
    freeaddrinfo(res_client);
    close(fd_client);
}


void disconnect_pdserver() {  // disconnect pd server
    freeaddrinfo(res_server);
    close(fd_server);
}


void register_user() {  // register user in as
    char request[42];
    pid_t pid, ppid;

    sprintf(request, "REG %s %s %s %s\n", uid, pass, pdip, pdport);

    n = sendto(fd_client, request, strlen(request), 0, res_client->ai_addr, res_client->ai_addrlen);
    if (n == -1) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    addrlen_server = sizeof(addr_server);
    n = recvfrom(fd_client, buffer, 128, 0, (struct sockaddr*) &addr_server, &addrlen_server);
    if (n == -1) { fputs("Error: Could not get response from server. Try again!\n", stderr); return; }
    else buffer[n] = '\0';

    /* reply parsing */
    if (strcmp(buffer, "RRG OK\n") == 0) fputs("Registration successful!\n", stdout);
    if (strcmp(buffer, "RRG NOK\n") == 0) { message_error(REG); return; }
    if (strcmp(buffer, "ERR\n") == 0) { message_error(UNK); return; }

    registered_user = 1;  // set register control flag

    ppid = getpid();
    pid = fork();

    if (pid == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

    /* fork; return if parent, set up pd server if child */
    if (pid) return;
    else {
        signal(SIGTERM, kill_pdserver);  // kill pd server if SIGTERM received
        setvbuf(stdout, NULL, _IONBF, 0);  // make stdout unbuffered; for aesthetic purposes

        n = prctl(PR_SET_PDEATHSIG, SIGTERM);  // send SIGTERM to child if parent exits
        if (n == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        if (getppid() != ppid) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        setup_pdserver();

        get_vc();  // child listens for validation codes
    }
}


void unregister_user() {  // unregister user from as
    char request[20];

    sprintf(request, "UNR %s %s\n", uid, pass);

    n = sendto(fd_client, request, strlen(request), 0, res_client->ai_addr, res_client->ai_addrlen);
    if (n == -1) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    addrlen_server = sizeof(addr_server);
    n = recvfrom(fd_client, buffer, 128, 0, (struct sockaddr*) &addr_server, &addrlen_server);
    if (n == -1) { fputs("Error: Could not get response from server. Try again!\n", stderr); return; }
    else buffer[n] = '\0';


    /* reply parsing */
    if (strcmp(buffer, "RUN NOK\n") == 0) message_error(UNR);
    if (strcmp(buffer, "ERR\n") == 0) message_error(UNK);
}


void get_vc() {  // listen for validation codes
    char request[64], response[20];
    char action[6], v_uid[8], vc[6], fop[4], fname[26];
    char operation[10];

    while (1) {
        addrlen_client = sizeof(addr_client);
        n = recvfrom(fd_server, request, 64, 0, (struct sockaddr*) &addr_client, &addrlen_client);
        if (n == -1) { fputs("Error: Could not get VC request from AS.\n", stderr); return; }
        else request[n] = '\0';

        bzero(fname, 26);

        /* reply parsing; extract operation and filename (optional) */
        sscanf(request, "%5s %7s %5s %3s %25s\n", action, v_uid, vc, fop, fname);
        if (strcmp(action, "VLC") != 0 || strlen(v_uid) != 5 || !is_only(NUMERIC, v_uid) ||
            strlen(vc) != 4 || !is_only(NUMERIC, vc) || strlen(fop) != 1 || !strchr("RUDLX", fop[0]) ||
            strlen(fname) > 24) sprintf(response, "ERR\n");
        else if (((strcmp(fop, "L") == 0) || (strcmp(fop, "X") == 0)) && strlen(fname) != 0)
            sprintf(response, "ERR\n");
        else if (strcmp(v_uid, uid) != 0) sprintf(response, "RVC %s NOK\n", v_uid);
        else  {
            sprintf(response, "RVC %s OK\n", uid);

            if (strcmp(fop, "L") == 0) strcpy(operation, "list");
            else if (strcmp(fop, "R") == 0) strcpy(operation, "retrieve");
            else if (strcmp(fop, "U") == 0) strcpy(operation, "upload");
            else if (strcmp(fop, "D") == 0) strcpy(operation, "delete");
            else if (strcmp(fop, "X") == 0) strcpy(operation, "remove");

            if ((strcmp(fop, "L") == 0) || (strcmp(fop, "X") == 0))
                fprintf(stdout, "...\nVC: %s | %s\n> ", vc, operation);
            else if ((strcmp(fop, "R") == 0) || (strcmp(fop, "U") == 0) || (strcmp(fop, "D") == 0))
                fprintf(stdout, "...\nVC: %s | %s: %s\n> ", vc, operation, fname);
        }

        n = sendto(fd_server, response, strlen(response), 0, (struct sockaddr*) &addr_client, addrlen_client);
        if (n == -1) { fputs("Error: Could not send response to AS.\n", stderr); return; }

    }
}


void read_commands() {  // read commands from stdin
    char command[64];
    char action[6];

    while (1) {
        fputs("> ", stdout);  // for aesthetic purposes

        bzero(command, 64);
        bzero(action, 6);

        /* parse command */
        fgets(command, sizeof command, stdin);
        sscanf(command, "%5s %7s %9s\n", action, uid, pass);

        /* get action from commands
           ifs check if args are valid */
        if (strcmp(action, "reg") == 0) {
            if (strlen(uid) != 5 || !is_only(NUMERIC, uid)) { syntax_error(USER); continue; }
            if (strlen(pass) != 8 || !is_only(ALPHANUMERIC, pass)) { syntax_error(PASS); continue; }

            register_user();

        } else if (strcmp(action, "exit") == 0) {
            if (registered_user) unregister_user();
            return;

        } else fputs("Invalid action!\n", stdout);
    }
}


int main(int argc, char const *argv[]) {
    parse_args(argc, argv);

    connect_to_as();

    read_commands();

    disconnect_from_as();

    return 0;
}
