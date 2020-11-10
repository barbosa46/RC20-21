#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/stat.h>
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
char buffer[1024];

/* errno */
extern int errno;

/* Comms info */
char asip[18], asport[8];
char fsport[8];

/* Client info */
char uip[18];
int uport;

/* Validated operations */
char op, fname[26];

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

    bzero(response, 5);
    strcpy(response, "ERR\n");

    n = 5;

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }

    disconnect_fs();

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

    if (argc > 8) usage();  // numargs in range

    /* default values */
    strncpy(asip, "localhost", 16);
    strncpy(asport, "58046", 6);
    strncpy(fsport, "59046", 6);

    while ((opt = getopt(argc, (char * const*) argv, "q:n:p:v")) != -1) {
        if (optarg && optarg[0] == '-') usage();

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

    errcode = getaddrinfo(asip, asport, &hints_as, &res_as);
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


void change_to_dusers() {
    DIR *udir;

    udir = opendir("USERS");

    if (!udir) mkdir("USERS", 0755);
    else closedir(udir);

    chdir("USERS");
}


int validate(char *uid, char *tid) {  // validate operation with as
    char request[20], response[128];
    char pcode[5];
    char vuid[6], vtid[5];

    sprintf(request, "VLD %s %s\n", uid, tid);

    n = strlen(request);

    /* send request */
    n = sendto(fd_as, request, strlen(request), 0, res_as->ai_addr, res_as->ai_addrlen);
    if (n == -1) protocol_error();

    /* clear receive buffer, read response */
    bzero(response, 128);
    addrlen_as = sizeof(addr_as);
    n = recvfrom(fd_as, response, 128, 0, (struct sockaddr*) &addr_as, &addrlen_as);
    if (n == -1) protocol_error();

    /* check as response */
    if (strcmp(response, "ERR\n") == 0) protocol_error();
    else {
        sscanf(response, "%5s %s %s %c", pcode, vuid, vtid, &op);

        /* reply parsing */
        if (strcmp(pcode, "CNF") == 0 && strcmp(uid, vuid) == 0 && strcmp(tid, vtid) == 0) {
            if (op == 'R' || op == 'U' || op == 'D') sscanf(response, "%*s %*s %*s %*c %s", fname);
            else if (op == 'L' || op == 'X');
            else if (op == 'E') return 0;
            else protocol_error();

        } else protocol_error();
    }

    return 1;

}


void list_files() {  // list user files
    char request[128], response[1024];
    char ruid[6], rtid[5];
    char finfo[50];
    DIR *udir;
    FILE *ufile;
    struct dirent *udirent;
    int nfiles = 0, fsize;

    bzero(buffer, 128);
    if ((n = read(fd_fs, buffer, 127)) > 0)
        strncpy(request, buffer, 127);  // read request

    sscanf(request, "%s %s", ruid, rtid);

    bzero(response, 1024);
    if (strlen(ruid) != 5 || !is_only(NUMERIC, ruid) ||
        strlen(rtid) != 4 || !is_only(NUMERIC, rtid))
        strcpy(response, "RLS ERR\n");  // format error

    else {
        if (!validate(ruid, rtid)) strcpy(response, "RLS INV\n");  // validation error
        else {
            udir = opendir(ruid);  // open user directory

            if (verbose_mode) fprintf(stdout, "%s: list (IP: %s | PORT: %d)\n", ruid, uip, uport);

            if (udir) {
                bzero(buffer, 1024);
                while ((udirent = readdir(udir)) != NULL) {
                    /* ignore current and previous directory */
                    if (strcmp(udirent->d_name, ".") == 0 ||
                        strcmp(udirent->d_name, "..") == 0)
                        continue;

                    chdir(ruid);  // change to user dir

                    /* open file to get size */
                    ufile = fopen(udirent->d_name, "r");

                    /* get file size */
                    if (ufile == NULL) fsize = 0;
                    else {
                        fseek(ufile, 0, SEEK_END);
                        fsize = ftell(ufile);
                        fseek(ufile, 0, SEEK_SET);

                        fclose(ufile);
                    }

                    chdir(".."); // go back

                    bzero(finfo, 50);
                    sprintf(finfo, " %.24s %d", udirent->d_name, fsize);

                    strcat(buffer, finfo);
                    nfiles++;
                }

                closedir(udir);

                if (nfiles > 0) {
                    sprintf(response, "RLS %d", nfiles);
                    strcat(response, buffer);
                    strcat(response, "\n");

                } else strcpy(response, "RLS EOF\n");  // empty user directory
            } else strcpy(response, "RLS EOF\n");  // user not in fs
        }
    }

    n = strlen(response);

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }
}


