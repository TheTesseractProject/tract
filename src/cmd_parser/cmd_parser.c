#include "cmd_parser.h"

#include <stdio.h>
#include <string.h>

#include "constants.h"

static void set_error(cmd_data *data) {
    data->action = ERROR;
}

static bool take_value(int *i, int argc, char *argv[], const char *missing_msg,
                       char **out) {
    if (*i + 1 >= argc) {
        ERRLN(missing_msg);
        return false;
    }
    char *value = argv[*i + 1];
    if (value[0] == '-' && value[1] != '\0') {
        ERRFLN(ERR_BAD_ARGUMENT, value);
        return false;
    }
    *out = value;
    (*i)++;
    return true;
}

static bool set_path(cmd_data *data, char *path) {
    if (data->path != NULL) {
        ERRLN(ERR_MULTIPLE_BUILD);
        return false;
    }
    data->path = path;
    return true;
}

static bool set_output(cmd_data *data, char *output) {
    if (data->output != NULL) {
        ERRLN(ERR_MULTIPLE_OUTPUT);
        return false;
    }
    data->output = output;
    return true;
}

cmd_data parse_cmd(int *argc, char* argv[]) {
    cmd_data data;
    data.action = HELP;
    data.path = NULL;
    data.output = NULL;
    data.check_only = false;
    data.to_stdout = false;
    data.quiet = false;

    if (argc == NULL || argv == NULL) {
        ERRLN(TERRA_ARGS_ERROR);
        set_error(&data);
        return data;
    }

    if (*argc <= 1) {
        data.action = HELP;
        return data;
    }

    bool want_help = false;
    bool want_version = false;
    bool want_animation = false;
    bool end_of_options = false;

    for (int i = 1; i < *argc; i++) {
        char *arg = argv[i];
        if (arg == NULL) {
            ERRLN(TERRA_ARGS_ERROR);
            set_error(&data);
            return data;
        }

        if (!end_of_options && strcmp(arg, "--") == 0) {
            end_of_options = true;
            continue;
        }

        /* Options (anything that starts with '-' and is not just "-"). */
        if (!end_of_options && arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                want_help = true;
                continue;
            }
            if (strcmp(arg, "-v") == 0 || strcmp(arg, "-V") == 0 ||
                strcmp(arg, "--version") == 0) {
                want_version = true;
                continue;
            }
            if (strcmp(arg, "--tesseract") == 0) {
                want_animation = true;
                continue;
            }
            if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
                data.quiet = true;
                continue;
            }
            if (strcmp(arg, "-c") == 0 || strcmp(arg, "--check") == 0) {
                data.check_only = true;
                continue;
            }
            if (strcmp(arg, "--stdout") == 0) {
                data.to_stdout = true;
                continue;
            }
            if (strcmp(arg, "-b") == 0 || strcmp(arg, "--build") == 0) {
                char *value = NULL;
                if (!take_value(&i, *argc, argv,
                                ERR_BUILD_FILE_NOT_SPECIFIED, &value)) {
                    set_error(&data);
                    return data;
                }
                if (!set_path(&data, value)) {
                    set_error(&data);
                    return data;
                }
                continue;
            }
            if (strncmp(arg, "--build=", 8) == 0) {
                if (arg[8] == '\0') {
                    ERRLN(ERR_BUILD_FILE_NOT_SPECIFIED);
                    set_error(&data);
                    return data;
                }
                if (!set_path(&data, arg + 8)) {
                    set_error(&data);
                    return data;
                }
                continue;
            }
            if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
                char *value = NULL;
                if (!take_value(&i, *argc, argv,
                                ERR_OUTPUT_FILE_NOT_SPECIFIED, &value)) {
                    set_error(&data);
                    return data;
                }
                if (!set_output(&data, value)) {
                    set_error(&data);
                    return data;
                }
                continue;
            }
            if (strncmp(arg, "--output=", 9) == 0) {
                if (arg[9] == '\0') {
                    ERRLN(ERR_OUTPUT_FILE_NOT_SPECIFIED);
                    set_error(&data);
                    return data;
                }
                if (!set_output(&data, arg + 9)) {
                    set_error(&data);
                    return data;
                }
                continue;
            }

            ERRFLN(ERR_BAD_ARGUMENT, arg);
            set_error(&data);
            return data;
        }

        /* Positional source path. */
        if (!set_path(&data, arg)) {
            set_error(&data);
            return data;
        }
    }

    if (want_version) {
        data.action = VERSION;
    } else if (want_help) {
        data.action = HELP;
    } else if (want_animation) {
        data.action = ANIMATION;
    } else if (data.path != NULL) {
        data.action = BUILD_FILE;
    } else if (data.check_only || data.output != NULL || data.to_stdout) {
        ERRLN(ERR_BUILD_FILE_NOT_SPECIFIED);
        set_error(&data);
    } else {
        data.action = HELP;
    }

    if (data.action != BUILD_FILE) {
        return data;
    }

    /* `-o -` is shorthand for writing the generated C to stdout. */
    if (data.output != NULL && strcmp(data.output, "-") == 0) {
        data.output = NULL;
        data.to_stdout = true;
    }

    if (data.check_only && data.to_stdout) {
        ERRLN(ERR_CHECK_STDOUT_CONFLICT);
        set_error(&data);
        return data;
    }
    if (data.check_only && data.output != NULL) {
        ERRLN(ERR_CHECK_OUTPUT_CONFLICT);
        set_error(&data);
        return data;
    }

    if (!data.check_only && !data.to_stdout && data.output == NULL) {
        data.output = "output";
    }

    return data;
}
