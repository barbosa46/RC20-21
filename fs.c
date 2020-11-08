#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>

#include "fs.h"


/* Socket vars */
int fd_as, fd_fs, errcode;
ssize_t n, nw;
socklen_t addrlen_as, addrlen_fs;
struct addrinfo hints_as, hints_fs, *res_as, *res_fs;
struct sockaddr_in addr_as, addr_fs, sa;
char buffer[128];

/* errno */
extern int errno;

/* Comms info */
char asip[18], asport[8];
char fsport[8];

/* Verbose control flag */
int verbose_mode = 0;


void usage() {
    fputs("usage: ./fs  [-q FSport] [-n ASIP] [-p ASport] [-v]\n", stderr);
    exit(1);
}

void kill_fs(int signum) {  // kill child process if parent exits
    disconnect_fs();
    exit(0);
}


void protocol_error() {  // basic protocol error; reply with "ERR\n"
    char response[5];

    while ((n = read(fd_fs, buffer, 127)) > 0);  // flush socket

    bzero(response, 5);
    strcpy(response, "ERR\n");

    n = 5;

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; response = &response[nw];
    }

    write(fd_fs, response, 5);
}


void syntax_error(int error) {  // error gives error info
    if (error == IP_INVALID) fputs("Error: Invalid IP address. Exiting...\n", stderr);
    else if (error == PORT_INVALID) fputs("Error: Invalid port. Exiting...\n", stderr);

    exit(1);
}


int is_only(int which, char *str) {  // check if string is only numeric, alpha, ...
    if (which == NUMERIC) {
        while (*str) { if (isdigit(*str++) == 0) return 0; }
        return 1;

    } else if (which == ALPHANUMERIC) {
        while (*str) { if (isdigit(*str++) == 0 && isalpha(*(str - 1)) == 0) return 0; }
        return 1;

    } else if (which == ALPHA) {
        while (*str) { if (isalpha(*str++) == 0) return 0; }
        return 1;

    } else if (which == FILE_CHARS) {
        while (*str) {
            if (isdigit(*str++) == 0 && isalpha(*(str - 1)) == 0 && !strchr("-_", *(str - 1)))
                return 0;
            }

        return 1;

    } else if (which == IP) {
        int result = inet_pton(AF_INET, str, &(sa.sin_addr));
        return result != 0;

    } else if (which == OP) {
        return strlen(str) == 1 && strchr("RUDLX", str[0]);

    } else if (which == FILE) {
        int result;

        if (strlen(str) > 24) return 0;
        if (str[strlen(str) - 4] != '.') return 0;

        str[strlen(str) - 4] = '\0';

        result = is_only(FILE_CHARS, str) && is_only (ALPHA, str + strlen(str) + 1);

        str[strlen(str)] = '.';

        return result;

    } return 0;
}


void parse_args(int argc, char const *argv[]) {  // parse flags and flag args
    int opt;

    if (argc > 8) usage();  // numargs in range

    /* default values */
    strncpy(asip, "localhost", 16);
    strncpy(asport, "58046", 6);
    strncpy(fsport, "59046", 6);

    while ((opt = getopt(argc, (char * const*) argv, "q:n:p:v")) != -1) {
        if (optarg[0] == '-') usage();

        switch (opt) {
            /* check flag; parse args
               ifs check if args are valid */
            case 'q':
                strncpy(fsport, optarg, 6);
                if (strlen(fsport) == 0 || strlen(fsport) > 5 ||
                    !is_only(NUMERIC, fsport) || atoi(fsport) > 65535)
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

            case 'v':
                verbose_mode = 1;

                break;

            default:
                usage();
        }
    }
}


void setup_fsserver() {  // set up fs server to receive and perform operations
    fd_fs = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_fs == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_fs, 0, sizeof hints_fs);
    hints_fs.ai_family = AF_INET;
    hints_fs.ai_socktype = SOCK_STREAM;
    hints_fs.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL, fsport, &hints_fs, &res_fs);
    if (errcode != 0) { fputs("Error: Could not bind FS. Exiting...\n", stderr); exit(1); }

    n = bind(fd_fs, res_fs->ai_addr, res_fs->ai_addrlen);
    if (n == -1) { fputs("Error: Could not bind FS. Exiting...\n", stderr); exit(1); }

    if (listen(fd_fs, BACKLOG) == -1) { fputs("Error: Could not set up FS. Exiting...\n", stderr); exit(1); }
}


void connect_to_as() {  // standard udp connection setup to as
    fd_as = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_as == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_as, 0, sizeof hints_as);
    hints_as.ai_family = AF_INET;
    hints_as.ai_socktype = SOCK_DGRAM;

    errcode = getaddrinfo("tejo.tecnico.ulisboa.pt", "58011", &hints_as, &res_as);
    if (errcode != 0) { fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1); }
}


void disconnect_from_as() {  // standard udp disconnect
    freeaddrinfo(res_as);
    close(fd_as);
}


void disconnect_fs() {  // disconnect fs sub-server
    freeaddrinfo(res_fs);
    close(fd_fs);
}


void receive_requests() {  // receive requests from socket
    char rcode[5];
    pid_t pid, ppid;
    struct sigaction act;
    int newfd;
    int ret;

    /* avoid zombie processes */
    act.sa_handler = SIG_IGN;
    if (sigaction(SIGCHLD, &act, NULL) == -1) exit(1);

    /* wait for connections */
    while (1) {
        addrlen_fs = sizeof(addr_fs);

        do newfd = accept(fd_fs, (struct sockaddr*) &addr_fs, &addrlen_fs); while (newfd == -1 && errno == EINTR);
        if(newfd == -1) exit(EXIT_FAILURE);

        ppid = getpid();
        pid = fork();

        if (pid == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        /* fork; continue listening if parent, perform operation if child */
        if (pid) {
            do ret = close(newfd); while(ret == -1 && errno == EINTR);  // close new socket
            if(ret == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        } else {
            signal(SIGTERM, kill_fs);  // kill fs sub-server if SIGTERM received

            n = prctl(PR_SET_PDEATHSIG, SIGTERM);  // send SIGTERM to child if parent exits
            if (n == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

            if (getppid() != ppid) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

            /* close accept socket; swap new and accept socket */
            close(fd_fs);
            fd_fs = newfd;

            bzero(buffer, 128);
            bzero(rcode, 5);
            while ((n = read(fd_fs, buffer, 4)) > 0) {
                strncpy(rcode, buffer, 4);  // read request code + space

                break;
            }

            /* perform operation acording to rcode; error if invalid */
            if (strcmp(rcode, "LST ") == 0) list_files();
            else if (strcmp(rcode, "RTV ") == 0) retrive_file();
            else if (strcmp(rcode, "UPL ") == 0) upload_file();
            else if (strcmp(rcode, "DEL ") == 0) delete_file();
            else if (strcmp(rcode, "REM ") == 0) remove_user();
            else protocol_error();

            exit(0);  // child exits if successful

        }
    }
}


int main(int argc, char const *argv[]){
    parse_args(argc, argv);

    setup_fsserver();
    connect_to_as();

    receive_requests();

    disconnect_from_as();
    disconnect_fs();

    return 0;

}