void retrive_file() {  // retrieve file from fs
    char request[128], response[1024];
    char ruid[6], rtid[5], rfname[26];
    DIR *udir;
    FILE *toretrieve;
    int fsize;

    bzero(buffer, 128);
    if ((n = read(fd_fs, buffer, 127)) > 0)
        strncpy(request, buffer, 127);  // read request

    sscanf(request, "%s %s %s", ruid, rtid, rfname);

    bzero(response, 128);
    if (strlen(ruid) != 5 || !is_only(NUMERIC, ruid) ||
        strlen(rtid) != 4 || !is_only(NUMERIC, rtid) ||
        !is_only(FILENAME, rfname))
        strcpy(response, "RRT ERR\n");  // format error

    else {
        if (!validate(ruid, rtid)) strcpy(response, "RRT INV\n");  // validation error
        else if (strcmp(fname, rfname) != 0) strcpy(response, "RRT INV\n");  // validation error
        else {
            udir = opendir(ruid);  // open user directory

            if (!udir) strcpy(response, "RRT NOK\n"); // user not in fs
            else {
                closedir(udir);

                if (verbose_mode) fprintf(stdout, "%s: retrieve: %s (IP: %s | PORT: %d)\n", ruid, fname, uip, uport);

                chdir(ruid);  // change to user dir

                toretrieve = fopen(fname, "r");
                if (toretrieve == NULL) strcpy(response, "RRT EOF\n"); // file not found
                else {
                    /* get file size */
                    fseek(toretrieve, 0, SEEK_END);
                    fsize = ftell(toretrieve);
                    fseek(toretrieve, 0, SEEK_SET);

                    sprintf(response, "RRT OK %d ", fsize);  // successful retrieve

                    n = strlen(response);

                    while (n > 0) {  // write response
                        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
                        n -= nw; strcpy(response, &response[nw]);
                    }

                    /* write data until eof */
                    while (!feof(toretrieve)) {
                        bzero(response, 1024);
                        n = fread(response, 1, 1023, toretrieve);

                        while (n > 0) {  // write response
                            if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
                            n -= nw; strcpy(response, &response[nw]);
                        }

                    } write(fd_fs, "\n", 1); chdir(".."); return;
                } chdir(".."); // go back
            }
        }
    }

    n = strlen(response);

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }
}


void upload_file() {
    char request[1024], response[128];
    char ruid[6], rtid[5], rfname[26];
    DIR *udir;
    FILE *uploaded;
    struct dirent *udirent;
    int nfiles = 0, fsize = -1;
    int offset = 0, bytes_read = 0, first = 1, nmem;

    bzero(buffer, 1024);
    bzero(request, 1024);
    if ((n = read(fd_fs, buffer, 1023)) > 0)
        memcpy(request, buffer, 1023);  // read request

    nmem = n;  // store n

    sscanf(request, "%s %s %s %d %n", ruid, rtid, rfname, &fsize, &offset);

    bzero(response, 128);
    if (strlen(ruid) != 5 || !is_only(NUMERIC, ruid) ||
        strlen(rtid) != 4 || !is_only(NUMERIC, rtid) ||
        !is_only(FILENAME, rfname) || fsize == -1)
        strcpy(response, "RUP ERR\n");  // format error

    else {
        if (!validate(ruid, rtid)) strcpy(response, "RUP INV\n");  // validation error
        else if (strcmp(fname, rfname) != 0) strcpy(response, "RUP INV\n");  // validation error
        else {
            udir = opendir(ruid);  // open user directory

            if (!udir) { mkdir(ruid, 0777); udir = opendir(ruid); } // user not in fs

            if (verbose_mode) fprintf(stdout, "%s: upload: %s (IP: %s | PORT: %d)\n", ruid, fname, uip, uport);

            while ((udirent = readdir(udir)) != NULL) {
                /* ignore current and previous directory */
                if (strcmp(udirent->d_name, ".") == 0 ||
                    strcmp(udirent->d_name, "..") == 0)
                    continue;

                // file already exists
                if (strcmp(udirent->d_name, fname) == 0) { strcpy(response, "RUP DUP\n"); break; }

                nfiles++;
            }

            if (nfiles >= 15) strcpy(response, "RUP FULL\n");  // user directory already at max capacity
            else if (strcmp(response, "RUP DUP\n") != 0) {  // file can be created
                chdir(ruid);

                /* create file */
                uploaded = fopen(fname, "w");
                if (uploaded == NULL) protocol_error();

                n = nmem;  // retrieve n value

                while (n > 0) {
                    if (first) {
                        /* start writing to file */
                        fwrite(request + offset, 1, n - offset, uploaded);
                        bytes_read -= offset;

                        first = 0;

                    } else {
                        bzero(request, 1024);
                        memcpy(request, buffer, 1023);

                        fwrite(request, 1, n, uploaded); // if not first loop, write to file
                    }

                    bytes_read += n;  // keep count of bytes read
                    if (bytes_read >= fsize) break;

                    bzero(buffer, 1024);
                    n = read(fd_fs, buffer, 1023);
                }

                fseek(uploaded, 0, SEEK_SET);
                ftruncate(fileno(uploaded), fsize);  // delete last char (\n)

                fclose(uploaded);

                chdir("..");

                strcpy(response, "RUP OK\n");
            }
        }
    }

    n = strlen(response);

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }
}


