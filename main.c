#include <fcntl.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


// returns slice of a string from  s_index to e_index inclusive
char* slice(char* input, int s_index, int e_index) {
    int n = strlen(input);
    if (s_index < 0 || e_index >= n || e_index < s_index) {
        return NULL;
    }
    int len = e_index - s_index + 1;
    char* res = (char*)malloc(len + 1);
    if (res == NULL) {
        return NULL;
    }
    memcpy(res, input + s_index, len);
    res[len] = '\0';
    return res;
}


//returns the binary name from it's path
//char* cmdFromPath(char* path) {
//    char* binary_name = strrchr(path, '/');
//    binary_name++;
//    return binary_name;
//}

//checks if a command exists by its full path
int isInPath(char* cmd_path) {
    if (access(cmd_path, F_OK) == 0) {
        return 1;
    } else {
        return 0;
    }
}

//takes a cmd looks through the PATH env variable for it's location and returns that path
char* binPath(char* cmd) {
    char* path_env = getenv("PATH");
    char* tmp = strdup(path_env);
    char* token = strtok(tmp, ":");
    char buf[1024];
    while (token) {
        int n = strlen(token) + strlen(cmd) + 2;
        snprintf(buf, n, "%s/%s", token, cmd);
        if (isInPath(buf)) {
            char* ret = strdup(buf);
            free(tmp);
            return ret;
        }
        token = strtok(NULL, ":");
    }
    free(tmp);
    return NULL;

}


char** parseArgs(const char* input, char delim) {
    int res_capacity = 1024;
    int r_index = 0;
    char** res = malloc(res_capacity * sizeof(char*));
    if (!res) {
        perror("malloc");
    }

    bool s_quote = false;
    bool d_quote = false;
    char token[1024];
    int t_len = 0;

    const char* cur = input;
    while (*cur) {
        if (*cur == '"' && !s_quote) {
            d_quote = !d_quote;
            cur++;

        } else if (*cur == '\'' && !d_quote) {
            s_quote = !s_quote;
            cur++;

        } else if (*cur == delim && !s_quote && !d_quote) {
            if (t_len > 0) {
                token[t_len] = '\0';
                if (r_index >= res_capacity) {
                    res_capacity *= 2;
                    res = realloc(res, res_capacity * sizeof(char*));
                    if (!res) {
                        perror("realloc");
                    }
                }
                res[r_index++] = strdup(token);
                t_len = 0;
            }
            cur++;

        } else if (*cur == '\\' && *(cur + 1)) {
            if (!s_quote) {
                if (d_quote) {
                    if (*(cur + 1) == '"' || *(cur + 1) == '\\' || *(cur + 1) == '$' || *(cur + 1) == '\n') {
                        cur++;
                        token[t_len++] = *cur++;
                    } else {
                        token[t_len++] = *cur++;
                    }
                } else {
                    cur++;
                    token[t_len++] = *cur++;
                }
            } else {
                token[t_len++] = *cur++;
            }
        } else {
            token[t_len++] = *cur++;
        }

    }

    if (t_len > 0) {
        token[t_len] = '\0';
        res[r_index++] = strdup(token);
    }

    res[r_index] = NULL;
    return res;
}


void runCmd(char** args) {
    int pid, p_stat;
    switch(pid = fork()) {
        case -1:
            perror("couldn't spawn child\n");
            break;
        case 0:
            execvp(args[0], args);
            break;
    }
    waitpid(pid, &p_stat, 0);
    return;
}

void redirect(int std, int perm, int flag, char* filename, char** cmd) {
    int fd = open(filename, O_WRONLY | O_CREAT | flag, perm);
    if (fd == -1) {
        perror("Failed to open the file for redirection");
        free(filename); // Only free the memory that was duplicated.
        return;
    }

    int saved_std = dup(std);
    if (saved_std == -1) {
        perror("Failed to save the original file descriptor");
        close(fd);
        free(filename);
        return;
    }

    if (dup2(fd, std) == -1) {
        perror("Failed to redirect file descriptor");
        close(fd);
        close(saved_std);
        free(filename);
        return;
    }

    close(fd);
    runCmd(cmd); // Run the command.

    if (dup2(saved_std, std) == -1) {
        perror("Failed to restore the original file descriptor");
    }

    close(saved_std);

    free(filename); // Free after all uses of the filename.
    // Do not free(cmd) here because it may be reused elsewhere.
}


