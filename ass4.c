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
    if (spawnPid == 0) {
        if (cmd->input_file) {
            int input_fd = open(cmd->input_file, O_RDONLY);
            if (input_fd == -1) {
                fprintf(stderr, "Error: Cannot open input file %s: %s\n", cmd->input_file, strerror(errno));
                exit(1);
            }
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (cmd->output_file) {
            int output_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd == -1) {
                fprintf(stderr, "Error: Cannot open output file %s: %s\n", cmd->output_file, strerror(errno));
                exit(1);
            }
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        execvp(cmd->argv[0], cmd->argv);
        perror(cmd->argv[0]);
        exit(1);
    } else {
        if (cmd->is_bg && allow_bg) {
            if (bg_count < MAX_BG_PROCESSES) {
                bg_processes[bg_count++] = spawnPid;
            } else {
                fprintf(stderr, "Error: Too many background processes.\n");
            }
        } else {
            int childStatus;
            waitpid(spawnPid, &childStatus, 0);
            last_fg_status = childStatus;
        }
    }
}

int main() {
    struct command_line *curr_command;
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigemptyset(&SIGTSTP_action.sa_mask);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = handle_SIGINT;
    sigemptyset(&SIGINT_action.sa_mask);
    sigaction(SIGINT, &SIGINT_action, NULL);
    while (true) {
        curr_command = parse_input();
        if (!curr_command) continue;
        if (curr_command->argc > 0) {
            if (strcmp(curr_command->argv[0], "exit") == 0) {
                free_command(curr_command);
                kill_bg_processes();
                exit(EXIT_SUCCESS);
            } else {
                execute_command(curr_command);
            }
        }
        free_command(curr_command);
    }
    return EXIT_SUCCESS;
}
