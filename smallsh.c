#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

// constants for max args and max processes allowed
#define MAX_ARGUMENTS 513
#define MAX_PROCESSES 100 

// Global variable to keep track of SIGTSTP
// 1 is true, 0 is false
int background_on = 1;

// Structure that contains all information related to a user command
typedef struct Command{
    char* cmd;
    char* arguments[MAX_ARGUMENTS];
    char* input_file;
    char* output_file;
}command;


command get_input(pid_t pid) 
{   
    // Print integer pid to a str. Used for $$ expansion
    char pid_str[10];
    sprintf(pid_str, "%d", pid);

    // Initialize variables needed to read input from user
    command TempCommand;
    size_t line_size = 2048;
    int bytes_read;
    char* input_string;
    input_string = (char*) calloc(line_size, sizeof(char));

    // Prompt user for command
    printf(": ");
    fflush(stdout);
    // Clear errors - required for program to work after sigtstp_action is called
    clearerr(stdin);
    bytes_read = getline(&input_string, &line_size, stdin);

    // Ignore empty lines
    if (bytes_read == 1){
        TempCommand.cmd = NULL;
    }

    else {
        if (strcmp(input_string, "") == 0) {
            // In case an empty string somehow gets through, set cmd to NULL and return
            TempCommand.cmd = NULL;
            return TempCommand;
        }

        // Remove new line character from input_string
        if(input_string != NULL){
            input_string[strcspn(input_string, "\n")] = 0;
        }

        // initialize memory to copy input_string
        char* input_copy;
        input_copy = calloc(line_size, sizeof(char));
        
        // Copy input_string into input_copy one character at time
        int j = 0;
        for (int k=0; k < bytes_read; k++) {
            if (input_string[k] == '$' && input_string[k+1] == '$') {
                // Variable expansion of $$ if it exists
                strcat(input_copy, pid_str);
                j = j + strlen(pid_str);
                k++;
            }

            else {
                // Copy the character if $$ not found 
                input_copy[j] = input_string[k];
                j++;
            }
        }
        
        char* token = strtok(input_copy, " ");    // First part of input saved into token
        
        TempCommand.cmd = strdup(token);  // command copied into the command struct
        TempCommand.arguments[0] = strdup(token); // copy into arg array
        
        token = strtok(NULL, " "); // Check for next arguments
        TempCommand.arguments[1] = NULL; // Set second element to NULL - used to check if arguments given
        TempCommand.input_file = "";    // Set input and output redirection to "" by default
        TempCommand.output_file = "";   
        
        // Index for arg array
        int i = 1;
        while (token) {

            // Check for input redirect
            if (strcmp(token, "<") == 0) {
                // copies name into command struct, do not add it to arg array
                token = strtok(NULL, " ");
                TempCommand.input_file = strdup(token);
            }

            // Check for output redirect
            else if (strcmp(token, ">") == 0) {
                // copies name into command struct, do not add it to arg array
                token = strtok(NULL, " ");
                TempCommand.output_file = strdup(token);
            }

            // Otherwise it is an argument to the command
            else {
                TempCommand.arguments[i] = strdup(token); // Add all arguments to the command struct
                TempCommand.arguments[i+1] = NULL;
                i++;
            }
            token = strtok(NULL, " ");
        }
        // free the used memory 
        free(input_copy);
    }
    free(input_string);
    
    return TempCommand;
}


void handle_sigtstp(int signo) 
{   
    // If background processes are currently allowed -> toggle them off
    if (background_on == 1) {
        // Message as required
        char* message = "Entering forground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 49);
        fflush(stdout);

        // Set global to false
        background_on = 0;
    }

    // If background process are not allowed -> toggle them on
    else {
        // Message as required
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 29);
        fflush(stdout);

        // set global to true
        background_on = 1;
    }
}


void exit_status(int last_exit_status) {
	// Checks the last saved exit status with WIFEXITED and prints the exact 
    // output required in the assignment page
	if (WIFEXITED(last_exit_status)) {
		// If exited by status
		printf("Exit value %d\n", WEXITSTATUS(last_exit_status));
        fflush(stdout);
	} else {
		// If terminated by signal
		printf("Terminated by signal %d\n", WTERMSIG(last_exit_status));
        fflush(stdout);
	}
}


