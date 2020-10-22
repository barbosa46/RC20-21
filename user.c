#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "user.h"

// CHANGE GETADDRINFO BEFORE SUBMITING

int fd_as, fd_fs, errcode;
ssize_t n;
socklen_t addrlen;
struct addrinfo hints_as, *res_as, hints_fs, *res_fs;
struct sockaddr_in addr_as, addr_fs, sa;
char recvline[128];
char buffer[1024];

char asip[18], asport[8];
char fsip[18], fsport[8];

char uid[6], pass[9];
int tid = 9999, rid = 9999;

int is_logged_in = 0;


void usage() {
    fputs("usage: ./user [-n ASIP] [-p ASport] [-m FSIP] [-q FSport]\n", stderr);
    exit(1);
}


void syntax_error(int error) {
    if (error == IP_INVALID) fputs("Error: Invalid IP address. Exiting...\n", stderr);
    else if (error == PORT_INVALID) fputs("Error: Invalid port. Exiting...\n", stderr);
    else if (error == USER) {
        fputs("Error: UID must be 5 characters long and consist only of numbers. Try again!\n", stderr); return; }
    else if (error == PASS) {
        fputs("Error: Password must be 8 characters long and consist only of alphanumeric characters. Try again!\n", stderr); return; }
    else if (error == OP_INVALID) {
        fputs("Error: Operation must be a list (L), retrieve (R), upload (U), delete (D) or remove (X). Try again!\n", stderr); return; }
    else if (error == FILE_INVALID) { fputs("Error: Invalid filename. Try again!\n", stderr); return; }
    else if (error == VC_INVALID) { fputs("Error: Invalid VC. Try again!\n", stderr); return; }
    else fputs("Unknown errror. Exiting...\n", stderr);

    exit(1);
}


void message_error(int error) {
    if (error == LOGIN) fputs("Error: Invalid user ID or password. Try again!\n", stderr);
    else if (error == LOGOUT) { fputs("Error: Logout unsuccessful. Exiting...\n", stderr); exit(1); }
    else if (error == REQ) fputs("Error: Could not send VC to PD. Try again!\n", stderr);
    else if (error == UNK) fputs("Error: Unexpected protocol message. Might not have performed operation\n", stderr);
    else fputs("Unknown errror. Aborting...\n", stderr);
}


int is_only(int which, char *str) {
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


void parse_args(int argc, char const *argv[]) {
    int opt;

    if (argc > 9) usage();

    strncpy(asip, "localhost", 16);
    strncpy(asport, "58046", 6);
    strncpy(fsip, "localhost", 16);
    strncpy(fsport, "59046", 6);

    while ((opt = getopt(argc, (char * const*) argv, "n:p:m:q:")) != -1) {
        if (optarg[0] == '-') usage();

        switch (opt) {
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

            case 'm':
                strncpy(fsip, optarg, 16);
                if (!is_only(IP, fsip)) syntax_error(IP_INVALID);

                break;

            case 'q':
                strncpy(fsport, optarg, 6);
                if (strlen(fsport) == 0 || strlen(fsport) > 5 ||
                    !is_only(NUMERIC, fsport) || atoi(fsport) > 65535)
                    syntax_error(PORT_INVALID);

                break;

            default:
                usage();
        }
    }
}


void connect_to_as() {
    fd_as = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_as == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_as, 0, sizeof hints_as);
    hints_as.ai_family = AF_INET;
    hints_as.ai_socktype = SOCK_STREAM;

    errcode = getaddrinfo("tejo.tecnico.ulisboa.pt", "58011", &hints_as, &res_as);
    if (errcode != 0) { fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1); }
    if (connect(fd_as, res_as->ai_addr, res_as->ai_addrlen) < 0) {
        fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1);
    }
}


