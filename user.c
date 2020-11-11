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
#include <errno.h>

#include "user.h"


/* Socket vars */
int fd_as, fd_fs, errcode;
ssize_t n, nw;
socklen_t addrlen;
struct addrinfo hints_as, *res_as, hints_fs, *res_fs;
struct sockaddr_in addr_as, addr_fs, sa;
char recvline[128];
char buffer[1024];

/* errno */
extern int errno;

/* Comms info */
char asip[18], asport[8];
char fsip[18], fsport[8];

/* User info */
char uid[6], pass[9];
int tid = 9999, rid = 9999;

/* Login control */
int is_logged_in = 0;


void usage() {
    fputs("usage: ./user [-n ASIP] [-p ASport] [-m FSIP] [-q FSport]\n", stderr);
    exit(1);
}


void syntax_error(int error) {  // error gives error info
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


void message_error(int error) {  // error gives error info
    if (error == LOGIN) fputs("Error: Invalid user ID or password. Try again!\n", stderr);
    else if (error == LOGOUT) { fputs("Error: Logout unsuccessful. Exiting...\n", stderr); exit(1); }
    else if (error == REQ) fputs("Error: Could not send VC to PD. Try again!\n", stderr);
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


void parse_args(int argc, char const *argv[]) { // parse flags and flag args
    int opt;

    if (argc > 9) usage();  // numargs in range

    /* default values */
    strncpy(asip, "127.0.0.1", 16);
    strncpy(asport, "58046", 6);
    strncpy(fsip, "127.0.0.1", 16);
    strncpy(fsport, "59046", 6);

    while ((opt = getopt(argc, (char * const*) argv, "n:p:m:q:")) != -1) {
        if (optarg[0] == '-') usage();

        switch (opt) {
            /* check flag; parse args
               ifs check if args are valid */
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


void connect_to_as() {  // standard tcp connection setup to as
    fd_as = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_as == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_as, 0, sizeof hints_as);
    hints_as.ai_family = AF_INET;
    hints_as.ai_socktype = SOCK_STREAM;

    errcode = getaddrinfo(asip, asport, &hints_as, &res_as);
    if (errcode != 0) { fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1); }
    if (connect(fd_as, res_as->ai_addr, res_as->ai_addrlen) < 0) {
        fputs("Error: Could not connect to AS. Exiting...\n", stderr); exit(1);
    }
}


void connect_to_fs() {  // standard tcp connection setup to fs
    fd_fs = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_fs == -1) { fputs("Error: Could not create socket. Exiting...\n", stderr); exit(1); }

    memset(&hints_fs, 0, sizeof hints_fs);
    hints_fs.ai_family = AF_INET;
    hints_fs.ai_socktype = SOCK_STREAM;

    errcode = getaddrinfo(fsip, fsport, &hints_fs, &res_fs);
    if (errcode != 0) { fputs("Error: Could not connect to FS. Exiting...\n", stderr); exit(1); }
    if (connect(fd_fs, res_fs->ai_addr, res_fs->ai_addrlen) < 0) {
        fputs("Error: Could not connect to FS. Exiting...\n", stderr); exit(1);
    }
}


void disconnect_from_as() {  // standard tcp disconnect from as
    freeaddrinfo(res_as);
    close(fd_as);
}


void disconnect_from_fs() {  // standard tcp disconnect from fs
    freeaddrinfo(res_fs);
    close(fd_fs);
}


void generate_rid() { rid = rand() % 9000 + 1000; }  // generate a random rid between 1000 and 9999


void login(char *l_uid, char *l_pass) {  // login (as command)
    char request[20], response[128];
    int len;

    /* stop uid and pass from overflowing */
    strncpy(uid, l_uid, 5);
    strncpy(pass, l_pass, 8);

    sprintf(request, "LOG %s %s\n", uid, pass);

    len = strlen(request);

    if (write(fd_as, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    /* clear receive buffers, read response */
    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_as, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    /* reply parsing */
    if (strcmp(response, "RLO OK\n") == 0) fputs("You are now logged in!\n", stdout);
    if (strcmp(response, "RLO NOK\n") == 0) { message_error(LOGIN); return; }
    if (strcmp(response, "ERR\n") == 0) { message_error(UNK); return; }

    is_logged_in = 1;   // set login control flag
}


void request_operation(char *fop, char *fname) {  // request operation (as command)
    char request[128], response[128];
    int len;

    generate_rid();  // generate request id

    /* request format depends on operation type */
    bzero(request, 128);
    if ((strcmp(fop, "R") == 0) || (strcmp(fop, "U") == 0) || (strcmp(fop, "D") == 0))
        sprintf(request, "REQ %s %d %s %s\n", uid, rid, fop, fname);
    else if ((strcmp(fop, "L") == 0) || (strcmp(fop, "X") == 0))
        sprintf(request, "REQ %s %d %s\n", uid, rid, fop);

    len = strlen(request);
    if (len > 42) { fputs("Error: Filename is too long. Try again!\n", stderr); return; }

    if (write(fd_as, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    /* clear receive buffers, read response */
    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_as, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    /* reply parsing */
    if (strcmp(response, "RRQ OK\n") == 0) { fputs("VC successfully sent (check PD)\n", stdout); return; }
    if (strcmp(response, "RRQ ELOG\n") == 0) { fputs("Error: No user is logged in. Try again!\n", stderr); return; }
    if (strcmp(response, "RRQ EPD\n") == 0) { message_error(REQ); return; }
    if (strcmp(response, "RRQ EUSER\n") == 0) { message_error(LOGIN); return; }
    if (strcmp(response, "RRQ EFOP\n") == 0) { syntax_error(OP_INVALID); return; }
    if (strcmp(response, "RRQ ERR\n") == 0) { message_error(UNK); return; }
    if (strcmp(response, "ERR\n") == 0) { message_error(UNK); return; }

}


void val_operation(char *vc) {  // validate operation (as command)
    char request[128], response[128];
    char pcode[6];
    int len;

    bzero(request, 128);
    sprintf(request, "AUT %s %d %s\n", uid, rid, vc);

    len = strlen(request);

    if (write(fd_as, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    /* clear receive buffers, read response */
    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_as, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    /* reply parsing */
    if (strcmp(response, "RAU 0\n") == 0) { fputs("Error: Authentication failed. Try again!\n", stderr); return; }
    if (strcmp(response, "ERR\n") == 0) { message_error(UNK); return; }

    sscanf(response, "%5s %d\n", pcode, &tid);

    if (strcmp(pcode, "RAU") == 0) fprintf(stdout, "Authenticated! (TID = %04d)\n", tid);
    else message_error(UNK);

}


void list_files() {  // list files (fs operation)
    char request[128], response[128];
    char pcode[6], fname[26];
    int nfiles, fsize;
    FILE *temp;
    int i = 0, len, offset = 0, first = 1;

    bzero(request, 128);
    sprintf(request, "LST %s %04d\n", uid, tid);

    connect_to_fs();

    len = strlen(request);

    if (write(fd_fs, request, len) != len) {
        fputs("Error: Could not send request. Try again!\n", stderr);
        disconnect_from_fs(); return; }

    /* create a temp file for storing filenames and file sizes */
    temp = fopen("temp.txt", "w+");
    if (temp == NULL) {
        fputs("Error: Could not process request. Try again!\n", stderr);
        disconnect_from_fs(); return; }

    /* clear receive buffers, read response */
    bzero(buffer, 128);
    while ((n = read(fd_fs, buffer, 127) > 0)) {
        strncpy(response, buffer, 127);

        /* reply parsing */
        if (strcmp(response, "ERR\n") == 0) {
            message_error(UNK); fclose(temp); remove("temp.txt"); disconnect_from_fs(); return; }
        if (strcmp(response, "RLS EOF\n") == 0) {
            fprintf(stdout, "User %s has no files in his directory.\n", uid);
            fclose(temp); remove("temp.txt"); disconnect_from_fs(); return; }
        if (strcmp(response, "RLS NOK\n") == 0) {
            fprintf(stdout, "Error: User %s does not exist in FS.\n", uid);
            fclose(temp); remove("temp.txt"); disconnect_from_fs(); return; }
        if (strcmp(response, "RLS INV\n") == 0) {
            fputs("Error: Could not validate operation.\n", stdout);
            fclose(temp); remove("temp.txt"); disconnect_from_fs(); return; }
        if (strcmp(response, "RLS ERR\n") == 0) {
            fputs("Error: Bad request. Try again!\n", stdout);
            fclose(temp); remove("temp.txt"); disconnect_from_fs(); return; }

        if (first) {  // if first loop
            sscanf(response, "%s %d %n", pcode, &nfiles, &offset);
            fputs(response + offset, temp);

            first = 0;

        } else fputs(response, temp);  // if not first loop

        if (response[strlen(response) - 1] == '\n') break;
    }

    fprintf(stdout, "User %s has %d file(s) in his directory:\n\n", uid, nfiles);

    /* read from temp file */
    fseek(temp, 0, SEEK_SET);
    for (i = 1; i <= nfiles; i++) {
        fscanf(temp, "%s %d", fname, &fsize);
        fprintf(stdout, "%d. %s | %d bytes\n", i, fname, fsize);
    } fputs("\n", stdout);

    /* close stream and delete temp file */
    fclose(temp);
    remove("temp.txt");

    disconnect_from_fs();
}


void retrieve_file(char *fname) {  // retrieve file (fs operation)
    char request[128], response[1024];
    char pcode[6], status[6];
    FILE *file;
    int fsize;
    int len, offset = 0, bytes_read = 0, first = 1;

    bzero(request, 128);
    sprintf(request, "RTV %s %04d %s\n", uid, tid, fname);

    connect_to_fs();

    len = strlen(request);

    if (write(fd_fs, request, len) != len) {
        fputs("Error: Could not send request. Try again!\n", stderr);
        disconnect_from_fs(); return; }

    /* create file */
    file = fopen(fname, "w");
    if (file == NULL) {
        fputs("Error: Could not process request. Try again!\n", stderr);
        disconnect_from_fs(); return; }

    /* clear receive buffers, read response */
    bzero(buffer, 1024);
    n = read(fd_fs, buffer, 1023);

    while (n > 0) {
        bzero(response, 1024);
        memcpy(response, buffer, 1023);

        /* reply parsing */
        if (strcmp(response, "ERR\n") == 0) {
            message_error(UNK); fclose(file); remove(fname); disconnect_from_fs(); return; }
        if (strcmp(response, "RRT EOF\n") == 0) {
            fprintf(stdout, "Error: %s is not avaiable in user directory.\n", fname);
            fclose(file); remove(fname); disconnect_from_fs(); return; }
        if (strcmp(response, "RRT NOK\n") == 0) {
            fprintf(stdout, "Error: User %s has no content in FS.\n", uid);
            fclose(file); remove(fname); disconnect_from_fs(); return; }
        if (strcmp(response, "RRT INV\n") == 0) {
            fputs("Error: Could not validate operation.\n", stdout);
            fclose(file); remove(fname); disconnect_from_fs(); return; }
        if (strcmp(response, "RRT ERR\n") == 0) {
            fputs("Error: Bad request. Try again!\n", stdout);
            fclose(file); remove(fname); disconnect_from_fs(); return; }

        if (first) {  // if first loop
            /* extract file info */
            sscanf(response, "%s %s %d %n", pcode, status, &fsize, &offset);

            if (strcmp(pcode, "RRT") != 0 || strcmp(status, "OK") != 0) {
                message_error(UNK); fclose(file); remove(fname); return; }

            /* start writing to file */
            fwrite(response + offset, 1, n - offset, file);
            bytes_read -= offset;

            first = 0;

        } else fwrite(response, 1, n, file); // if not first loop, write to file

        bytes_read += n;  // keep count of bytes read
        if (bytes_read >= fsize) break;

        bzero(buffer, 1024);
        n = read(fd_fs, buffer, 1023);
    }

    fprintf(stdout, "Retrieved %s (stored in current directory)\n", fname);

    fseek(file, 0, SEEK_SET);
    ftruncate(fileno(file), fsize);  // delete last char (\n)

    fclose(file);

    disconnect_from_fs();
}


void upload_file(char *fname) {  // upload file (fs operation)
    char request[1024], response[128];
    FILE *file;
    int fsize;
    int len;

    /* open file to upload in read mode */
    file = fopen(fname, "r");
    if (file == NULL) {
        fputs("Error: File not found. Try again!\n", stderr); return; }

    /* get file size */
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* write file info to socket */
    bzero(request, 1024);
    sprintf(request, "UPL %s %04d %s %d ", uid, tid, fname, fsize);

    connect_to_fs();

    len = strlen(request);
    write(fd_fs, request, len);

    /* while not end of file, write to socket */
    while (!feof(file)) {
        bzero(request, 1024);
        n = fread(request, 1, 1023, file);

        while (n > 0 && errno != ECONNRESET) {  // write response (ignore fs disconnect)
            if ((nw = write(fd_fs, request, n)) <= 0 && errno != ECONNRESET) {
                fputs("Error: Could not send request. Try again!\n", stderr);
                disconnect_from_fs(); return;

            } n -= nw; strcpy(request, &request[nw]);
        }

    } if (errno != ECONNRESET) write(fd_fs, "\n", 1);

    fclose(file);

    /* clear receive buffers, read response */
    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_fs, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    /* reply parsing */
    if (strcmp(response, "ERR\n") == 0) message_error(UNK);
    if (strcmp(response, "RUP OK\n") == 0)
        fprintf(stdout, "Uploaded %s (%d bytes)\n", fname, fsize);
    if (strcmp(response, "RUP NOK\n") == 0)
        fprintf(stdout, "Error: User %s does not exist in FS.\n", uid);
    if (strcmp(response, "RUP DUP\n") == 0)
        fprintf(stdout, "Error: %s already exists in FS.\n", fname);
    if (strcmp(response, "RUP FULL\n") == 0)
        fprintf(stdout, "Error: User %s has exceeded file limit in FS.\n", uid);
    if (strcmp(response, "RUP INV\n") == 0)
        fputs("Error: Could not validate operation.\n", stdout);
    if (strcmp(response, "RUP ERR\n") == 0)
        fputs("Error: Bad request. Try again!\n", stdout);

    disconnect_from_fs();

}


void delete_file(char *fname) {  // delete file (fs operation)
    char request[128], response[128];
    int len;

    bzero(request, 128);
    sprintf(request, "DEL %s %04d %s\n", uid, tid, fname);

    connect_to_fs();

    len = strlen(request);

    if (write(fd_fs, request, len) != len) {
        fputs("Error: Could not send request. Try again!\n", stderr);
        disconnect_from_fs(); return; }

    /* clear receive buffers, read response */
    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_fs, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    /* reply parsing */
    if (strcmp(response, "ERR\n") == 0) {
        message_error(UNK); disconnect_from_fs(); return; }
    if (strcmp(response, "RDL EOF\n") == 0) {
        fprintf(stdout, "Error: %s is not avaiable in user directory.\n", fname);
        disconnect_from_fs(); return; }
    if (strcmp(response, "RDL NOK\n") == 0) {
        fprintf(stdout, "Error: User %s does not exist in FS.\n", uid);
        disconnect_from_fs(); return; }
    if (strcmp(response, "RDL INV\n") == 0) {
        fputs("Error: Could not validate operation.\n", stdout);
        disconnect_from_fs(); return; }
    if (strcmp(response, "RDL ERR\n") == 0) {
        fputs("Error: Bad request. Try again!\n", stdout);
        disconnect_from_fs(); return; }

    if (strcmp(response, "RDL OK\n") == 0) fprintf(stdout, "%s successfully deleted from user directory\n", fname);

    disconnect_from_fs();
}


void remove_user() {  // remove user (fs operation)
    char request[128], response[128];
    int len;

    bzero(request, 128);
    sprintf(request, "REM %s %04d\n", uid, tid);

    connect_to_fs();

    len = strlen(request);

    if (write(fd_fs, request, len) != len) { fputs("Error: Could not send request. Try again!\n", stderr); return; }

    /* clear receive buffers, read response */
    bzero(response, 128);
    bzero(buffer, 128);
    while ((n = read(fd_fs, buffer, 127) > 0)) {
        strncat(response, buffer, 127);
        if (response[strlen(response) - 1] == '\n') break;
    }

    /* reply parsing */
    if (strcmp(response, "ERR\n") == 0) message_error(UNK);
    if (strcmp(response, "RRM OK\n") == 0)
        fprintf(stdout, "User %s was successfully removed from FS\n", uid);
    if (strcmp(response, "RRM NOK\n") == 0)
        fprintf(stdout, "Error: User %s does not exist in FS.\n", uid);
    if (strcmp(response, "RRM INV\n") == 0)
        fputs("Error: Could not validate operation.\n", stdout);
    if (strcmp(response, "RRM ERR\n") == 0)
        fputs("Error: Bad request. Try again!\n", stdout);

    disconnect_from_fs();
}


void read_commands() {  // read commands from stdin
    char command[128];
    char action[10];
    char arg_1[64], arg_2[64];

    while (1) {
        bzero(arg_1, 64);
        bzero(arg_2, 64);

        fputs("> ", stdout);  // for aesthetic purposes

        /* parse command */
        fgets(command, sizeof command, stdin);
        sscanf(command, "%9s %63s %63s\n", action, arg_1, arg_2);

        /* get action from commands
           ifs check if args are valid */
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

        } else if((strcmp(action, "upload") == 0) || (strcmp(action, "u") == 0)){
            if (!is_only(FILENAME, arg_1)) { syntax_error(FILE_INVALID); continue; }

            upload_file(arg_1);

        } else if((strcmp(action, "delete") == 0) || (strcmp(action, "d") == 0)) {
            if (!is_only(FILENAME, arg_1)) { syntax_error(FILE_INVALID); continue; }

            delete_file(arg_1);

        } else if((strcmp(action, "list") == 0) || (strcmp(action, "l") == 0)) {
            list_files();

        } else if((strcmp(action, "remove") == 0) || (strcmp(action, "x") == 0)) {
            remove_user();

        } else if (strcmp(action, "exit") == 0) return;

        else fputs("Invalid action!\n", stdout);  // command not recognized
    }
}


int main (int argc, char const *argv[]){
    parse_args(argc, argv);

    srand(time(NULL));  // init random generator

    connect_to_as();

    read_commands();

    disconnect_from_as();

    return 0;
}
