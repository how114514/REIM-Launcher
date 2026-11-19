# reim-analyze.jar is built from JDK 25 sources. The JDK is an offline
# Converter tool only; the Native Runtime never loads a JVM.
find_program(JAVAC_EXECUTABLE javac
  HINTS "$ENV{JAVA_HOME}/bin"
  DOC "JDK 25 javac")
find_program(JAR_EXECUTABLE jar
  HINTS "$ENV{JAVA_HOME}/bin"
  DOC "JDK 25 jar")

set(REIM_ANALYZER_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/reim/analyze/Analyzer.java"
)
set(REIM_ANALYZER_CLASSES "${CMAKE_BINARY_DIR}/java-analyzer/classes")
set(REIM_ANALYZER_JAR "${REIM_BIN_DIR}/reim-analyze.jar")

if(JAVAC_EXECUTABLE AND JAR_EXECUTABLE)
  add_custom_command(
    OUTPUT "${REIM_ANALYZER_JAR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${REIM_ANALYZER_CLASSES}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${REIM_ANALYZER_CLASSES}"
    COMMAND "${JAVAC_EXECUTABLE}" -encoding UTF-8 -d "${REIM_ANALYZER_CLASSES}"
            ${REIM_ANALYZER_SOURCES}
    COMMAND "${JAR_EXECUTABLE}" --create --file "${REIM_ANALYZER_JAR}"
            --main-class reim.analyze.Analyzer
            -C "${REIM_ANALYZER_CLASSES}" .
    DEPENDS ${REIM_ANALYZER_SOURCES}
    COMMENT "Building reim-analyze.jar (JDK 25)"
    VERBATIM
  )
  add_custom_target(reim_analyze_jar ALL DEPENDS "${REIM_ANALYZER_JAR}")
else()
  message(WARNING
    "javac/jar not found. reim-analyze.jar will not be built. "
    "Install JDK 25 (Temurin) or set JAVA_HOME.")
endif()
