/*
 *  This program implements the following functionality:
 *  - Provide a prompt for running commands
 *  - Handle blank lines and comments, which are lines beginning with the # character
 *  - Provide expansion for the variable $$ with process ID
 *  - Execute 3 commands "exit", "cd", and "status" via code built into the shell
 *  - Execute other commands by creating new processes using a function from the exec family of functions
 *  - Support input and output redirection
 *  - Support running commands in foreground and background processes
 *  - Implement custom handlers for 2 signals, SIGINT and SIGTSTP
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#define MAX_PROCESSES 100

/*
 *  struct to hold command line arguments.
 */
struct commandLine {
    // Array of pointers for arguments
    // args[0] contains command, and the rest its arguments, terminated by NULL
    char *args[512];
    // Number of actual arguments
    int argsNum;
    // Input file path
    char *inputFile;
    // Output file path
    char *outputFile;
    // Ampersand character
    bool background;
};

/*
 *  Function declarations.
 */
void SIGTSTPHandler(int sig);
void SIGCHLDHandler(int sig);
void createInputFD();
void createOutputFD();
char *expandToken(char *inputToken);
struct commandLine *printShell();
void printExitStatus(int status);
void printSignalStatus(int status);
int changeWD();
void freeCommandLine();
void exitShell();
void executeCommandLine();

/*
 *  Global variables.
 */
int runShell = 1;
pid_t childPID;
int childStatus = 0;
bool foregroundModeOnly = false;
pid_t backgroundPIDs[MAX_PROCESSES] = {0};
struct commandLine *inputCommand;
struct sigaction SIGINTAction = {0};
struct sigaction SIGTSTPAction = {0};
struct sigaction SIGCHLDAction = {0};

/*
 *  A small shell program for CS344 Assignment 3.
 *  Compile the program as follows: gcc --std=gnu99 -o smallsh main.c
 *  Run the program as follows: ./smallsh
 */

int main() {
    // Print the shell prompt on a loop until runShell is set to 0
    // Intentional infinite loop since the program can be exited inside the shell with "exit" command
    do {
        // Print the prompt and save command
        inputCommand = printShell();
        executeCommandLine();
        freeCommandLine();
    } while(runShell);

    return 0;
}

/*
 *  Execute the command based on command values
 */
void executeCommandLine() {
    // SIGINT signal handling
    SIGINTAction.sa_handler = SIG_IGN;
    sigfillset(&SIGINTAction.sa_mask);
    SIGINTAction.sa_flags = 0;
    sigaction(SIGINT, &SIGINTAction, NULL);

    // SIGTSTP signal handling
    SIGTSTPAction.sa_handler = SIGTSTPHandler;
    sigfillset(&SIGTSTPAction.sa_mask);
    SIGTSTPAction.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTPAction, NULL);

    // SIGCHLD signal handling
    SIGCHLDAction.sa_handler = SIGCHLDHandler;
    sigfillset(&SIGCHLDAction.sa_mask);
    SIGCHLDAction.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &SIGCHLDAction, NULL);

    // Execute command only there is one to execute
    if(inputCommand->args[0] != NULL && *inputCommand->args[0] != '\n') {
        if(strncmp(inputCommand->args[0], "exit", 4) == 0) {
            // Execute built-in command "exit"
            exitShell();
        } else if(strncmp(inputCommand->args[0], "cd", 2) == 0) {
            // Execute built-in command "cd"
            changeWD(inputCommand);
        } else if(strncmp(inputCommand->args[0], "status", 6) == 0) {
            // Execute built-in command "status"
            if(WIFEXITED(childStatus)) {
                printExitStatus(childStatus);
            } else {
                printSignalStatus(childStatus);
            }
        } else {
            // Fork child process to run non-builtin command
            childPID = fork();
            switch(childPID) {
                case -1:
                    perror("fork() failed\n");
                    fflush(stdout);
                    break;
                case 0:
                    // Child to execute code below
                    // If user doesn't redirect the standard input
                    if(inputCommand->background) {
                        if(inputCommand->inputFile != NULL) {
                            inputCommand->inputFile = "/dev/null";
                            createInputFD(inputCommand);
                        }
                        // If user doesn't redirect the standard output
                        if(inputCommand->outputFile != NULL) {
                            inputCommand->outputFile = "/dev/null";
                            createOutputFD(inputCommand);
                        }
                    }

                    // Set signal handlers
                    if(inputCommand->background == false) {
                        // Foreground process must terminate via the handler for SIGINT
                        SIGINTAction.sa_handler = SIG_DFL;
                        sigaction(SIGINT, &SIGINTAction, NULL);

                        // Foreground process must ignore SIGTSTP
                        SIGTSTPAction.sa_handler = SIG_IGN;
                        sigaction(SIGTSTP, &SIGTSTPAction, NULL);
                    } else if(inputCommand->background == true) {
                        // Background process must ignore SIGINT
                        SIGINTAction.sa_handler = SIG_IGN;
                        sigaction(SIGINT, &SIGINTAction, NULL);

                        // Background process must ignore SIGTSTP
                        SIGTSTPAction.sa_handler = SIG_IGN;
                        sigaction(SIGTSTP, &SIGTSTPAction, NULL);
                    }

                    // Process input file, if any
                    if(inputCommand->inputFile != NULL) {
                        createInputFD(inputCommand);
                    }
                    // Process output file, if any
                    if(inputCommand->outputFile != NULL) {
                        createOutputFD(inputCommand);
                    }

                    // Save child's PID
                    childPID = getpid();

                    // Look for command in PATH variable
                    execvp(inputCommand->args[0], inputCommand->args);
                    perror("execvp() failed, command could not be executed\n");
                    fflush(stdout);

                    // If command fails, print error message and set exit(1)
                    exit(1);
                default:
                    // Save background child's process ID
                    // Parent to execute code here
                    // If foreground mode only is on, then all processes run in the foreground
                    if(foregroundModeOnly == true) {
                        // Run as a foreground process, i.e., wait for it to finish
                        waitpid(childPID, &childStatus, 0);
                        // If it's been killed by a signal, print the signal number
                        if(WIFSIGNALED(childStatus)){
                            printSignalStatus(childStatus);
                        }
                    } else {
                        // If background mode with '&' is respected, then check for that property
                        // If not a background process
                        if(inputCommand->background == false) {
                            // Run as a foreground process, i.e., wait for it to finish
                            waitpid(childPID, &childStatus, 0);
                            // If it's been killed by a signal, print the signal number
                            if(WIFSIGNALED(childStatus)){
                                printSignalStatus(childStatus);
                            }
                        } else {
                            // Run as a background process
                            // Save PID of background processes in array
                            for(int i = 0; i < MAX_PROCESSES; i++) {
                                if(backgroundPIDs[i] == 0) {
                                    backgroundPIDs[i] = childPID;
                                    break;
                                }
                            }

                            // Must print out background child process ID
                            printf("Background child PID %d is starting\n", childPID);
                            fflush(stdout);
                        }
                    }
            }
        }
    }
}

