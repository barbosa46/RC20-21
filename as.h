#ifndef AS_H
#define AS_H


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
void kill_tcp(int signum);
void kill_udp(int signum);
void protocol_error_tcp();
void protocol_error_udp();
void syntax_error(int error);
int is_only(int which, char *str);
void parse_args(int argc, char const *argv[]);
void setup_udpserver();
void setup_tcpserver();
void connect_to_pdserver();
void disconnect_udpserver();
void disconnect_tcpserver();
void disconnect_from_pdserver();
void change_to_dusers();
void generate_vc();
void generate_tid();
void register_user();
void unregister_user();
void validate_operation();
void handle_udp();
void handle_tcp();
void setup_server();


#endif /* AS_H */
