#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_LENGTH 1000
#define LOG_FILE "/home/amencsed/Desktop/UbuntuShell/logfile.txt"
#define GREEN "\x1b[32m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define RED "\x1b[31m"
FILE* file_logger;
/* 
   command type can either be built in command 
   or normal command or exit command 
*/
typedef enum {
    built_in_shell_command,
    normal_command,
    exit_command_shell
} command_type;

/* logging the child process the logging file*/
void write_to_log_file(pid_t pid, int status) {
    file_logger = fopen(LOG_FILE, "a");
    if (file_logger != NULL) {
        fprintf(file_logger, "Child process %d %s with status code %d\n", pid,
            WIFEXITED(status) ? "exited" : WIFSIGNALED(status) ? "was terminated by signal" : "was stopped by signal",
            WEXITSTATUS(status));
        fclose(file_logger);
    }
}
/* 
   the function to reap zombie child processes
   waitpid() suspends the calling process 
   until the system gets status information 
   on the child process and using pooling mode WNOHANG
   as it takes large amount of time to suspend parent 
   compared to time for child to terminate.
*/
void reap_child_zombie() {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        write_to_log_file(pid, status);
    }
}
/* signal handler for parent */
void on_child_exit () {
    reap_child_zombie();
}

/* set the home directory as the default directory */
void setup_environment(){
    chdir(getenv("HOME"));
}

/* execute cd command*/
void cd (char* command_parameters[]) {
    char path[MAX_LENGTH]; 
    char *ptr; 
    int k = 0;

    if(command_parameters[1] == NULL || strcmp(command_parameters[1], "~") == 0) {
        chdir(getenv("HOME"));
        return;
    }

    else if(*command_parameters[1] == '~') {
        ptr = getenv("HOME");
        for (int i = 0; i < strlen(ptr); i++) {
            path[k++] = ptr[i]; // coping  the path
        }
        path[k] = '\0';
        command_parameters[1]++;
        strcat(path, command_parameters[1]);

        if (chdir(path) != 0) {
            perror(RED "cd");
        }
    }
    else {
        if (chdir(command_parameters[1]) != 0) {
            perror(RED "cd");
        }
    }
}

/* export command */
void export (char command[]) {
    char *name, *value;

    name = strsep(&command, "="); // = is the delimter
    value = strdup(command);

    // for qoutes
    if (value[0] == '"' && value[strlen(value) - 1] == '"') {
        value[strlen(value) - 1] = '\0';
        value++;
    }
    setenv(name, value, 1);
}

/* execute the echo*/
void echo(char command[]) {
    // for qoutes
    if (command[0] == '"' && command[strlen(command) - 1] == '"') {
        command[strlen(command) - 1] = '\0';
        command++;
    }
    printf("%s\n", command);
}

// function for builtin commands: cd, echo, export
void execute_shell_bultin(char *parameters[]) {
    char *command = parameters[0];
    if (strcmp(command, "cd") == 0) {
        cd(parameters);
    }
    else if (strcmp(command, "echo") == 0) {
        echo(parameters[1]);
    }
    else if (strcmp(command, "export") == 0) {
        export(parameters[1]);
    }
}

//forking a new child process
void execute_command(char *parameters[], int background) {
    pid_t pid = fork();
    int status;
    if (pid < 0) {
        perror(RED "Forking failed");
        exit(1);
    }
    else if (pid == 0) {
        if (background) {
            printf("process: %d\n", getpid());
            sleep(10);
        }
        execvp(parameters[0], parameters);
        printf(RED "%s: command not found\n", parameters[0]);
        exit(1);
    }
    else {
        if (background) {
            // return immediately
            waitpid(pid, &status, WNOHANG);
        }
        else {
            // wait till child ends
            waitpid(pid, &status, 0);
        }
    }
}

char* remove_starting_trailing_spaces(char *command) {
    /* remove spaces at the start */
    while (command[0] == ' ') {
        command++;
    }
    /* remove spaces at the end */
    while (command[strlen(command) - 1] == ' ') {
        command[strlen(command) - 1] = '\0';
    }
    return command;
}

