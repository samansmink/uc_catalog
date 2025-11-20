# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(uc_catalog
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

duckdb_extension_load(delta
        GIT_URL https://github.com/samansmink/duckdb_delta
        GIT_TAG d6afcd460627e7851025b3258f8e4689c00bd6b7 # branch: ccv2
        SUBMODULES extension-ci-tools
)