//runs a command provided the cmd and it's args

void runCmdR(char** args) {
    bool redirection = false;
    bool append = false;
    int r_index = -1;
    int i = 0;

    while (args[i]) {
        if (strcmp(args[i], ">") == 0 || strcmp(args[i], "1>") == 0 || strcmp(args[i], "2>") == 0
        || strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0 || strcmp(args[i], "2>>") == 0) {
            redirection = true;
            r_index = i;
        }
        i++;
    }

    if (redirection) {
        int l_cmd = r_index + 1;
        char** cmd = (char**)malloc(sizeof(char*) * l_cmd);
        for (i = 0; i < r_index; i++) {
            cmd[i] = args[i];
        }
        cmd[r_index] = NULL;

        char* filename = strdup(args[r_index + 1]);

        if (strcmp(args[r_index], ">") == 0 || strcmp(args[r_index], "1>") == 0) {
            redirect(STDOUT_FILENO, 0666, O_TRUNC,filename, cmd);
        }
        else if (strcmp(args[r_index], "2>") == 0 ) {
            redirect(STDERR_FILENO, 0666, O_TRUNC, filename, cmd);
        }
        else if (strcmp(args[r_index], ">>") == 0 || strcmp(args[r_index], "1>>") == 0) {
            redirect(STDOUT_FILENO, 0666, O_APPEND, filename, cmd);
        }
        else if (strcmp(args[r_index], "2>>") == 0 ) {
            redirect(STDERR_FILENO, 0666, O_APPEND, filename, cmd);
        }

    }
    else {
        // If there's no redirection, just run the command normally
        runCmd(args);
    }
}

//returns a string containing the cwd
char* getCurrentDirectory() {
    char* buf;
    char* ptr;
    long size = pathconf(".", _PC_PATH_MAX);

    if ((buf = (char*)malloc((size_t)size)) != NULL) {
        ptr = getcwd(buf, (size_t)size);
    }
    return ptr;
}


int changeDirectory(const char* path) {
    //check if directory exists
    DIR* dir;
    if (strcmp(path, "~") == 0) {
        path = getenv("HOME");
    }
    dir = opendir(path);
    if (dir) {
        closedir(dir);
        chdir(path);
        return 1;
    }
    return 0;
}


int main() {
    // Wait for user input
    char input[100];

    while(1) {
        printf("$ ");
        fflush(stdout);
        fgets(input, 100, stdin);
        input[strlen(input) - 1] = '\0';

        int n = strlen(input);
        char* tmp = strdup(input);
        char** cmd = parseArgs(tmp, ' ');
        char** args;

        if (strcmp(input, "exit 0") == 0) {
            break;
        }

        else if (strcmp(cmd[0], "cd") == 0) {
            int cd_status = changeDirectory(cmd[1]);
            if (cd_status && cmd[2] != NULL) {
                printf("cd: %s: No such file or directory\n", slice(input, 3, n-1));
            }
            else if (!cd_status){
                printf("cd: %s: No such file or directory\n", cmd[1]);
            }
        }

        else if (strcmp(input, "pwd") == 0) {
            printf("%s\n", getCurrentDirectory());
        }

        else if (strncmp(input, "echo ", 5) == 0) {
            args = parseArgs(input,' ');
            runCmdR(args);
            free(args);
        }

        else if (strncmp(input, "type ", 5) == 0) {
            char* bin_path = binPath(cmd[1]);

            if (strcmp(cmd[1], "echo")==0 || strcmp(cmd[1], "exit")==0 || strcmp(cmd[1], "type")==0 || strcmp(cmd[1], "pwd")==0
            || strcmp(cmd[1], "cd") == 0) {
                printf("%s is a shell builtin\n",cmd[1]);
            }

            else if (bin_path != NULL) {
                printf("%s is %s\n",cmd[1], bin_path);
            }

            else {
                printf("%s: not found\n",cmd[1]);
            }
        }

        else if (binPath(cmd[0]) != NULL) {
            args = parseArgs(input, ' ');
            runCmdR(args);
            free(args);
        }

        else {
            printf("%s: command not found\n", input);
        }
        free(cmd);

    }
    return 0;
}