/* read the command and terminate at "\n" excape character*/
void read_command (char command[]) {
    char cwd[MAX_LENGTH];
    getcwd(cwd, sizeof(cwd));
    printf(MAGENTA "%s", cwd);
    printf(GREEN "$-");
    printf(BLUE " ");
    fgets(command, MAX_LENGTH, stdin);
    command[strcspn(command, "\n")] = '\0';
}


/* replace var by its value */
char* replace_var(char* input_command, int foundAt) {
    
    char var[MAX_LENGTH]; 
    int v = 0;

    char output_command[MAX_LENGTH]; 
    int k = 0;

    // characters before $
    for (int i = 0; i < foundAt; i++) {
        output_command[k++] = input_command[i];
    }

    for (int i = foundAt + 1; i < strlen(input_command); i++) {
        if (input_command[i] != '$' && input_command[i] != ' ' && input_command[i] != '"') {
            var[v++] = input_command[i];
        }
        else {
            var[v++] = '\0';
            break;
        }
    }

    char *value = getenv(var);

    // plug in var's value 
    if (value != NULL) {
        for (int i = 0; i < strlen(value); i++) {
            output_command[k++] = value[i];
        }
    }

    // then complete the string with the remaining characters
    for (int i = foundAt + strlen(var) + 1; i < strlen(input_command); i++) {
        output_command[k++] = input_command[i];
    }

    output_command[k++] = '\0';

    strcpy(input_command, output_command); // updates the input by the var value
    return input_command;
}

/* parse the command and get the parameters of it*/
void parse_command(char command[], char* parameters[]) {
    // consecutively remove $var by value
    while (1) {
        int i = 0;
        int flag = 0;
        while (i < strlen(command)) {
            if (command[i] == '$') {
                flag = 1;
                break;
            }
            i++;
        }
        if (!flag) {
            break;
        }
        command = replace_var(command, i);
    }

    char *delimiter = " ";
    int arg_count = 0;

    char *token = strtok(command, delimiter);
    char *first_token = strdup(token);
    // Check if the first token is a built-in command
    if (strcmp(first_token, "cd") == 0 
        || strcmp(first_token, "echo") == 0 
        || strcmp(first_token, "export") == 0) {
            
        while (token != NULL && arg_count < MAX_LENGTH) {
            parameters[arg_count++] = strdup(token);
            token = strtok(NULL, "");
        }
    } 
    else {
        while (token != NULL && arg_count < MAX_LENGTH) {
            parameters[arg_count++] = token;
            token = strtok(NULL, " ");
        }
    }
}


/* begin to execute the commands */
void shell () {
    int command_exit = 0;
    do {
        char command [MAX_LENGTH] = {};
        char *parameters [MAX_LENGTH] = {}; // array of pointers
        command_type cmd_type;
        int background = 0;
        /* 
           read the input and 
           removed the un-necessary spaces
        */
        read_command(command);
        strcpy(command, remove_starting_trailing_spaces(command));
        

        if(strlen(command) == 0){
            continue;
        }

        /* detected that it is background */
        if (command[strlen(command) - 1] == '&') {
            background = 1;
            command[strlen(command) - 1] = '\0';
            remove_starting_trailing_spaces(command);
        }

        parse_command(command, parameters);
        
        /* detected it is exit command */
        if (strcmp(parameters[0], "exit") == 0) {
            command_exit = 1;
            cmd_type = exit_command_shell;
        }

        /* detected it is built in shell command */
        else if (strcmp(parameters[0], "cd") == 0 
                || strcmp(parameters[0], "echo") == 0 
                || strcmp(parameters[0], "export") == 0) {
            cmd_type = built_in_shell_command;
        }

        /* normal command detected */
        else {
            cmd_type = normal_command;
        }

        switch (cmd_type) {
            case built_in_shell_command:
                execute_shell_bultin(parameters);
                break;
            case normal_command:
                execute_command(parameters, background);
                break;
            case exit_command_shell:
                break;
        }
    } while (!command_exit); // while not exit command
}

int main() {
    /* 
       this signal informs the parent process
       to clean up the child zombie process via 
       wait system call. 
    */
    signal(SIGCHLD, on_child_exit);

    //to home directory
    setup_environment();
    // begin reading & executing commands in terminal 
    shell();

    return 0;
}
