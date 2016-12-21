cmake_minimum_required(VERSION 3.5)

add_library(SML INTERFACE)
target_include_directories(SML INTERFACE sml/include)
target_sources(SML INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/sml/include)
target_compile_features(SML INTERFACE
    cxx_alias_templates
    cxx_auto_type
    cxx_constexpr
    cxx_decltype
    cxx_lambdas
    cxx_lambda_init_captures
    cxx_nullptr
    cxx_relaxed_constexpr
    cxx_rvalue_references
    cxx_static_assert
    cxx_uniform_initialization
    cxx_user_literals
    cxx_variadic_templates)

add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE asio/asio/include)
    target_sources(asio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/asio/asio/include)
    target_compile_features(asio INTERFACE cxx_auto_type cxx_lambdas)
    target_compile_options(asio INTERFACE
            -Wno-suggest-override
            -Wno-old-style-cast
            -Wno-unused-variable
            -DASIO_STANDALONE=1)

add_library(GSL INTERFACE)
    target_include_directories(GSL INTERFACE GSL)
    target_sources(GSL INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/GSL)
    target_compile_features(asio INTERFACE cxx_relaxed_constexpr)

add_library(masqotte
    masqotte.cpp)
target_link_libraries(masqotte asio GSL SML)
