#ifndef LOGGER_H
#define LOGGER_H

typedef enum logger_level {
    LOG_MIN = 1,
    LOG_STANDARD,
    LOG_MAX
}logger_level_t;

#define FILENAME "log.txt"

//public functions for users

void save_log(logger_level_t level, const char* message);
int set_message(char* message);
void init_logger(void);
void destroy_logger(void);

#endif //LOGGER_H