/*
 *  Print the shell prompt and parse user command.
 */
struct commandLine *printShell() {
    char *line = NULL;
    size_t len = 0;
    char *saveptr;
    char *token = NULL;

    // Set up command struct
    struct commandLine *command = malloc(sizeof(struct commandLine));
    command->inputFile = NULL;
    command->outputFile = NULL;
    command->background = false;
    int tokenNum = 0;

    // Clear any existing errors from previous run
    clearerr(stdin);
    // Print shell prompt
    write(STDOUT_FILENO, ": ", 2);
    fflush(stdout);
    // Get command line
    getline(&line, &len, stdin);

    line[strlen(line)-1] = '\0';

    // Get the first token if command is not empty
    if(strlen(line) != 0) {
        token = strtok_r(line, " ", &saveptr);
    }

    // Skip comment and empty line
    if(token != NULL && *token != '#') {
        // Save token to appropriate command value
        while(token != NULL) {
            switch(*token) {
                // Process input file
                case '<':
                    // Skip "<" to next token
                    token = strtok_r(NULL, " ", &saveptr);
                    token = expandToken(token);
                    // Save token
                    command->inputFile = calloc(strlen(token) + 1, sizeof(char));
                    strcpy(command->inputFile, token);
                    break;
                    // Process output file
                case '>':
                    // Skip ">" to next token
                    token = strtok_r(NULL, " ", &saveptr);
                    token = expandToken(token);
                    // Save token
                    command->outputFile = calloc(strlen(token) + 1, sizeof(char));
                    strcpy(command->outputFile, token);
                    break;
                    // Process background indicator
                case '&':
                    // Set flag based on token value
                    command->background = true;
                    break;
                default:
                    token = expandToken(token);
                    // Save token to command args array
                    command->args[tokenNum] = calloc(strlen(token) + 1, sizeof(char));
                    strcpy(command->args[tokenNum], token);
                    tokenNum++;
            }
            if(token != NULL) {
                if(token[0] != '&') {
                    free(token);
                }
            }
            // Get next token
            token = strtok_r(NULL, " ", &saveptr);
        }
        // Set last arg to NULL to signal end of args array
        command->args[tokenNum] = NULL;
        // Save number of actual arguments
        command->argsNum = tokenNum;
    } else {
        // If command is # or blank
        command->args[0] = NULL;
    }

    // Free memory
    if(line != NULL) {
        free(line);
    }
    return command;
}

/*
 *  Expand token and replace variable '$$' with process ID
 */
