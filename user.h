#ifndef USER_H
#define USER_H


#define IP_INVALID 0
#define PORT_INVALID 1
#define USER 2
#define PASS 3
#define OP_INVALID 4
#define FILE_INVALID 5
#define VC_INVALID 6

#define UNK 0
#define LOGIN 1
#define LOGOUT 2
#define REQ 3

#define NUMERIC 0
#define ALPHANUMERIC 1
#define ALPHA 2
#define IP 3
#define OP 4
#define FILE 5
#define FILE_CHARS 6


void usage();
void syntax_error(int error);
void message_error(int error);
void parse_args(int argc, char const *argv[]);
void connect_to_as();
void connect_to_fs();
void disconnect_from_as();
void disconnect_from_fs();
void generate_rid();
void login(char *l_uid, char *l_pass);
void request_operation(char *fop, char *fname);
void val_operation(char *vc);
void read_commands();


#endif /* USER_H */
