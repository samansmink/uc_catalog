# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(uc_catalog
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

duckdb_extension_load(delta
        GIT_URL https://github.com/samansmink/duckdb_delta
        GIT_TAG a5d61103d047ca4d8f8d862abf24d670dbde5060 # branch: ccv2
        SUBMODULES extension-ci-tools
)