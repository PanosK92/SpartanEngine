cmake_minimum_required(VERSION 3.24)
project(PhasmaMCP CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "PhasmaMCP = ${CMAKE_CURRENT_SOURCE_DIR}")

# Engine-agnostic MCP server library:
#   - Tool model (Tool.h)        : CallToolResult, Context, ToolDefinition (JSON Schema in/out)
#   - Protocol core (Server)     : transport-agnostic JSON-RPC dispatch (initialize / tools/list / tools/call / ping)
#   - HTTP transport             : httplib-backed, MCP-Protocol-Version header, loopback OAuth shim
#   - Utils.h                    : JSON helpers, Base64, UTF-8 sanitize, RGBA->PNG, path safety
#
# Optional codebase indexer (BM25 / CodebaseIndexer / CodebaseContext) is included by default
# but compiles only when PE_PMCP_CODEBASE=ON. Adopters who only want the protocol layer can
# pass -DPE_PMCP_CODEBASE=OFF to strip ~600 LoC and the related headers from the build.
option(PE_PMCP_CODEBASE "Build the BM25 + line-chunked codebase indexer alongside the MCP core" ON)

set(PMCP_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Server.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/HttpTransport.cpp"
)

set(PMCP_CODEBASE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BM25Index.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodebaseIndexer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CodebaseContext.cpp"
)

set(PMCP_SOURCES ${PMCP_CORE_SOURCES})
if(PE_PMCP_CODEBASE)
    list(APPEND PMCP_SOURCES ${PMCP_CODEBASE_SOURCES})
endif()

add_library(PhasmaMCP STATIC ${PMCP_SOURCES})

target_include_directories(PhasmaMCP
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Public preprocessor flag mirrors the option so downstream code can guard against
# missing codebase headers/types (e.g. `#if PE_PMCP_CODEBASE_ENABLED ...`).
target_compile_definitions(PhasmaMCP PUBLIC
    PE_PMCP_CODEBASE_ENABLED=$<BOOL:${PE_PMCP_CODEBASE}>
)

find_package(Threads REQUIRED)
target_link_libraries(PhasmaMCP PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(PhasmaMCP PRIVATE ws2_32)
    target_compile_definitions(PhasmaMCP PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
