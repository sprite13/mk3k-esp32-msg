#pragma once

#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(condition)                                                     \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                    #condition);                                                  \
            exit(EXIT_FAILURE);                                                   \
        }                                                                         \
    } while (0)
