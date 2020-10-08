#ifndef PD_H
#define PD_H


void usage();
void parse_args(int argc, char const *argv[]);
void connect_to_as();
void disconnect_from_as();
void register_user(char *uid, char *pass);
void read_commands();

#endif /* PD_H */
