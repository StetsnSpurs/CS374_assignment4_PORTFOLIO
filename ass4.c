#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>

#define INPUT_LENGTH 2048
#define MAX_ARGS 512
#define MAX_BG_PROCESSES 100

pid_t bg_processes[MAX_BG_PROCESSES];
int bg_count = 0;
int last_fg_status = 0;
static bool allow_bg = true;

struct command_line {
    char *argv[MAX_ARGS + 1];
    int argc;
    char *input_file;
    char *output_file;
    bool is_bg;
};

struct command_line *parse_input() {
    char input[INPUT_LENGTH];
    struct command_line *curr_command = calloc(1, sizeof(struct command_line));

    printf(": ");
    fflush(stdout);
    if (!fgets(input, INPUT_LENGTH, stdin)) {
        free(curr_command);
        exit(0);
    }

    if (input[0] == '#' || input[0] == '\n') {
        free(curr_command);
        return NULL;
    }

    char *token = strtok(input, " \n");
    while (token) {
        if (!strcmp(token, "<")) {
            char *next_token = strtok(NULL, " \n");
            if (next_token) {
                curr_command->input_file = strdup(next_token);
            } else {
                fprintf(stderr, "Error: Missing input file after '<'\n");
                free(curr_command);
                return NULL;
            }
        } else if (!strcmp(token, ">")) {
            char *next_token = strtok(NULL, " \n");
            if (next_token) {
                curr_command->output_file = strdup(next_token);
            } else {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                free(curr_command);
                return NULL;
            }
        } else if (!strcmp(token, "&") && allow_bg) {
            if (!strtok(NULL, " \n")) {
                curr_command->is_bg = true;
            }
        } else {
            curr_command->argv[curr_command->argc++] = strdup(token);
        }
        token = strtok(NULL, " \n");
    }
    return curr_command;
}

void free_command(struct command_line *cmd) {
    if (!cmd) return;
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->input_file);
    free(cmd->output_file);
    free(cmd);
}

void kill_bg_processes() {
    for (int i = 0; i < bg_count; i++) {
        kill(bg_processes[i], SIGTERM);
    }
}

void execute_command(struct command_line *cmd) {
    pid_t spawnPid = fork();

    if (spawnPid == -1) {
        perror("fork failed");
        exit(1);
    }

    if (spawnPid == 0) { // Child process
        struct sigaction SIGINT_default = {0};
        SIGINT_default.sa_handler = SIG_DFL;  // Restore default SIGINT behavior
        sigaction(SIGINT, &SIGINT_default, NULL);

        // Handle input redirection
        if (cmd->input_file) {
            int input_fd = open(cmd->input_file, O_RDONLY);
            if (input_fd == -1) {
                fprintf(stderr, "input file %s: No such file or directory\n", cmd->input_file);
                exit(1);
            }
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        // Handle output redirection
        if (cmd->output_file) {
            int output_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd == -1) {
                fprintf(stderr, "output file %s: Permission denied\n", cmd->output_file);
                exit(1);
            }
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        execvp(cmd->argv[0], cmd->argv);

        // If execvp fails
        fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
        exit(1);
    } else { // Parent process
        if (cmd->is_bg && allow_bg) {
            printf("Background PID: %d\n", spawnPid);
            fflush(stdout);
        } else {
            int childStatus;
            waitpid(spawnPid, &childStatus, 0);
            last_fg_status = childStatus;

            // If killed by SIGINT, print the signal number
            if (WIFSIGNALED(childStatus)) {
                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                fflush(stdout);
            }
        }
    }
}


void handle_SIGTSTP(int signo) {
    allow_bg = !allow_bg;

    char *msg;
    if (allow_bg) {
        msg = "\nBackground processes are now allowed.\n: ";
    } else {
        msg = "\nBackground processes are now disabled. Running jobs must complete in the foreground.\n: ";
    }

    write(STDOUT_FILENO, msg, strlen(msg));
    fflush(stdout);
}

void handle_SIGINT(int signo) {
    write(STDOUT_FILENO, "\n", 1);
    fflush(stdout);
}


int main() {
    struct sigaction SIGTSTP_action = {0}, SIGINT_action = {0};

    // SIGTSTP (Ctrl+Z) setup
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigemptyset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;  // Ensure interrupted system calls restart
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // SIGINT (Ctrl+C) setup
    SIGINT_action.sa_handler = handle_SIGINT;
    sigemptyset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;  // Ensure interrupted system calls restart
    sigaction(SIGINT, &SIGINT_action, NULL);
    
    // Shell loop
    while (true) {
        check_background_processes();
        struct command_line *curr_command = parse_input();
        if (!curr_command) continue;

        if (curr_command->argc > 0) {
            if (strcmp(curr_command->argv[0], "exit") == 0) {
                free_command(curr_command);
                handle_exit();
            } else if (strcmp(curr_command->argv[0], "cd") == 0) {
                handle_cd(curr_command);
            } else if (strcmp(curr_command->argv[0], "status") == 0) {
                handle_status();
            } else {
                execute_command(curr_command);
            }
        }

        free_command(curr_command);
    }

    return EXIT_SUCCESS;
}

