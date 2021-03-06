//
// Created by irakli on 9/29/16.
//
#include "fsh_nice.h"

char ** get_changed_copy_array(int len, pos_arguments *args, char **funcname, int diff) {
    char ** argv = malloc((len + 2) * sizeof(char *));
    argv[0] = *funcname;
    int i = 0;
    for (; i < len; i++) {
        argv[i+1] = args->arguments[i+diff];
    }
    argv[len+1] = NULL;

    return argv;
}

bool fsh_nice(pos_arguments *args) {
    if (!args || !args->arguments) {
        printf("syntax error in calling 'nice'\n");
        return false;
    }

	context *c = (context *)args->arguments[args->num_args];

    if (args->num_args < 2 && !strcmp(args->arguments[0], "nice"))
        return fsh_nice_helper('0', 10, NULL, NULL, c);

    char flag = 'n';
    char *program_name;
    int increment = 10;
    char **argv;

    if (!strcmp(args->arguments[0], "nice")) {
        if (!strcmp(args->arguments[1], "-n")) {
            if (args->num_args < 3 || !is_valid_integer(args->arguments[2]) || args->num_args < 4) {
                printf("syntax error in passing arguments in 'nice'\n");
                return false;
            } else {
                program_name = args->arguments[3];
                increment = atoi(args->arguments[2]);
                argv = get_changed_copy_array(args->num_args - 4, args, &program_name, 4);
            }
        } else {
            program_name = args->arguments[1];
            argv = get_changed_copy_array(args->num_args - 2, args, &program_name, 2);
        }
        
    } else {
        program_name = args->arguments[0];
        argv = get_changed_copy_array(args->num_args - 1, args, &program_name, 1);
    }

    if (args->num_args < 1 || !args->arguments) {
        printf("syntax error in passing arguments in 'nice'\n");
        return false;
    }

    bool res = fsh_nice_helper(flag, increment, program_name, argv, c);
    free(argv);
    return res;
}

bool fsh_nice_helper(char flag, int increment, char * program_name, char * const argv[], context *c){
    errno = 0;
    if (flag!='n'){
        int prior = getpriority(PRIO_PROCESS,getpid());
        if (errno!=0){
            error_handler(errno,"getting niceness");
            return false;
        }else {
            printf("%d\n", prior);
            return true;
        }
    }
    //TLPI
    pid_t childPid;
	if(c->no_fork) childPid = 0;
	else childPid = fork();
    switch (childPid) {
        case -1:
            printf("error while trying to fork");
            return false;
        case 0:
            errno = 0;
            if (nice(increment)<0)
                error_handler(errno, "nice");
            else if (execvp(program_name,argv)<0)
                error_handler(errno, "executing given program");
            exit(-1);
            break;
        default:
			if(true) {
				int status;
				int res = wait(&status);
				if (res < 0) {
					error_handler(errno, "waiting for child to terminate");
					return false;
				}
				else return (status == 0);
			}
            break;
    }
    return true;
}

