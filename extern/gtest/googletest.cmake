include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
        URL_MD5 7e11f6cfcf6498324ac82d567dcb891e

        DOWNLOAD_NO_PROGRESS 1
        INACTIVITY_TIMEOUT 5
        TIMEOUT 30
)

FetchContent_MakeAvailable(googletest)