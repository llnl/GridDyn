# modifyOctSourceFile.cmake

if(WIN32)
    if(griddynOCTAVE_wrap.cxx IS_NEWER_THAN griddynOCTAVE_wrap.cpp)
        if(VOID_SIZE EQUAL 8)
            file(READ griddynOCTAVE_wrap.cxx GRIDDYN_OCT_SOURCE)

            string(REPLACE "long swig_this\(\) const" "long long swig_this\(\)" GRIDDYN_OCT_SOURCE
                           "${GRIDDYN_OCT_SOURCE}"
            )
            string(REPLACE "return \(long\) this" "return \(long long\) this" GRIDDYN_OCT_SOURCE
                           "${GRIDDYN_OCT_SOURCE}"
            )
            string(REPLACE "\(long\) types[0].second.ptr" "\(long long\) types[0].second.ptr"
                           GRIDDYN_OCT_SOURCE "${GRIDDYN_OCT_SOURCE}"
            )

            file(WRITE griddynOCTAVE_wrap.cpp "${GRIDDYN_OCT_SOURCE}")
            set(FILE_WRITTEN TRUE)
        endif()
    endif()
endif(WIN32)

if(NOT FILE_WRITTEN)
    configure_file(griddynOCTAVE_wrap.cxx griddynOCTAVE_wrap.cpp COPYONLY)
endif()
