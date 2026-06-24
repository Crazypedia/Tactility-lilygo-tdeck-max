file(GLOB_RECURSE SOURCE_FILES Source/*.c*)

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "Source"
    REQUIRES Tactility GDEQ031T10 CST3530 TCA8418 driver
)