void delete_file() {  // delete file in fs
    char request[128], response[128];
    char ruid[6], rtid[5], rfname[26];
    DIR *udir;
    FILE *todelete;

    bzero(buffer, 128);
    if ((n = read(fd_fs, buffer, 127)) > 0)
        strncpy(request, buffer, 127);  // read request

    sscanf(request, "%s %s %s", ruid, rtid, rfname);

    bzero(response, 128);
    if (strlen(ruid) != 5 || !is_only(NUMERIC, ruid) ||
        strlen(rtid) != 4 || !is_only(NUMERIC, rtid) ||
        !is_only(FILENAME, rfname))
        strcpy(response, "RDL ERR\n");  // format error

    else {
        if (!validate(ruid, rtid)) strcpy(response, "RDL INV\n");  // validation error
        else if (strcmp(fname, rfname) != 0) strcpy(response, "RDL INV\n");  // validation error
        else {
            udir = opendir(ruid);  // open user directory

            if (!udir) strcpy(response, "RDL NOK\n"); // user not in fs
            else {
                closedir(udir);

                if (verbose_mode) fprintf(stdout, "%s: delete: %s (IP: %s | PORT: %d)\n", ruid, fname, uip, uport);

                chdir(ruid);  // change to user dir

                todelete = fopen(fname, "r");
                if (todelete == NULL) strcpy(response, "RDL EOF\n"); // file not found
                else {
                    fclose(todelete);
                    remove(fname);

                    strcpy(response, "RDL OK\n");  // successful delete
                }

                chdir(".."); // go back
            }
        }
    }

    n = strlen(response);

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }
}


void remove_user() {  // remove user from fs
    char request[128], response[128];
    char ruid[6], rtid[5];
    DIR *udir;
    struct dirent *udirent;

    bzero(buffer, 128);
    if ((n = read(fd_fs, buffer, 127)) > 0)
        strncpy(request, buffer, 127);  // read request

    sscanf(request, "%s %s", ruid, rtid);

    bzero(response, 128);
    if (strlen(ruid) != 5 || !is_only(NUMERIC, ruid) ||
        strlen(rtid) != 4 || !is_only(NUMERIC, rtid))
        strcpy(response, "RRM ERR\n");  // format error

    else {
        if (!validate(ruid, rtid)) strcpy(response, "RRM INV\n");  // validation error
        else {
            udir = opendir(ruid);  // open user directory

            if (!udir) strcpy(response, "RRM NOK\n"); // user not in fs
            else {
                if (verbose_mode) fprintf(stdout, "%s: remove (IP: %s | PORT: %d)\n", ruid, uip, uport);

                while ((udirent = readdir(udir)) != NULL) {
                    /* ignore current and previous directory */
                    if (strcmp(udirent->d_name, ".") == 0 ||
                        strcmp(udirent->d_name, "..") == 0)
                        continue;

                    chdir(ruid);  // change to user dir
                    remove(udirent->d_name);
                    chdir(".."); // go back

                } closedir(udir); remove(ruid); strcpy(response, "RRM OK\n");
            }
        }
    }

    n = strlen(response);

    while (n > 0) {  // write response
        if ((nw = write(fd_fs, response, n)) <= 0) exit(1);
        n -= nw; strcpy(response, &response[nw]);
    }
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

            /* store client ip and port */
            strcpy(uip, inet_ntoa(addr_fs.sin_addr));
            uport = ntohs(addr_fs.sin_port);

            bzero(buffer, 6);
            bzero(rcode, 4);
            if ((n = read(fd_fs, buffer, 4)) > 0)
                strncpy(rcode, buffer, 4);  // read request code + space

            /* perform operation acording to rcode; error if invalid */
            if (strcmp(rcode, "LST ") == 0) list_files();
            else if (strcmp(rcode, "RTV ") == 0) retrive_file();
            else if (strcmp(rcode, "UPL ") == 0) upload_file();
            else if (strcmp(rcode, "DEL ") == 0) delete_file();
            else if (strcmp(rcode, "REM ") == 0) remove_user();
            else protocol_error();

            disconnect_fs();  // disconnects fs sub-server

            exit(0);  // child exits if successful

        }
    }
}


int main(int argc, char const *argv[]){
    parse_args(argc, argv);

    setup_fsserver();
    connect_to_as();

    change_to_dusers();

    receive_requests();

    disconnect_from_as();
    disconnect_fs();

    return 0;

}
