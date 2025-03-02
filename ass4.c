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
    fgets(input, INPUT_LENGTH, stdin);

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

void kill_bg_processes() {
    for (int i = 0; i < bg_count; i++) {
        if (kill(bg_processes[i], SIGTERM) == 0) {
            printf("Killed process %d\n", bg_processes[i]);
            fflush(stdout);
        }
    }
}

void handle_exit() {
    kill_bg_processes();
    exit(EXIT_SUCCESS);
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

void execute_command(struct command_line *cmd) {
    pid_t spawnPid = fork();

    if (spawnPid == -1) {
        perror("fork failed");
        exit(1);
    }

    if (spawnPid == 0) { // Child process
        // Initialize sigaction structures for SIGINT and SIGTSTP
        struct sigaction SIGINT_action = {0};
        struct sigaction SIGTSTP_action = {0};

        // Initialize SIGINT action (parent ignores SIGINT)
        SIGINT_action.sa_handler = handle_SIGINT;  // Ignore SIGINT in parent
        SIGINT_action.sa_flags = 0;                 // No special flags
        SIGINT_action.sa_restorer = NULL;           // Not used, initialize to NULL
        sigemptyset(&SIGINT_action.sa_mask);        // Initialize the signal mask (empty set)
        SIGINT_action.sa_sigaction = NULL;          // Not used, initialize to NULL
        sigaction(SIGINT, &SIGINT_action, NULL);

        // Initialize SIGTSTP action (ignore SIGTSTP)
        SIGTSTP_action.sa_handler = handle_SIGTSTP; // Handle SIGTSTP toggle
        SIGTSTP_action.sa_flags = 0;                // No special flags
        SIGTSTP_action.sa_restorer = NULL;          // Not used, initialize to NULL
        sigemptyset(&SIGTSTP_action.sa_mask);       // Initialize the signal mask (empty set)
        SIGTSTP_action.sa_sigaction = NULL;         // Not used, initialize to NULL
        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

        // Execute the command
        execvp(cmd->argv[0], cmd->argv);

        // If execvp fails:
        fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
        exit(1);
    } else { // Parent process
        if (cmd->is_bg && allow_bg) {
            // Background process allowed
            printf("Background PID: %d\n", spawnPid);
        } else {
            // No background process, or background not allowed
            int childStatus;
            waitpid(spawnPid, &childStatus, 0);
            last_fg_status = childStatus; // Store for `status` command
        }
    }
}

void handle_SIGINT(int signo) {
    // Do nothing (parent ignores SIGINT)
}

void handle_SIGTSTP(int signo) {
    // Toggle background command behavior
    allow_bg = !allow_bg;

    if (allow_bg) {
        printf("\nBackground processes are now allowed.\n");
    } else {
        printf("\nBackground processes are now disabled.\n");
    }
    fflush(stdout);
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
