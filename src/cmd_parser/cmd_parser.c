#include "cmd_parser.h"

#include <stdio.h>
#include <string.h>

#include "constants.h"

cmd_data parse_cmd(int *argc, char* argv[]) {
    cmd_data data;
    data.action = BUILD_FILE;
    data.path = NULL;
    data.output = NULL;
    
    // Проверка на NULL аргументы
    if (argc == NULL || argv == NULL) {
        ERRLN(TERRA_ARGS_ERROR);
        data.action = ERROR;
        return data;
    }

    if (*argc <= 1) {
        data.action = HELP;
        return data;
    }

    for (int i = 1; i < *argc; i++) {
        if (argv[i] == NULL || argv[i][0] != '-') {
            // Это путь к файлу
            if (data.path == NULL) {
                data.path = argv[i];
                continue;
            } else {
                ERRFLN(ERR_BAD_ARGUMENT, argv[i]);
                data.action = ERROR;
                return data;
            }
        }

        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            data.action = VERSION;
            return data;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            data.action = HELP;
            return data;
        } else if (strcmp(argv[i], "--tesseract") == 0) {
            data.action = ANIMATION;
            return data;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--build") == 0) {
            // Проверка: есть ли следующий аргумент
            if (i + 1 >= *argc) {
                ERRLN(ERR_BUILD_FILE_NOT_SPECIFIED);
                data.action = ERROR;
                return data;
            }
            
            // Следующий аргумент не должен быть флагом
            if (argv[i + 1][0] == '-') {
                ERRFLN(ERR_BAD_ARGUMENT, argv[i + 1]);
                data.action = ERROR;
                return data;
            }
            
            // Проверка на дубликат build файла
            if (data.path != NULL && strcmp(data.path, argv[i + 1]) != 0) {
                ERRLN(ERR_MULTIPLE_BUILD);
                data.action = ERROR;
                return data;
            }
            
            data.path = argv[i + 1];
            i++;
            continue;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            // Проверка: есть ли следующий аргумент
            if (i + 1 >= *argc) {
                ERRLN(ERR_OUTPUT_FILE_NOT_SPECIFIED);
                data.action = ERROR;
                return data;
            }
            
            // Следующий аргумент не должен быть флагом
            if (argv[i + 1][0] == '-') {
                ERRFLN(ERR_BAD_ARGUMENT, argv[i + 1]);
                data.action = ERROR;
                return data;
            }
            
            if (data.output != NULL) {
                ERRLN(ERR_MULTIPLE_OUTPUT);
                data.action = ERROR;
                return data;
            }
            
            data.output = argv[i + 1];
            i++;
            continue;
        } else {
            ERRFLN(ERR_BAD_ARGUMENT, argv[i]);
            data.action = ERROR;
            return data;
        }
    }

    // Установить дефолтный output только если это не режим анимации
    if (data.action != ANIMATION && data.output == NULL) {
        data.output = "output";
    }

    return data;
}
