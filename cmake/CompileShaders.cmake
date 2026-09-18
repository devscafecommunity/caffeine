# CompileShaders.cmake — compile GLSL shaders to SPIR-V and optionally DXBC/MSL.
#
# caffeine_compile_shader(
#   NAME sphere_lit_vert
#   STAGE vertex
#   SOURCE ${CMAKE_SOURCE_DIR}/src/render/shaders/sphere_lit.vert
#   OUTPUT_DIR ${CAFFEINE_GENERATED_SHADER_DIR}
#   FORMATS SPIRV [DXBC] [MSL]
# )

function(caffeine_compile_shader)
    set(options)
    set(oneValueArgs NAME STAGE SOURCE OUTPUT_DIR)
    set(multiValueArgs FORMATS)
    cmake_parse_arguments(SHADER "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT SHADER_NAME OR NOT SHADER_STAGE OR NOT SHADER_SOURCE OR NOT SHADER_OUTPUT_DIR)
        message(FATAL_ERROR "caffeine_compile_shader requires NAME, STAGE, SOURCE, OUTPUT_DIR")
    endif()

    if("SPIRV" IN_LIST SHADER_FORMATS)
        set(_spirv_out "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT "${_spirv_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=${SHADER_STAGE} "${SHADER_SOURCE}" -o "${_spirv_out}"
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Compiling ${SHADER_NAME} (SPIR-V)"
        )
        list(APPEND CAFFEINE_SHADER_SPIRV_OUTPUTS "${_spirv_out}")
        set(CAFFEINE_SHADER_SPIRV_OUTPUTS "${CAFFEINE_SHADER_SPIRV_OUTPUTS}" PARENT_SCOPE)

        set(_header "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_spirv.hpp")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT="${_spirv_out}"
                -DOUTPUT="${_header}"
                -DSYMBOL=${SHADER_NAME}_spirv
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            DEPENDS "${_spirv_out}" "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            COMMENT "Embedding ${SHADER_NAME} SPIR-V"
        )
        list(APPEND CAFFEINE_SHADER_HEADERS "${_header}")
        set(CAFFEINE_SHADER_HEADERS "${CAFFEINE_SHADER_HEADERS}" PARENT_SCOPE)
    endif()

    if("DXBC" IN_LIST SHADER_FORMATS AND DXC_EXECUTABLE)
        set(_hlsl_out "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.hlsl")
        set(_dxbc_out "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.dxbc")
        add_custom_command(
            OUTPUT "${_dxbc_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=${SHADER_STAGE} "${SHADER_SOURCE}" -o "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_tmp.spv"
            COMMAND ${SPIRV_CROSS_EXECUTABLE} --hlsl "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_tmp.spv" -o "${_hlsl_out}"
            COMMAND ${DXC_EXECUTABLE} -T vs_5_0 -E main "${_hlsl_out}" -Fo "${_dxbc_out}"
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Compiling ${SHADER_NAME} (DXBC)"
        )
        list(APPEND CAFFEINE_SHADER_DXBC_OUTPUTS "${_dxbc_out}")
        set(CAFFEINE_SHADER_DXBC_OUTPUTS "${CAFFEINE_SHADER_DXBC_OUTPUTS}" PARENT_SCOPE)

        set(_header "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_dxbc.hpp")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT="${_dxbc_out}"
                -DOUTPUT="${_header}"
                -DSYMBOL=${SHADER_NAME}_dxbc
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            DEPENDS "${_dxbc_out}"
            COMMENT "Embedding ${SHADER_NAME} DXBC"
        )
        list(APPEND CAFFEINE_SHADER_HEADERS "${_header}")
        set(CAFFEINE_SHADER_HEADERS "${CAFFEINE_SHADER_HEADERS}" PARENT_SCOPE)
    endif()

    if("MSL" IN_LIST SHADER_FORMATS AND SPIRV_CROSS_EXECUTABLE)
        set(_msl_out "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.msl")
        add_custom_command(
            OUTPUT "${_msl_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
            COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=${SHADER_STAGE} "${SHADER_SOURCE}" -o "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_tmp.spv"
            COMMAND ${SPIRV_CROSS_EXECUTABLE} --msl "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_tmp.spv" -o "${_msl_out}"
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Compiling ${SHADER_NAME} (MSL source)"
        )
        list(APPEND CAFFEINE_SHADER_MSL_OUTPUTS "${_msl_out}")
        set(CAFFEINE_SHADER_MSL_OUTPUTS "${CAFFEINE_SHADER_MSL_OUTPUTS}" PARENT_SCOPE)

        set(_header "${SHADER_OUTPUT_DIR}/${SHADER_NAME}_msl.hpp")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_msl_out}" "${SHADER_OUTPUT_DIR}/${SHADER_NAME}.msl.txt"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT="${SHADER_OUTPUT_DIR}/${SHADER_NAME}.msl.txt"
                -DOUTPUT="${_header}"
                -DSYMBOL=${SHADER_NAME}_msl
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            DEPENDS "${_msl_out}"
            COMMENT "Embedding ${SHADER_NAME} MSL"
        )
        list(APPEND CAFFEINE_SHADER_HEADERS "${_header}")
        set(CAFFEINE_SHADER_HEADERS "${CAFFEINE_SHADER_HEADERS}" PARENT_SCOPE)
    endif()
endfunction()
