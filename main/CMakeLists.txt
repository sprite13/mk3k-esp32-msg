idf_component_register(SRCS "app_main.c" REQUIRES log)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
