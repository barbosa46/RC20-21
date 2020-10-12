#ifndef PD_H
#define PD_H


void usage();
void parse_args(int argc, char const *argv[]);
void connect_to_as();
void setup_pdserver();
void disconnect_from_as();
void disconnect_pdserver();
void register_user();
void unregister_user();
void read_commands();

#endif /* PD_H */
