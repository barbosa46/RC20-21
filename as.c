#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>

#include "as.h"

/* Socket vars */
int fd_tcp, fd_udp, fd_pdserver, errcode;
ssize_t n, nw;
socklen_t addrlen_tcp, addrlen_udp, addrlen_pdserver;
struct addrinfo hints_tcp, hints_udp, hints_pdserver, *res_tcp, *res_udp, *res_pdserver;
struct sockaddr_in addr_tcp, addr_udp, addr_pdserver, sa;
char buffer[1024];
struct timeval timeout;  // for timeout

/* Comms info */
char asport[8];
char pdip[18], pdport[8];

/* errno */
extern int errno;

/* Client info */
char cip[18];
int cport;

/* Validation data */
int vc = 9999, tid = 9999, rid = 9999;

/* Verbose control flag */
int verbose_mode = 0;


void usage() {
    fputs("usage: ./AS [-p ASport] [-v]\n", stderr);
    exit(1);
}


void kill_tcp(int signum) {  // kill child process if parent exits
    disconnect_tcpserver();
    exit(0);
}


void kill_udp(int signum) {  // kill child process if parent exits
    disconnect_udpserver();
    exit(0);
}


void protocol_error_tcp() {  // basic protocol error; reply with "ERR\n"
    char response[5];

    bzero(response, 5);
    strcpy(response, "ERR\n");

    n = 5;

    while (n > 0) {  // write response
        if ((nw = write(fd_tcp, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }

    disconnect_tcpserver();

    exit(1);

}


void protocol_error_udp() {
    char response[5];

    bzero(response, 5);
    strcpy(response, "ERR\n");

    // send response to pd, will timeout if lost
    n = sendto(fd_udp, response, strlen(response), 0, (struct sockaddr*) &addr_udp, addrlen_udp);

    exit(1);

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

    } else if (which == FILENAME) {
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

    if (argc > 4) usage();  // numargs in range

    /* default values */
    strncpy(asport, "58046", 6);

    while ((opt = getopt(argc, (char * const*) argv, "p:v")) != -1) {
        if (optarg && optarg[0] == '-') usage();

        switch (opt) {
            /* check flag; parse args
               ifs check if args are valid */
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


void setup_udpserver() {  // sets up udp socket
    fd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_udp == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_udp, 0, sizeof hints_udp);
    hints_udp.ai_family = AF_INET;
    hints_udp.ai_socktype = SOCK_DGRAM;
    hints_udp.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL, asport, &hints_udp, &res_udp);
    if (errcode != 0) { fputs("Error: Could not get AS UDP address. Exiting...\n", stderr); exit(1); }

    n = bind(fd_udp, res_udp->ai_addr, res_udp->ai_addrlen);
    if (n == -1) { fputs("Error: Could not bind socket. Exiting...\n", stderr); exit(1); }
}


void setup_tcpserver() {  // set up tcp socket
    fd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_tcp == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_tcp, 0, sizeof hints_tcp);
    hints_tcp.ai_family = AF_INET;
    hints_tcp.ai_socktype = SOCK_STREAM;
    hints_tcp.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL, asport, &hints_tcp, &res_tcp);
    if (errcode != 0) { fputs("Error: Could not bind FS. Exiting...\n", stderr); exit(1); }

    n = bind(fd_tcp, res_tcp->ai_addr, res_tcp->ai_addrlen);
    if (n == -1) { fputs("Error: Could not bind FS. Exiting...\n", stderr); exit(1); }

    if (listen(fd_tcp, BACKLOG) == -1) { fputs("Error: Could not set up FS. Exiting...\n", stderr); exit(1); }
}


void connect_to_pdserver() {  // connect to pd server
    fd_pdserver = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_pdserver == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_pdserver, 0, sizeof hints_pdserver);
    hints_pdserver.ai_family = AF_INET;
    hints_pdserver.ai_socktype = SOCK_DGRAM;

    errcode = getaddrinfo(pdip, pdport, &hints_pdserver, &res_pdserver);
    if (errcode != 0) { fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1); }

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(fd_pdserver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fputs("Error: Could not set up timeout. Exiting...\n", stderr); exit(1);
    }
}


void disconnect_udpserver() {  // standard udp disconnect
    freeaddrinfo(res_udp);
    close(fd_udp);
}


void disconnect_tcpserver() {  // standard tcp disconnect
    freeaddrinfo(res_tcp);
    close(fd_tcp);
}