void connect_to_fs() {
    fd_fs = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_fs == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_fs, 0, sizeof hints_fs);
    hints_fs.ai_family = AF_INET;
    hints_fs.ai_socktype = SOCK_STREAM;

    errcode = getaddrinfo("tejo.tecnico.ulisboa.pt", "59000", &hints_fs, &res_fs);
    if (errcode != 0) { fputs("Error: Could not connect to FS. Exiting...\n", stderr); exit(1); }
    if (connect(fd_fs, res_fs->ai_addr, res_fs->ai_addrlen) < 0 ) {
        fputs("Error: Could not connect to FS. Exiting...\n", stderr); exit(1);
    }
}


void disconnect_from_as() {
    freeaddrinfo(res_as);
    close(fd_as);
}


void disconnect_from_fs() {
    freeaddrinfo(res_fs);
    close(fd_fs);
}


void generate_rid() { rid = rand() % 9000 + 1000; }


void login(char *l_uid, char *l_pass) {
    char request[20], response[128];
    int len;

    strncpy(uid, l_uid, 5);
    strncpy(pass, l_pass, 8);

    sprintf(request, "LOG %s %s\n", uid, pass);

    len = strlen(request);

    if (write(fd_as, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_as, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    if (strcmp(response, "RLO OK\n") == 0) fputs("You are now logged in!\n", stdout);
    if (strcmp(response, "RLO NOK\n") == 0) { message_error(LOGIN); return; }
    if (strcmp(response, "ERR\n") == 0) { message_error(UNK); return; }

    is_logged_in = 1;
}


void request_operation(char *fop, char *fname) {
    char request[128], response[128];
    int len;

    generate_rid();

    bzero(request, 128);
    if ((strcmp(fop, "R") == 0) || (strcmp(fop, "U") == 0) || (strcmp(fop, "D") == 0))
        sprintf(request, "REQ %s %d %s %s\n", uid, rid, fop, fname);
    else if ((strcmp(fop, "L") == 0) || (strcmp(fop, "X") == 0))
        sprintf(request, "REQ %s %d %s\n", uid, rid, fop);

    len = strlen(request);
    if (len > 42) { fputs("Error: Filename is too long. Try again!\n", stderr); return; }

    if (write(fd_as, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_as, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    if (strcmp(response, "RRQ ELOG\n") == 0) { fputs("Error: No user is logged in. Try again!\n", stderr); return; }
    if (strcmp(response, "RRQ EPD\n") == 0) { message_error(REQ); return; }
    if (strcmp(response, "RRQ EUSER\n") == 0) { message_error(LOGIN); return; }
    if (strcmp(response, "RRQ EFOP\n") == 0) { syntax_error(OP_INVALID); return; }
    if (strcmp(response, "RRQ ERR\n") == 0) { message_error(UNK); return; }
    if (strcmp(response, "ERR\n") == 0) { message_error(UNK); return; }

}


void val_operation(char *vc) {
    char request[128], response[128];
    char pcode[6];
    int len;

    bzero(request, 128);
    sprintf(request, "AUT %s %d %s\n", uid, rid, vc);

    len = strlen(request);

    if (write(fd_as, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_as, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    if (strcmp(response, "RAU 0\n") == 0) { fputs("Error: Authentication failed. Try again!\n", stderr); return; }
    if (strcmp(response, "ERR\n") == 0) { message_error(UNK); return; }

    sscanf(response, "%5s %d\n", pcode, &tid);

    if (strcmp(pcode, "RAU") == 0) fprintf(stdout, "Authenticated! (TID = %d)\n", tid);
    else message_error(UNK);

}


void list_files() {
    char request[128], response[128];
    char pcode[6], fname[26];
    int nfiles, fsize;
    FILE *temp;
    int i = 0, len, offset = 0, first = 1;

    bzero(request, 128);
    sprintf(request, "LST %s %d\n", uid, tid);

    connect_to_fs();

    len = strlen(request);

    if (write(fd_fs, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    temp = fopen("temp.txt", "w+");
    if (temp == NULL) { fputs("Error: Could not create process request. Try again!\n", stderr); return; }

    bzero(buffer, 128);
    while ((n = read(fd_fs, buffer, 127) > 0)) {
        strncpy(response, buffer, 127);

        if(strcmp(response, "ERR\n") == 0) {
            message_error(UNK); fclose(temp); remove("temp.txt"); return; }
        if (strcmp(response, "RLS EOF\n") == 0) {
            fprintf(stdout, "User %s has no files in his directory.\n", uid);
            fclose(temp); remove("temp.txt"); return; }
        if (strcmp(response, "RLS NOK\n") == 0) {
            fprintf(stdout, "Error: User %s does not exist in FS.\n", uid);
            fclose(temp); remove("temp.txt"); return; }
        if (strcmp(response, "RLS INV\n") == 0) {
            fputs("Error: Could not validate operation.\n", stdout);
            fclose(temp); remove("temp.txt"); return; }
        if (strcmp(response, "RLS ERR\n") == 0) {
            fputs("Error: Bad request. Try again!\n", stdout);
            fclose(temp); remove("temp.txt"); return; }

        if (first) {
            sscanf(response, "%s %d %n", pcode, &nfiles, &offset);
            fputs(response + offset, temp);

            first = 0;

        } else fputs(response, temp);

        if (response[strlen(response) - 1] == '\n') break;
    }

    fprintf(stdout, "User %s has %d file(s) in his directory:\n\n", uid, nfiles);

    fseek(temp, 0, SEEK_SET);
    for (i = 1; i <= nfiles; i++) {
        fscanf(temp, "%s %d", fname, &fsize);
        fprintf(stdout, "%d. %s | %d bytes\n", i, fname, fsize);
    } fputs("\n", stdout);

    fclose(temp);
    remove("temp.txt");

    disconnect_from_fs();
}


void retrieve_file(char *fname) {
    char request[128], response[1024];
    char pcode[6], status[6];
    FILE *file;
    int fsize;
    int len, offset = 0, bytes_read = 0, first = 1;

    bzero(request, 128);
    sprintf(request, "RTV %s %d %s\n", uid, tid, fname);

    connect_to_fs();

    len = strlen(request);

    if (write(fd_fs, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    file = fopen(fname, "w");
    if (file == NULL) { fputs("Error: Could not create process request. Try again!\n", stderr); return; }

    bzero(buffer, 1024);
    n = read(fd_fs, buffer, 1023);

    while (n  > 0) {
        bzero(response, 1024);
        memcpy(response, buffer, 1023);

        if(strcmp(response, "ERR\n") == 0) {
            message_error(UNK); fclose(file); remove(fname); return; }
        if (strcmp(response, "RRT EOF\n") == 0) {
            fprintf(stdout, "Error: %s is not avaiable in user directory.\n", fname);
            fclose(file); remove(fname); return; }
        if (strcmp(response, "RRT NOK\n") == 0) {
            fprintf(stdout, "Error: User %s has no content in FS.\n", uid);
            fclose(file); remove(fname); return; }
        if (strcmp(response, "RRT INV\n") == 0) {
            fputs("Error: Could not validate operation.\n", stdout);
            fclose(file); remove(fname); return; }
        if (strcmp(response, "RRT ERR\n") == 0) {
            fputs("Error: Bad request. Try again!\n", stdout);
            fclose(file); remove(fname); return; }

        if (first) {
            sscanf(response, "%s %s %d %n", pcode, status, &fsize, &offset);

            if (strcmp(pcode, "RRT") != 0 || strcmp(status, "OK") != 0) {
                message_error(UNK); fclose(file); remove(fname); return; }

            fwrite(response + offset, 1, n - offset, file);
            bytes_read -= offset;

            first = 0;

        } else fwrite(response, 1, n, file);

        bytes_read += n;
        if (bytes_read >= fsize && response[strlen(response) - 1] == '\n') break;

        bzero(buffer, 1024);
        n = read(fd_fs, buffer, 1023);
    }

    fprintf(stdout, "Retrieved %s\n", fname);

    fseek(file, 0, SEEK_SET);
    ftruncate(fileno(file), fsize);

    fclose(file);

    disconnect_from_fs();
}


void read_commands() {
    char command[128];
    char action[10];
    char arg_1[64], arg_2[64];

    while (1) {
        bzero(arg_1, 64);
        bzero(arg_2, 64);

        fputs("> ", stdout);

        fgets(command, sizeof command, stdin);
        sscanf(command, "%9s %63s %63s\n", action, arg_1, arg_2);

        if ((strcmp(action, "login") == 0)) {
            if (strlen(arg_1) != 5 || !is_only(NUMERIC, arg_1)) { syntax_error(USER); continue; }
            if (strlen(arg_2) != 8 || !is_only(ALPHANUMERIC, arg_2)) { syntax_error(PASS); continue; }

            login(arg_1, arg_2);

        } else if ((strcmp(action, "req") == 0)) {
            if (!is_logged_in) { fputs("Error: No user is logged in. Try again!\n", stderr); continue; }
            if (!is_only(OP, arg_1)) { syntax_error(OP_INVALID); continue; }
            if (strlen(arg_2) != 0 && !is_only(FILENAME, arg_2)) { syntax_error(FILE_INVALID); continue; }

            request_operation(arg_1, arg_2);

        } else if ((strcmp(action, "val")) == 0) {
            if (!is_logged_in) { fputs("Error: No user is logged in. Try again!\n", stderr); continue; }
            if (strlen(arg_1) != 4 || !is_only(NUMERIC, arg_1)) { syntax_error(VC_INVALID); continue; }

            val_operation(arg_1);

        } else if((strcmp(action, "retrieve") == 0) || (strcmp(action, "r") == 0)) {
            if (!is_only(FILENAME, arg_1)) { syntax_error(FILE_INVALID); continue; }

            retrieve_file(arg_1);

        } /* else if((strcmp(action, "upload") == 0) || (strcmp(action, "u") == 0)){
            //Connect to FS for action
            sscanf(command, "%s %s\n", action, filename);
            sprintf(buffer, "UPL %s %s %s\n", uid, verCode, filename);
            //int sendbytes= strlen(buffer);
            connect_to_fs();
            while( (n= read(fd_fs, recvline, 127) > 0)){
              printf("%s", recvline);
            }
            disconnect_from_fs();
         }
         else if((strcmp(action, "delete") == 0) || (strcmp(action, "d") == 0)){
            //Connect to FS for action
            sscanf(command, "%s %s\n", action, filename);
            sprintf(buffer, "DEL %s %s %s\n", uid, verCode, filename);
            int sendbytes= strlen(buffer);
            connect_to_fs();
            if(write(fd_fs, buffer, sendbytes) != sendbytes )
              perror("Couldn't write to socket!");
            while( (n= read(fd_fs, recvline, 127) > 0)){
              printf("%s", recvline);
            }
            disconnect_from_fs();
        } */else if((strcmp(action, "list") == 0) || (strcmp(action, "l") == 0)) {
            list_files();

        } /*else if((strcmp(action, "remove") == 0) || (strcmp(action, "x") == 0)){
            //Connect to FS for action
            sscanf(command, "%s %s\n", action, filename);
            sprintf(buffer, "REM %s %s\n", uid, verCode);
            int sendbytes= strlen(buffer);
            connect_to_fs();
            if(write(fd_fs, buffer, sendbytes) != sendbytes )
              perror("Couldn't write to socket!");
            while( (n= read(fd_fs, recvline, 127) > 0)){
              printf("%s", recvline);
            }
            disconnect_from_fs();


        }*/ else if (strcmp(action, "exit") == 0) return;
        else fputs("Invalid action!\n", stdout);
    }
}


int main (int argc, char const *argv[]){
    parse_args(argc, argv);
    srand(time(NULL));

    connect_to_as();

    read_commands();

    disconnect_from_as();

    return 0;
}
