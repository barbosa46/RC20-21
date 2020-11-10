#ifndef FS_H
#define FS_H


#define IP_INVALID 0
#define PORT_INVALID 1

#define NUMERIC 0
#define ALPHANUMERIC 1
#define ALPHA 2
#define IP 3
#define OP 4
#define FILENAME 5
#define FILE_CHARS 6

#define BACKLOG 100

void usage();
void kill_fs(int signum);
void protocol_error();
void syntax_error(int error);
int is_only(int which, char *str);
void parse_args(int argc, char const *argv[]);
void connect_to_as();
void setup_fsserver();
void disconnect_from_as();
void disconnect_fs();
void change_to_dusers();
int validate(char *uid, char *tid);
void list_files();
void retrive_file();
void upload_file();
void delete_file();
void remove_user();
void receive_requests();


#endif /* FS_H */
