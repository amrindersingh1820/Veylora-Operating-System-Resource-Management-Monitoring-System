#ifndef VOS_CLI_H
#define VOS_CLI_H

/* Runs the interactive terminal UI. Returns exit code (0 = normal). */
int cli_run(void);

/* Handles non-interactive CLI subcommands (e.g. 'resource', 'vm').
 * Returns exit code (0 = success, non-zero = error). */
int cli_handle_command(int argc, char **argv);

#endif /* VOS_CLI_H */
