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
    struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));

    // Get input
    printf(": ");
    fflush(stdout);
    if (!fgets(input, INPUT_LENGTH, stdin)) {
        free(curr_command);
        exit(0); // Handle EOF (Ctrl+D)
    }

    // Ignore empty lines and comment lines (starting with #)
    if (input[0] == '#' || input[0] == '\n') {
        free(curr_command);
        return NULL;
    }

    // Tokenize the input
    char *token = strtok(input, " \n");
    while (token) {
        if (!strcmp(token, "<")) {
            curr_command->input_file = strdup(strtok(NULL, " \n"));
        } else if (!strcmp(token, ">")) {
            curr_command->output_file = strdup(strtok(NULL, " \n"));
        } else if (!strcmp(token, "&") && allow_bg) {
            curr_command->is_bg = true;
        } else {
            curr_command->argv[curr_command->argc++] = strdup(token);
        }
        token = strtok(NULL, " \n");
    }

    return curr_command;
}

void handle_exit() {
    kill_bg_processes();
    exit(EXIT_SUCCESS);
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

void handle_cd(struct command_line *cmd) {
    char *target_dir;

    // No arguments: change to HOME directory
    if (cmd->argc == 1) {
        target_dir = getenv("HOME");
        if (!target_dir) {
            fprintf(stderr, "cd: HOME not set\n");
            return;
        }
    } else {
        target_dir = cmd->argv[1]; // Use provided directory
    }

    // Attempt to change directory
    if (chdir(target_dir) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target_dir, strerror(errno));
    }
}

void handle_status() {
    if (WIFEXITED(last_fg_status)) {
        printf("exit status %d\n", WEXITSTATUS(last_fg_status));
    } else if (WIFSIGNALED(last_fg_status)) {
        printf("terminated by signal %d\n", WTERMSIG(last_fg_status));
    }
    fflush(stdout);
}

void check_background_processes() {
    int childStatus;
    pid_t pid;
    
    while ((pid = waitpid(-1, &childStatus, WNOHANG)) > 0) {
        if (WIFEXITED(childStatus)) {
            printf("Background PID %d terminated. Exit status: %d\n", pid, WEXITSTATUS(childStatus));
        } else if (WIFSIGNALED(childStatus)) {
            printf("Background PID %d terminated by signal %d\n", pid, WTERMSIG(childStatus));
        }
        fflush(stdout);
    }
}

void handle_SIGINT(int signo) {
    // Do nothing (parent ignores SIGINT)
}

void handle_SIGTSTP(int signo) {
    allow_bg = !allow_bg;

    if (allow_bg) {
        write(STDOUT_FILENO, "\nBackground processes are now allowed.\n: ", 41);
    } else {
        write(STDOUT_FILENO, "\nBackground processes are now disabled. Running jobs must complete in the foreground.\n: ", 93);
    }
}

void execute_command(struct command_line *cmd) {
    pid_t spawnPid = fork();

    if (spawnPid == -1) {
        perror("fork failed");
        exit(1);
    }

    if (spawnPid == 0) { // Child process
        // Handle Input Redirection
        if (cmd->input_file) {
            int input_fd = open(cmd->input_file, O_RDONLY);
            if (input_fd == -1) {
                fprintf(stderr, "input file %s: No such file or directory\n", cmd->input_file);
                exit(1);
            }
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        // Handle Output Redirection
        if (cmd->output_file) {
            int output_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd == -1) {
                fprintf(stderr, "output file %s: Permission denied\n", cmd->output_file);
                exit(1);
            }
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        // Execute command
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
        }
    }
}

int main() {
    struct command_line *curr_command;

    // Initialize sigaction for SIGTSTP
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    SIGTSTP_action.sa_flags = 0;
    SIGTSTP_action.sa_restorer = NULL;        // Not used, initialize to NULL
    sigemptyset(&SIGTSTP_action.sa_mask);     // Initialize the signal mask (empty set)
    SIGTSTP_action.sa_sigaction = NULL;       // Not used, initialize to NULL
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Initialize sigaction for SIGINT (ignore SIGINT in the parent)
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = handle_SIGINT; // Ignore SIGINT in parent
    SIGINT_action.sa_flags = 0;
    SIGINT_action.sa_restorer = NULL;         // Not used, initialize to NULL
    sigemptyset(&SIGINT_action.sa_mask);      // Initialize the signal mask (empty set)
    SIGINT_action.sa_sigaction = NULL;        // Not used, initialize to NULL
    sigaction(SIGINT, &SIGINT_action, NULL);

    while (true) {
        check_background_processes(); // Check for completed background jobs
        curr_command = parse_input();
        if (!curr_command) continue; // Reprompt on blank or comment

        if (curr_command->argc > 0) {
            if (strcmp(curr_command->argv[0], "exit") == 0) {
                free_command(curr_command);
                handle_exit();
            } else if (strcmp(curr_command->argv[0], "cd") == 0) {
                handle_cd(curr_command);
            } else if (strcmp(curr_command->argv[0], "status") == 0) {
                handle_status();
            } else {
                execute_command(curr_command); // Handle external commands
            }
        }

        free_command(curr_command);
    }

    return EXIT_SUCCESS;
}
