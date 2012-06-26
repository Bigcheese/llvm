find_package(Sphinx)

if( ${SPHINX_FOUND} )
  set(SPHINX_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_build")
  set(SPHINX_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_doctrees")
  set(SPHINX_HTML_DIR "${LLVM_BINARY_DIR}/docs/html")

  file(GLOB_RECURSE REST_SOURCES "*.rst")
  file(GLOB HTML_SOURCES "*.html" "tutorial/*.html")
  source_group(ReST FILES ${REST_SOURCES})
  source_group(HTML FILES ${HTML_SOURCES})

  add_custom_target(docs-sphinx
    ${SPHINX_EXECUTABLE}
    -q
    -b html
    -d "${SPHINX_CACHE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${SPHINX_HTML_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building HTML documentation with Sphinx"
    SOURCES ${REST_SOURCES} ${HTML_SOURCES} conf.py
    )

  install(DIRECTORY "${SPHINX_HTML_DIR}/"
          DESTINATION docs/llvm/html
          )
endif()
