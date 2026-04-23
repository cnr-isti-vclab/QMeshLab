if (NOT DEFINED INPUT_PNG OR NOT DEFINED OUTPUT_ICNS)
    message(FATAL_ERROR "INPUT_PNG and OUTPUT_ICNS must be defined")
endif()

find_program(SIPS_EXECUTABLE sips REQUIRED)
find_program(ICONUTIL_EXECUTABLE iconutil REQUIRED)

get_filename_component(_output_dir "${OUTPUT_ICNS}" DIRECTORY)
get_filename_component(_output_name_we "${OUTPUT_ICNS}" NAME_WE)
set(_iconset_dir "${_output_dir}/${_output_name_we}.iconset")

file(REMOVE_RECURSE "${_iconset_dir}")
file(MAKE_DIRECTORY "${_iconset_dir}")

set(_base_sizes 16 32 128 256 512)
foreach(_size IN LISTS _base_sizes)
    execute_process(
        COMMAND "${SIPS_EXECUTABLE}" -z "${_size}" "${_size}" "${INPUT_PNG}" --out "${_iconset_dir}/icon_${_size}x${_size}.png"
        COMMAND_ERROR_IS_FATAL ANY
    )
    if (_size LESS_EQUAL 256)
        math(EXPR _size2x "${_size} * 2")
        execute_process(
            COMMAND "${SIPS_EXECUTABLE}" -z "${_size2x}" "${_size2x}" "${INPUT_PNG}" --out "${_iconset_dir}/icon_${_size}x${_size}@2x.png"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endforeach()

execute_process(
    COMMAND "${ICONUTIL_EXECUTABLE}" -c icns "${_iconset_dir}" -o "${OUTPUT_ICNS}"
    COMMAND_ERROR_IS_FATAL ANY
)
