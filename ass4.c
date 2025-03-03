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
    printf("\n: ");
    fflush(stdout);
    
    if (!fgets(input, INPUT_LENGTH, stdin)) {
        free(curr_command);
        return NULL;  // Handle EOF case
    }

    // Ignore comments and blank lines
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
        } else if (!strcmp(token, "&") && !allow_bg) {
            curr_command->is_bg = false;
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
            printf("Killed process %d", bg_processes[i]);
            fflush(stdout);
        }
    }
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
            fprintf(stderr, "cd: HOME not set");
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
        printf("exit status %d", WEXITSTATUS(last_fg_status));
    } else if (WIFSIGNALED(last_fg_status)) {
        printf("terminated by signal %d", WTERMSIG(last_fg_status));
    }
    fflush(stdout);
}

void check_background_processes() {
    int childStatus;
    pid_t pid;
    
    while ((pid = waitpid(-1, &childStatus, WNOHANG)) > 0) {
        if (WIFEXITED(childStatus)) {
            printf("\nBackground PID %d terminated. Exit status: %d", pid, WEXITSTATUS(childStatus));
        } else if (WIFSIGNALED(childStatus)) {
            printf("\nBackground PID %d terminated by signal %d", pid, WTERMSIG(childStatus));
        }
        fflush(stdout);
    }
}

void handle_SIGINT(int signo) {
    // do nothing if child process
}

void handle_SIGTSTP(int signo) {
    // Toggle background command behavior
    allow_bg = !allow_bg;

    if (allow_bg) {
        char* message = "Background processes are now allowed.";
        write(STDOUT_FILENO, message, 40);
    } else {
        char* message = "Background processes are now disabled."; 
        write(STDOUT_FILENO, message, 41);
    }
    fflush(stdout);
}

void handle_SIGCHLD(int signo) {
    check_background_processes();  // Check for completed background jobs
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

        // Handle input redirection if specified
        if (cmd->input_file != NULL) {
            int fd = open(cmd->input_file, O_RDONLY);
            if (fd == -1) {
                // Error: file doesn't exist
                perror("Error opening input file");
                exit(1);
            }

            // Redirect stdin to the input file
            if (dup2(fd, STDIN_FILENO) == -1) {
                perror("Error redirecting stdin");
                close(fd);
                exit(1);
            }
            close(fd); // Close file descriptor after duplicating
        }

        // Handle output redirection if specified
        if (cmd->output_file != NULL) {
            int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                perror("Error opening output file");
                exit(1);
            }

            // Redirect stdout to the output file
            if (dup2(fd, STDOUT_FILENO) == -1) {
                perror("Error redirecting stdout");
                close(fd);
                exit(1);
            }
            close(fd); // Close file descriptor after duplicating
        }

        // Execute the command
        execvp(cmd->argv[0], cmd->argv);

        // If execvp fails:
        fprintf(stderr, "%s: command not found", cmd->argv[0]);
        exit(1);
    } else { // Parent process
        if (cmd->is_bg && allow_bg) {
            // Background process allowed
            printf("Background PID: %d", spawnPid);
        } else {
            // No background process, or background not allowed
            int childStatus;
            waitpid(spawnPid, &childStatus, 0);
            last_fg_status = childStatus; // Store for `status` command
        }
    }
}

int main() {
    struct command_line *curr_command;

    // Initialize sigaction for SIGTSTP
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    SIGTSTP_action.sa_flags = 0;
    sigemptyset(&SIGTSTP_action.sa_mask); 
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Initialize sigaction for SIGINT (ignore SIGINT in the parent)
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = handle_SIGINT; 
    sigemptyset(&SIGINT_action.sa_mask);      
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Initialize sigaction for SIGCHLD (child process termination)
    struct sigaction SIGCHLD_action = {0};
    SIGCHLD_action.sa_handler = handle_SIGCHLD; // Register the handler for SIGCHLD
    sigemptyset(&SIGCHLD_action.sa_mask);
    sigaction(SIGCHLD, &SIGCHLD_action, NULL);

    while (true) {
        // Main loop continues without blocking for background processes to complete
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
