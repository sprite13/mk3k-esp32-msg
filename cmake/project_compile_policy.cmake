function(project_apply_compile_policy target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-std=gnu17>
        $<$<COMPILE_LANGUAGE:C>:-Wall>
        $<$<COMPILE_LANGUAGE:C>:-Wextra>
        $<$<COMPILE_LANGUAGE:C>:-Werror>
        $<$<COMPILE_LANGUAGE:C>:-Werror=extra>
        $<$<COMPILE_LANGUAGE:C>:-Werror=unused-function>
        $<$<COMPILE_LANGUAGE:C>:-Werror=unused-variable>
        $<$<COMPILE_LANGUAGE:C>:-Werror=unused-but-set-variable>
        $<$<COMPILE_LANGUAGE:C>:-Werror=deprecated-declarations>
        $<$<COMPILE_LANGUAGE:C>:-Werror=unused-parameter>
        $<$<COMPILE_LANGUAGE:C>:-Werror=sign-compare>
        $<$<COMPILE_LANGUAGE:C>:-Werror=enum-conversion>)
endfunction()