void execute_cmd(command user_command, int* last_exit_status, int* run_in_background, struct sigaction sigint_action, struct sigaction sigtstp_action, int* child_list) 
{   
    // Initialize for input and output redirected -- used only if needed
    int input_fd;
    int output_fd;
    int result;


    // fork child   
    pid_t spawn_pid = -5;
    spawn_pid = fork();
    
    switch(spawn_pid) {
        // fork failed
        case -1:
            perror("Command failed!\n");
            exit(1);
            break;

        // fork successful
        case 0:
            // Reset SIGINT to default for child process ONLY if it is a foreground process
            if (!(*run_in_background == 1 && background_on == 1)) {
                sigint_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sigint_action, NULL);
            }

            // Couldn't get this to work without the unnecessary if statement
            // Not sure why
            if(1) {
                // set all children to ignore SIGTSTP
                sigtstp_action.sa_handler = SIG_IGN;
                sigaction(SIGTSTP, &sigtstp_action, NULL);
            }

            
            // If its a background process, and input/output redirect not reqesuted 
            // Send to I/O to /dev/null
            if (*run_in_background == 1 && background_on == 1) {
                // Check if input redirect requested
                if (strcmp(user_command.input_file, "") == 0) {
                    // Set to /dev/null if no input redirect and bg process
                    user_command.input_file = "/dev/null";
                }


                // Redirect output if user requested it
                if (strcmp(user_command.output_file, "") == 0) {
                    // Set to /dev/null if no output redirect and bg process
                    user_command.output_file = "/dev/null";
                }
            }
                        
            // Check if redirect input is required
            if (strcmp(user_command.input_file, "") != 0) {
                // open the file
                input_fd = open(user_command.input_file, O_RDONLY);

                // Error if it doesn't exist
                if (input_fd == -1) {
                    perror("");
                    exit(1);
                }

                // Use dup2 to redirect
                result = dup2(input_fd, 0);

                // Error message if it doesn't work
                if (result == -1) {
                    perror("");
                    exit(2);
                }

                // Close the file
                fcntl(input_fd, F_SETFD, FD_CLOEXEC);
            }

            // Check if output redirect required
            if (strcmp(user_command.output_file, "") != 0) {
                // open the file
                output_fd = open(user_command.output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                // Error message if it doesn't exist
                if (output_fd == -1) {
                    perror("");
                    exit(1);
                }

                // Use dup2 to redirect
                result = dup2(output_fd, 1);
                if (result == -1) {
                    perror("");
                    exit(2);
                }

                // Close the file
                fcntl(output_fd, F_SETFD, FD_CLOEXEC);
            }

            // Execute command in child process
            if (execvp(user_command.cmd, user_command.arguments)) {
                // Only executes this portion if there is an error
                perror("");
                fflush(stdout);
                exit(2);
            }
            break;

        default:


            // If user requests background and current signal allows it
            if (*run_in_background == 1 && background_on == 1) {
                // Find first available "empty" index in child_list array
                int a = 0;
                while (child_list[a] != 0) {
                    a++;
                }

                // Save pid to child_list arr
                child_list[a] = spawn_pid;

                // Give pid of background process
                printf("Background pid is %d\n", spawn_pid);
                fflush(stdout);
            }

            // Otherwise, run process in foreground
            else {
                // Wait for child process to finish
                pid_t child_pid = waitpid(spawn_pid, last_exit_status, 0);

                // If forground process is terminated early, print message
                if (!(WIFEXITED(*last_exit_status))) {
                    exit_status(*last_exit_status);
                }

            
            // Check for exited background child processes every time a 
            // non built-in command is executed
            // Check every current bg process saved
            for (int t = 0; t < MAX_PROCESSES; t++) {
                // id of 0 represents an empty space
                if (child_list[t] == 0) continue;
                
                else {
                    // Check if process is finished
                    spawn_pid = waitpid(child_list[t], last_exit_status, WNOHANG);
                    
                    // If process isn't finished, do nothing
                    if (spawn_pid == 0) {
                    }

                    // Message for finished background process that includes exit status
                    else {
                        printf("Background process %d is done. ", spawn_pid);
                        exit_status(*last_exit_status);
                        fflush(stdout);
                        child_list[t] = 0;
                    }
                }

                t++;
            } 
        }
    }
}


int main() 
{   
    // Get parent pid and pgid
    pid_t pid = getpid();
    pid_t pgid = getpgid(pid);

    int run_in_background = 0;  // used to check if the user requests a background program
    int last_exit_status = 0;   // saves the last exit status

    command user_command;

    // Initialize array for child processes
    pid_t child_list[MAX_PROCESSES];
    for (int i = 0; i < MAX_PROCESSES; i++) {
        child_list[i] = 0;
    }

    // Ignore ctrl+C handler
    struct sigaction sigint_action = {0};
    sigint_action.sa_handler = SIG_IGN;
    sigfillset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    // SIGTSTP handler
    struct sigaction sigtstp_action = {0};
    sigtstp_action.sa_handler = handle_sigtstp;
    sigfillset(&sigtstp_action.sa_mask);
    sigtstp_action.sa_flags = 0;
    sigaction(SIGTSTP, &sigtstp_action, NULL);


    while (1) {
        // Get and parse the command
        user_command = get_input(pid);
        
        // Ignore empty lines -- just reprompts the user
        if (user_command.cmd == NULL) {
            continue;
        }

        // Ignore comments -- reprompts the user
        else if (strncmp(user_command.cmd, "#", 1) == 0) {
            continue;
        }

        // Built in exit command - terminates all processes with the parent's group id
        else if (strcmp(user_command.cmd, "exit") == 0) {
            
            // Kill all child processes still running
            for (int p = 0; p < MAX_PROCESSES; p++) {
                if (child_list[p] == 0) continue;
                else{
                    kill(child_list[p], SIGKILL);
                }
            }

            // Exit parent process
            exit(0);
        }

        // Built in cd command
        else if (strcmp(user_command.cmd, "cd") == 0) {
            // If no arg passed to cd -> go to home directory
            if (user_command.arguments[1] == NULL) {               
                chdir(getenv("HOME"));
            }

            else {
                // Attempt to change directory
                if (chdir(user_command.arguments[1]) == -1) {
                    // Error message if directory change fails
                    printf("Directory not found\n");
                    fflush(stdout);
                }
                
            }
        }

        // Built in status command
        else if (strcmp(user_command.cmd, "status") == 0) {
            exit_status(last_exit_status);
        }

        else {

            int z = 0; 
            // Find last index of last argument saved in arg array
            while (user_command.arguments[z] != NULL) {
                z++;
            }

            // If & is the last argument entered, set run_in_background to true
            if (strcmp(user_command.arguments[z-1], "&") == 0) {
                run_in_background = 1;

                // Replace & with NULL in arg array to remove it from the command
                // This ensures command executes properly
                user_command.arguments[z-1] = NULL;
            }
            else {
                // Process to be run in foreground
                // Set run_in_background to false
                run_in_background = 0;
            }

            
            // Execute the command
            execute_cmd(user_command, &last_exit_status, &run_in_background, sigint_action, sigtstp_action, child_list);
        }
        // Reset user_command struct
        command user_command = {0};
    }
}