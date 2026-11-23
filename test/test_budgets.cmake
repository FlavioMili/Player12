# Applies test/test_budgets.txt after the tests are discovered (included by
# CTest, see test/CMakeLists.txt). One line per test:
#   scenario <timeout-seconds> <test name>
#   fuzz <timeout-seconds> <test name>
#   heavy <test name>   (sanitizer builds; handled in test/CMakeLists.txt)
# Blank lines and lines starting with '#' are ignored; names of tests that do
# not exist are ignored too. Only add_test/set_tests_properties exist at this
# stage, so the area labels are derived from the name prefix.
file(STRINGS "${FM_BUDGET_FILE}" fm_budget_lines)
foreach(line IN LISTS fm_budget_lines)
  # `heavy <name>` lines are applied when the shards are registered
  # (sanitizer builds only, see test/CMakeLists.txt).
  if(line MATCHES "^[ \t]*(#.*)?$" OR line MATCHES "^heavy[ \t]")
    continue()
  endif()
  if(NOT line MATCHES "^(scenario|fuzz)[ \t]+([0-9]+)[ \t]+(.+)$")
    message(FATAL_ERROR "test_budgets.txt: cannot parse '${line}'")
  endif()
  set(fm_lane "${CMAKE_MATCH_1}")
  math(EXPR fm_timeout "${CMAKE_MATCH_2} * ${FM_BUDGET_SCALE}")
  set(fm_name "${CMAKE_MATCH_3}")
  if(fm_name MATCHES "^core::")
    set(fm_area "core;leak-check")
  elseif(fm_name MATCHES "^lab::")
    set(fm_area "lab")
  elseif(fm_name MATCHES "^audio::")
    set(fm_area "audio")
  else()
    set(fm_area "gui")
  endif()
  if(fm_lane STREQUAL "fuzz")
    set(fm_labels "fuzz;slow;${fm_area}")
  else()
    set(fm_labels "scenario;${fm_area}")
  endif()
  set_tests_properties("${fm_name}" PROPERTIES
    LABELS "${fm_labels}" TIMEOUT ${fm_timeout})
endforeach()
