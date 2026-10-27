# Applies test/test_budgets.txt after the tests are discovered (included by
# CTest, see test/CMakeLists.txt). One line per test:
#   scenario <timeout-seconds> <test name>
#   fuzz <timeout-seconds> <test name>
# Blank lines and lines starting with '#' are ignored.
file(STRINGS "${FM_BUDGET_FILE}" fm_budget_lines)
foreach(line IN LISTS fm_budget_lines)
  if(line MATCHES "^[ \t]*(#.*)?$")
    continue()
  endif()
  if(NOT line MATCHES "^(scenario|fuzz)[ \t]+([0-9]+)[ \t]+(.+)$")
    message(FATAL_ERROR "test_budgets.txt: cannot parse '${line}'")
  endif()
  set(fm_lane "${CMAKE_MATCH_1}")
  math(EXPR fm_timeout "${CMAKE_MATCH_2} * ${FM_BUDGET_SCALE}")
  set(fm_name "${CMAKE_MATCH_3}")
  if(NOT TEST "${fm_name}")
    continue()
  endif()
  get_test_property("${fm_name}" LABELS fm_labels)
  list(REMOVE_ITEM fm_labels unit)
  if(fm_lane STREQUAL "fuzz")
    list(APPEND fm_labels fuzz slow)
  else()
    list(APPEND fm_labels scenario)
  endif()
  set_tests_properties("${fm_name}" PROPERTIES
    LABELS "${fm_labels}" TIMEOUT ${fm_timeout})
endforeach()