void disconnect_from_pdserver() {  // standard udp disconnect
    freeaddrinfo(res_pdserver);
    close(fd_pdserver);
}


void change_to_dusers() {
    DIR *udir;

    udir = opendir("USERS");

    if (!udir) mkdir("USERS", 0755);
    else closedir(udir);

    chdir("USERS");
}


void generate_vc() { vc = rand() % 9000 + 1000; }  // generate a random vc between 1000 and 9999


void generate_tid() { tid = rand() % 9000 + 1000; }  // generate a random tid between 1000 and 9999


void register_user() {  // register a user
    char request[128], response[128];
    char uid[8], pass[10], storedpass[10];
    FILE *passfile, *reg;

    bzero(request, 128);
    strncpy(request, buffer, 127);

    sscanf(request, "%*s %s %s %s %s", uid, pass, pdip, pdport);

    if (verbose_mode) fprintf(stdout, "%s: register (IP: %s | PORT: %d)\n", uid, cip, cport);

    if (strlen(uid) != 5 || !is_only(NUMERIC, uid) || strlen(pass) != 8 ||
        !is_only(ALPHANUMERIC, pass) || !is_only(IP, pdip) || strlen(asport) == 0 ||
        strlen(asport) > 5 || !is_only(NUMERIC, asport) || atoi(asport) > 65535)
        strcpy(response, "RRG NOK\n");

    else {
        /* if not able to mkdir and not dir already exists */
        if ((mkdir(uid, 0755)) != 0 && errno != EEXIST) strcpy(response, "RRG NOK\n");

        chdir(uid);

        strcpy(response, "RRG OK\n");  // default is reg ok

        if ((passfile = fopen("pass.txt", "r"))) {
            fscanf(passfile, "%s", storedpass);

            /* if pass is different, reg nok */
            if (strcmp(pass, storedpass) != 0) strcpy(response, "RRG NOK\n");
            else {
                /* write pd info if reg ok */
                reg = fopen("reg.txt", "w");
                fprintf(reg, "%s %s", pdip, pdport);

                fclose(passfile);
                fclose(reg);
            }

        } else {
            /* write pass info if user does not exist */
            passfile = fopen("pass.txt", "w");
            fprintf(passfile, "%s", pass);

            /* write pd info if reg ok */
            reg = fopen("reg.txt", "w");
            fprintf(reg, "%s %s", pdip, pdport);

            fclose(passfile);
            fclose(reg);
        }

        chdir("..");
    }

    // send response to pd, will timeout if lost
    n = sendto(fd_udp, response, strlen(response), 0, (struct sockaddr*) &addr_udp, addrlen_udp);

}


void unregister_user() {
    char request[128], response[128];
    char uid[8], pass[10], storedpass[10];
    DIR *udir;
    FILE *passfile;

    bzero(request, 128);
    strncpy(request, buffer, 127);

    sscanf(request, "%*s %s %s", uid, pass);

    if (verbose_mode) fprintf(stdout, "%s: unregister (IP: %s | PORT: %d)\n", uid, cip, cport);

    if (strlen(uid) != 5 || !is_only(NUMERIC, uid) || strlen(pass) != 8 ||
        !is_only(ALPHANUMERIC, pass))
        strcpy(response, "RUN NOK\n");

    else {
        udir = opendir(uid);

        if (!udir) strcpy(response, "RUN NOK\n");
        else {
            closedir(udir);

            chdir(uid);

            /* check password */
            if ((passfile = fopen("pass.txt", "r"))) {
                fscanf(passfile, "%s", storedpass);

                /* if pass is different, unr nok */
                if (strcmp(pass, storedpass) != 0) strcpy(response, "RUN NOK\n");

                fclose(passfile);

            } else strcpy(response, "RUN NOK\n");  // user not found

            /* remove pd info */
            if ((remove("reg.txt")) == -1) strcpy(response, "RUN NOK\n");
            else strcpy(response, "RUN OK\n");

            chdir("..");
        }
    }

    // send response to pd, will timeout if lost
    n = sendto(fd_udp, response, strlen(response), 0, (struct sockaddr*) &addr_udp, addrlen_udp);
}


