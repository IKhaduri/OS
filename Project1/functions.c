#include "functions.h"
#include "string.h"
#include "constants.h"

bool fsh_info(pos_arguments *args){
    printf("This Free Shell was made by an awesome group of four cs undergraduates\n");
    printf("Enjoy while it lasts\n");

    return true;
}


bool fsh_cd_helper(char * dir){
    if (chdir(dir)<0){
        error_handler(errno," changing directory ");
        return false;
    }
    return true;
}

bool fsh_cd(pos_arguments *args) {
    if (args == NULL || args->num_args < 1) {
        printf("Syntax error in calling 'cd'\n");
        return false;
    }

    return fsh_cd_helper(args->arguments[0]);
}


bool fsh_pwd(pos_arguments *args){
    //glibc standard
    char * cur_dir = getcwd(NULL, 0);
    if (cur_dir==NULL) {
        error_handler(errno, " getting working directory ");
        return false;
    }else {
        printf("%s\n", cur_dir);
        free(cur_dir);
        return true;
    }
}


bool fsh_echo_name_helper(char * variable_name) {
    char * res;
    res = getenv(variable_name);
    if (res==NULL){
        printf("no result\n");
        return false;
    }else{
        printf("%s\n",res);
    }
    return true;
}

bool fsh_echo_name(pos_arguments *args) {
    if (args == NULL || args->num_args < 1) {
        printf("Syntax error in calling 'echo'\n");
        return false;
    }

    return fsh_echo_name_helper(args->arguments[0]);
}

bool fsh_echo_string_helper(char * str){
    printf("%s\n",str);
    return true;
}

bool fsh_echo_string(pos_arguments *args) {
    if (args == NULL || args->num_args < 1) {
        printf("Syntax error in calling 'echo'\n");
        return false;
    }

    return fsh_echo_string_helper(args->arguments[0]);
}

bool fsh_echo_export(pos_arguments *args) {
    if (args == NULL || args->num_args < 2) {
        printf("Syntax error in calling 'export'\n");
        return false;
    }
    
    return fsh_echo_export_helper(args->arguments[0], args->arguments[1]);
}

bool fsh_echo_export_helper(char * name, char * value) {
    errno = 0;
    if (setenv(name, value, 1)<0){
        error_handler(errno, "setting environment variable");
        return false;
    }
    return  true;

}


long long fsh_echo_last_status(long long  t) {
    static long long rv = 0LL;
    if (t==STATUS_CODE_ECHO){
        return  rv;
    }
    rv = t;
    return rv;
}

bool fsh_echo(pos_arguments *args) {
    if (args == NULL) {
        printf("Syntax error in calling 'export'\n");
        return false;
    }

    if (args->num_args == 0)
        return fsh_echo_string_helper("");

    if (strcmp(args->arguments[0], "$?") == 0) {
        printf("%lld\n", fsh_echo_last_status(STATUS_CODE_ECHO));
        return true;
    }

    if (args->arguments[0][0] == '$')
        return fsh_echo_name_helper(&args->arguments[0][1]);

    char buffer[1024]; buffer[0] = '\0';
    int i;
    for (i = 0; i < args->num_args - 1; i++) {
        if (i < args->num_args-2) strcat(buffer, strcat(args->arguments[i], " "));
        else strcat(buffer, args->arguments[i]);
    }

    return fsh_echo_string_helper(buffer);
}


