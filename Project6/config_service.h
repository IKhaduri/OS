#include <stdlib.h>
#include <string.h>
#include "bool.h"

const char[] CONFIG_PATH = "/Config.conf";
const char[] NO_KEY_VALUE = "no_key_value";

char[] get_config_value(char *vhost_name, char *key);