//
// Created by irakli on 9/27/16.
//

#include "util.h"
#include<string.h>

//error handling from tutorialspoint.com
void error_handler(int errnum, char * errmsg){
    fprintf(stderr, "Value of errno: %d\n", errnum);
    perror("Error printed by perror");
    fprintf(stderr, "Error while executing %s: %s\n",errmsg, strerror( errnum ));
}




bool string_pair_init(string_pair *this, const char *a, const char *b){
	if (a == NULL) this->a = NULL;
	else this->a = strdup(a);
	if (b == NULL) this->b = NULL;
	else this->b = strdup(b);
	if ((this->a == NULL && a != NULL) || (this->b == NULL && b != NULL)){
		string_pair_dispose(this);
		return false;
	}
	else return true;
}
void string_pair_dispose(string_pair *this){
	if (this->a != NULL) free(this->a);
	if (this->b != NULL) free(this->b);
}
bool string_pair_cpy_construct(string_pair *this, const string_pair *src){
	return string_pair_init(this, src->a, src->b);
}
bool string_pair_cpy(string_pair *dst, const string_pair *src){
	string_pair_dispose(dst);
	return string_pair_cpy_construct(dst, src);
}



/* constants */
const string_pair ESCAPE_SEQUENCES[] = {
		{ "\\\a", "\a" },
		{ "\\\b", "\b" },
		{ "\\\f", "\f" },
		{ "\\\n", "\n" },
		{ "\\\r", "\r" },
		{ "\\\t", "\t" },
		{ "\\\v", "\v" },
		{ "\\\\", "\\" },
		{ "\\\'", "\'" },
		{ "\\\"", "\"" },
		{ "\\\?", "\?" },
		STRING_PAIR_END };



bool get_path_of_program(char * program_name, char * res){
    const char* s = getenv("PATH");
    tokenizer tok;
    const char * delims[] = {":",DELIMITER_END};
    const string_pair empty[] = { STRING_PAIR_END };
    tokenizer_init(&tok,s,delims,empty, empty);
    while(tokenizer_move_to_next(&tok)){
        char * t = tokenizer_get_current_token(&tok);
        char * p = malloc(strlen(t)+strlen(program_name)+1);
        p[0]='\0';
        strcat(p,t);
        strcat(p,program_name);
        struct stat sb;
        if (stat(p, &sb) == 0 && S_ISDIR(sb.st_mode)){
            res = p;
            tokenizer_dispose(&tok);
            return true;
        }
    }
    tokenizer_dispose(&tok);
    return false;
}