char *expandToken(char *inputToken) {
    // Set output token size to be 4x len because pid_max is 7 digits
    // So, theoretically $$ could expand to ########
    char *outputToken = calloc(4 * strlen(inputToken), sizeof(char));

    // Build output token
    int i = 0, j = 0;
    while(inputToken[i] != '\0') {
        if(inputToken[i] == '$' && inputToken[i+1] == '$') {
            // Get process ID
            int pid = getpid();
            // Convert process ID to string
            char pidString[8] = {'\0'};
            sprintf(pidString, "%d", pid);
            // Concatenate process ID string to output token
            strcat(outputToken, pidString);
            // Advance pointers
            i += 2;
            j += (int)strlen(pidString);
        } else {
            outputToken[j] = inputToken[i];
            i++;
            j++;
        }
    }

    // Free passed in input token
    if(inputToken[i] != '\0') {
        free(inputToken);
    }

    return outputToken;
}

/*
 *  Print exit status or terminating signal of last foreground process.
 */
void printExitStatus(int status) {
    // Print status if exited normally
    if(WIFEXITED(status)) {
        printf("Exit status %d\n", WEXITSTATUS(status));
        fflush(stdout);
    }
}

/*
 *  Print terminating signal of last foreground process.
 */
void printSignalStatus(int status) {
    // Print status if terminated by signal
    if(WIFSIGNALED(status)) {
        printf("Terminated by signal %d\n", WTERMSIG(status));
        fflush(stdout);
    }
}

/*
 *  SIGTSTP handler for foreground mode
 */
void SIGTSTPHandler(int sig) {
    if(foregroundModeOnly == false) {
        char* foregroundModeMessage = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, foregroundModeMessage, 49);
        fflush(stdout);
        foregroundModeOnly = true;
    } else {
        char* foregroundExitMessage = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, foregroundExitMessage, 29);
        fflush(stdout);
        foregroundModeOnly = false;
    }
}

/*
 *  SIGCHLD handler
 */
void SIGCHLDHandler(int sig) {
    pid_t collectedPID = waitpid(-1, &childStatus, WNOHANG);

    // Check to see if PID is of a background child process
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(backgroundPIDs[i] == collectedPID && collectedPID > 0) {
            if(WIFEXITED(childStatus)){
                printf("Background child PID %d is done with exit status %d\n", collectedPID, WEXITSTATUS(childStatus));
                fflush(stdout);
            } else if(WIFSIGNALED(childStatus)) {
                printf("Background child PID %d is terminated by signal %d\n", collectedPID, WTERMSIG(childStatus));
                fflush(stdout);
            }
            backgroundPIDs[i] = 0;
        }
    }

    // Remove value from background PIDs array
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(backgroundPIDs[i] == collectedPID) {
            backgroundPIDs[i] = 0;
            break;
        }
    }
}

/*
 *  Free memory allocated to the commandLine struct.
 */
void freeCommandLine() {
    int i = 0;
    // Free each pointer in the args array
    while(inputCommand->args[i] != NULL) {
        free(inputCommand->args[i]);
        i++;
    }

    // Free pointer to the args array itself
    free(inputCommand->args);

    // Free input and output file pointers
    if (inputCommand->inputFile != NULL) {
        free(inputCommand->inputFile);
    } else if (inputCommand->outputFile != NULL) {
        free(inputCommand->outputFile);
    }
}

/*
 *  Change the working directory of smallsh.
 */
int changeWD() {
    char *homeDir = NULL;
    char *pwd = NULL;
    // If command only
    if(inputCommand->argsNum == 1) {
        homeDir = getenv("HOME");
        chdir(homeDir);
    } else { // If there is an argument
        chdir(inputCommand->args[1]);
    }
    return 0;
}

/*
 *  Check command for input file and open a file descriptor
 *  Source: adapted from example code in Module 5 - Exploration: Processes and I/O
 */
void createInputFD() {
    // Process input file, if any
    int sourceFile = open(inputCommand->inputFile, O_RDONLY);
    if(sourceFile == -1) {
        perror("Cannot open input file\n");
        exit(1);
    }

    // Redirect from sourceFile to stdin
    int result = dup2(sourceFile, 0);
    if(result == -1) {
        perror("Cannot redirect from source file");
        exit(2);
    }
}

/*
 *  Check command for output file and open a file descriptor
 *  Source: adapted from example code in Module 5 - Exploration: Processes and I/O
 */
void createOutputFD() {
    // Process output file, if any
    int targetFile = open(inputCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(targetFile == -1) {
        perror("Cannot open output file\n");
        fflush(stdout);
        exit(1);
    }

    // Redirect from stdout to targetFile
    int result = dup2(targetFile, 1);
    if(result == -1) {
        perror("Cannot redirect to output file\n");
        fflush(stdout);
        exit(2);
    }
}


/*
 *  Exit shell after killing any other processes or jobs.
 */
void exitShell() {
    // Free command struct
//    freeCommandLine();
    // Set shell loop to stop running
    runShell = 0;
    // Terminate all background processes
    for(int i = 0; i < MAX_PROCESSES; i++) {
        if(backgroundPIDs[i] > 0) {
            kill(backgroundPIDs[i], SIGTERM);
        }
    }
}