void validate_operation() {
    char request[128], response[128];
    char uid[8], tid[6];
    char storedtid[6], op, fname[26];
    DIR *udir;
    FILE *tidfile;

    bzero(request, 128);
    strncpy(request, buffer, 127);

    sscanf(request, "%*s %s %s", uid, tid);

    if (strlen(uid) != 5 || !is_only(NUMERIC, uid) || strlen(tid) != 4 ||
        !is_only(NUMERIC, tid))
        protocol_error_udp();

    if (verbose_mode) fprintf(stdout, "FS: validate %s (IP: %s | PORT: %d)\n", tid, cip, cport);

    udir = opendir(uid);

    if (!udir) sprintf(response, "CNF %s %s E\n", uid, tid);
    else {
        closedir(udir);

        chdir(uid);

        /* check tid */
        if ((tidfile = fopen("tid.txt", "r"))) {
            fscanf(tidfile, "%s %c %s", storedtid, &op, fname);  // read tid, op and filename (optional)

            /* if pass is different, unr nok */
            if (strcmp(tid, storedtid) != 0) sprintf(response, "CNF %s %s E\n", uid, tid);  // invalid tid
            else {
                if (op == 'R' || op == 'U' || op == 'D') sprintf(response, "CNF %s %s %c %s\n", uid, tid, op, fname);
                else if (op == 'L' || op == 'X') sprintf(response, "CNF %s %s %c\n", uid, tid, op);
                else sprintf(response, "CNF %s %s E\n", uid, tid);  // unknown op
            }
            
            fclose(tidfile);

        } else sprintf(response, "CNF %s %s E\n", uid, tid);  // user not found

        chdir("..");
    }

    // send response to pd, will timeout if lost
    n = sendto(fd_udp, response, strlen(response), 0, (struct sockaddr*) &addr_udp, addrlen_udp);
}


/*int findUser(char *username){
    char direc[32];
    strcpy(direc,"USERS/");
    strcat(direc,username);
    DIR* dir = opendir(username);
    if (!dir){
        closedir(dir);
        return 1;
    }
    return -1;
}

void userNotFound(){
    printf("USER NOT FOUND \n");
}
void userAlreadyRegistered(char* user){
    printf("User %s already registered.\n",user);
}

void wrongPasswd(){
    printf("Wrong Password\n");
}

void wrongVC(){
    printf("Wrong Validation Code\n");
}


void logSuccess(){
    printf("RLO OK\n");
}

void regSuccess(){
    printf("RRG OK\n");
}

char * getcommand(char* string){
    int i = 0;
    char* command = (char*)malloc(sizeof(char)*4);
    for( i = 0; i < 3; ++i){
        command[i]=string[i];
    }
    return command;
}
void sendVC(int vc){
    printf("%d\n",vc);
}

void validateSuccess(){
    printf("RAU\n");
}
void validate(char *command){
    char username[8];
    int localVC, localTID;
    sscanf(command,"AUT %s %d %d",username, &localTID, &localVC);

    if(localVC != vc){
        wrongVC();
    }
    else
        validateSuccess();
}

void require(char *command){
    generate_vc();
    char username[8], op[3], file[32];
    sscanf(command,"REG %s %d %s %s",username, &rid, op, file);
    if(!findUser(username)){
        userNotFound();
        return;
    }
    sendVC(vc);
}

void registerU(char * command){
    int i = 0;
    int len = strlen(command);
    char username[8], passwd[8];
    char dir[32];
    FILE* passwdfile;
    strcpy(dir,"USERS/");
    sscanf(command,"REG %s %s",username,passwd);
    strcat(dir,username);
    if(mkdir(dir, 0755) != 0)
        userAlreadyRegistered(username);
    else{
        strcat(dir,"/passwd.txt");
        passwdfile=fopen(dir,"w");
        fprintf(passwdfile,"%s",passwd);

        regSuccess();
    }
}

void login(char *command) {
    char username[8], passwd[8];
    char dir[32], match[8];
    FILE *passwdfile;
    DIR *directory;
    sscanf(command,"LOG %s %s",username,passwd);
    strcpy(dir,"USERS/");
    strcat(dir,username);
    directory = opendir(dir);
    if(!directory){
        closedir(directory);
        userNotFound();
        return;
    }
    strcat(dir,"/passwd.txt");
    passwdfile = fopen(dir,"r");
    fread(match,9,1, passwdfile);
    if(strcmp(match,passwd) == 0)
        logSuccess();
    else
        wrongPasswd();
}*/


