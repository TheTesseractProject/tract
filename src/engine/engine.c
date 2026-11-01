#include "engine.h"

#include <stdio.h>
#include <string.h>

#include "file_check/file_check.h"
#include "code_parser/code_parser.h"
#include "buffer_manager/buffer_manager.h"
#include "constants.h"

bool engine_build_file(FILE *input_file, const char *input_path, const char *output_path) {
    if (!input_file) {
        ERRLN(TERRA_ACCESS_ERROR);
        return false;
    }
    
    // Валидация входного пути
    if (input_path == NULL || input_path[0] == '\0') {
        ERRLN(ERR_BUILD_FILE_NOT_SPECIFIED);
        return false;
    }
    
    MSGF(BUILDING_MSG, 1, 1, input_path);
    fflush(stdout);

    // Initialize file buffer
    size_t file_size;
    char* buffer = init_file_buffer(input_file, &file_size);
    if (buffer == NULL) {
        return false;
    }
    
    // Проверка на пустой файл
    if (file_size == 0) {
        MSGLN("warning: empty source file");
    }

    bool parse_success = tesseract_parse(buffer, file_size, input_path);

    // Clean up buffer
    cleanup_buffer(buffer);

    if (parse_success) {
        MSGLN(OK);
        
        // Добавим информацию об output только если он не дефолтный
        if (output_path != NULL && strcmp(output_path, "output") != 0) {
            MSGFLN(BUILDING_BUILT_MSG, output_path);
        } else {
            MSGLN(TERRA_BUILD_FINISHED);
        }
    }

    return parse_success;
}
