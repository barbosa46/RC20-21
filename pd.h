#ifndef PD_H
#define PD_H


#define IP_INVALID 0
#define PORT_INVALID 1
#define USER 2
#define PASS 3

#define UNK 0
#define REG 1
#define UNR 2

#define NUMERIC 0
#define ALPHANUMERIC 1
#define IP 2


void usage();
void syntax_error(int error);
void message_error(int error);
int is_only(int which, char *str);
void parse_args(int argc, char const *argv[]);
void connect_to_as();
void setup_pdserver();
void disconnect_from_as();
void disconnect_pdserver();
void register_user();
void unregister_user();
void read_commands();

#endif /* PD_H */