void handle_udp() {
    char rcode[5];
    pid_t pid, ppid;
    struct sigaction act;

    while (1) {
        addrlen_udp = sizeof(addr_udp);
        n = recvfrom(fd_udp, buffer, 128, 0, (struct sockaddr*) &addr_udp, &addrlen_udp);
        if (n == -1) { fputs("Error: Could not get response from server. Try again!\n", stderr); return; }
        else buffer[n] = '\0';

        /* avoid zombie processes */
        act.sa_handler = SIG_IGN;
        if (sigaction(SIGCHLD, &act, NULL) == -1) exit(1);

        ppid = getpid();
        pid = fork();

        if (pid == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        /* fork; process request if child, continue if parent */
        if (pid) continue;
        else {
            signal(SIGTERM, kill_udp);  // kill fs sub-server if SIGTERM received
            setvbuf(stdout, NULL, _IONBF, 0);  // make stdout unbuffered

            n = prctl(PR_SET_PDEATHSIG, SIGTERM);  // send SIGTERM to child if parent exits
            if (n == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

            if (getppid() != ppid) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

            strcpy(cip, inet_ntoa(addr_udp.sin_addr));
            cport = ntohs(addr_udp.sin_port);

            strncpy(rcode, buffer, 4);

            /* perform operation acording to rcode; error if invalid */
            if (strcmp(rcode, "REG ") == 0) register_user();
            else if (strcmp(rcode, "UNR ") == 0) unregister_user();
            else if (strcmp(rcode, "VLD ") == 0) validate_operation();
            else protocol_error_udp();

            disconnect_udpserver();  // disconnects udp sub-server

            exit(0);  // child exits if successful

        }
    }
}


void handle_tcp() {  // receive requests from tcp socket
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
        addrlen_tcp = sizeof(addr_tcp);

        do newfd = accept(fd_tcp, (struct sockaddr*) &addr_tcp, &addrlen_tcp); while (newfd == -1 && errno == EINTR);
        if (newfd == -1) exit(EXIT_FAILURE);

        ppid = getpid();
        pid = fork();

        if (pid == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        /* fork; continue listening if parent, perform operation if child */
        if (pid) {
            do ret = close(newfd); while(ret == -1 && errno == EINTR);  // close new socket
            if (ret == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

        } else {
            signal(SIGTERM, kill_tcp);  // kill fs sub-server if SIGTERM received
            setvbuf(stdout, NULL, _IONBF, 0);  // make stdout unbuffered

            n = prctl(PR_SET_PDEATHSIG, SIGTERM);  // send SIGTERM to child if parent exits
            if (n == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

            if (getppid() != ppid) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

            /* close accept socket; swap new and accept socket */
            close(fd_tcp);
            fd_tcp = newfd;

            /* store client ip and port */
            strcpy(cip, inet_ntoa(addr_tcp.sin_addr));
            cport = ntohs(addr_tcp.sin_port);


            bzero(buffer, 6);
            bzero(rcode, 4);
            if ((n = read(fd_tcp, buffer, 4)) > 0)
                strncpy(rcode, buffer, 4);  // read request code + space

            /* perform operation acording to rcode; error if invalid */
            /*if (strcmp(rcode, "LOG ") == 0) login_user();
            else if (strcmp(rcode, "REQ ") == 0) request_operation();
            else if (strcmp(rcode, "AUT ") == 0) authenticate_operation();
            else protocol_error_tcp();*/

            disconnect_tcpserver();  // disconnects tcp sub-server

            exit(0);  // child exits if successful

        }
    }
}


void setup_server() {
    pid_t pid, ppid;
    struct sigaction act;

    /* avoid zombie processes */
    act.sa_handler = SIG_IGN;
    if (sigaction(SIGCHLD, &act, NULL) == -1) exit(1);

    ppid = getpid();
    pid = fork();

    if (pid == -1) { fputs("Error: Could not fork(). Exiting...\n", stderr); exit(1); }

    /* fork; set up udp if parent, set up tcp if child */
    if (pid) {
        setvbuf(stdout, NULL, _IONBF, 0);  // make stdout unbuffered

        disconnect_tcpserver();  // tcp socket not needed

        handle_udp();

    } else {
        setvbuf(stdout, NULL, _IONBF, 0);  // make stdout unbuffered

        disconnect_udpserver();  // udp socket not needed

        handle_tcp();
    }
}


int main(int argc, char const *argv[]){
    parse_args(argc, argv);

    srand(time(NULL));  // init random generator

    setup_udpserver();
    setup_tcpserver();

    change_to_dusers();

    setup_server();

    disconnect_udpserver();
    disconnect_tcpserver();

    return 0;

}
