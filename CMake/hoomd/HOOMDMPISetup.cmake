option (ENABLE_MPI "Enable the compilation of the MPI communication code" off)

##################################
## Find MPI
if (ENABLE_MPI)
    # the package is needed
    find_package(MPI REQUIRED)
    find_package(cereal CONFIG)
    if (cereal_FOUND)
        find_package_message(cereal "Found cereal: ${cereal_DIR}" "[${cereal_DIR}]")
    else()
        # work around missing ceralConfig.cmake

        find_path(cereal_INCLUDE_DIR NAMES cereal/cereal.hpp
            PATHS ${CMAKE_INSTALL_PREFIX}/include)
        add_library(cereal INTERFACE IMPORTED)
        set_target_properties(cereal PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${cereal_INCLUDE_DIR}")
        find_package_message(cereal "Could not find cereal, assuming it is on a default path" "[${cereal_INCLUDE_DIR}]")
    endif()

    mark_as_advanced(MPI_EXTRA_LIBRARY)
    mark_as_advanced(MPI_LIBRARY)
    mark_as_advanced(OMPI_INFO)

    # now perform some more in-depth tests of whether the MPI library supports CUDA memory
    if (ENABLE_HIP AND NOT DEFINED ENABLE_MPI_CUDA)
        if (MPI_LIBRARY MATCHES mpich)
            # find out if this is MVAPICH2
            get_filename_component(_mpi_library_dir ${MPI_LIBRARY} PATH)
            find_program(MPICH2_VERSION
                NAMES mpichversion mpich2version
                HINTS ${_mpi_library_dir} ${_mpi_library_dir}/../bin
            )
            if (MPICH2_VERSION)
                execute_process(COMMAND ${MPICH2_VERSION}
                                OUTPUT_VARIABLE _output)
                if (_output MATCHES "--enable-cuda")
                    set(MPI_CUDA TRUE)
                    message(STATUS "Found MVAPICH2 with CUDA support.")
                endif()
            endif()
        elseif(MPI_LIBRARY MATCHES libmpi)
            # find out if this is OpenMPI
            get_filename_component(_mpi_library_dir ${MPI_LIBRARY} PATH)
            find_program(OMPI_INFO
                NAMES ompi_info
                HINTS ${_mpi_library_dir} ${_mpi_library_dir}/../bin
            )
            if (OMPI_INFO)
                execute_process(COMMAND ${OMPI_INFO}
                                OUTPUT_VARIABLE _output)
                if (_output MATCHES "smcuda")
                    set(MPI_CUDA TRUE)
                    message(STATUS "Found OpenMPI with CUDA support.")
                endif()
            endif()
        endif()

        if (MPI_CUDA)
           option(ENABLE_MPI_CUDA "Enable MPI<->CUDA interoperability" off)
        else(MPI_CUDA)
           option(ENABLE_MPI_CUDA "Enable MPI<->CUDA interoperability" off)
        endif(MPI_CUDA)
    endif (ENABLE_HIP AND NOT DEFINED ENABLE_MPI_CUDA)

# backport CMake FindMPI fix from 3.12 to earlier versions
# https://gitlab.kitware.com/cmake/cmake/merge_requests/2529/diffs

if (CMAKE_VERSION VERSION_LESS 3.12.0 AND ENABLE_HIP)
    if(HIP_PLATFORM EQUALS "nvcc")
    string(replace "-pthread" "$<$<compile_language:cuda>:-Xcompiler>;-pthread"
      _mpi_c_compile_options "${mpi_c_compile_options}")
    set_property(target mpi::mpi_c property interface_compile_options "${_mpi_c_compile_options}")
    else()
    set_property(target mpi::mpi_c property interface_compile_options "${mpi_c_compile_options}")
    endif()
    unset(_mpi_c_compile_options)

    if(HIP_PLATFORM EQUALS "nvcc")
    string(REPLACE "-pthread" "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler>;-pthread"
      _MPI_CXX_COMPILE_OPTIONS "${MPI_CXX_COMPILE_OPTIONS}")
    set_property(TARGET MPI::MPI_CXX PROPERTY INTERFACE_COMPILE_OPTIONS "${_MPI_CXX_COMPILE_OPTIONS}")
    else()
    set_property(TARGET MPI::MPI_CXX PROPERTY INTERFACE_COMPILE_OPTIONS "${MPI_CXX_COMPILE_OPTIONS}")
    endif()
    message(STATUS "_MPI_CXX_COMPILE_OPTIONS: ${_MPI_CXX_COMPILE_OPTIONS}")
    unset(_MPI_CXX_COMPILE_OPTIONS)
endif()

endif (ENABLE_MPI)
