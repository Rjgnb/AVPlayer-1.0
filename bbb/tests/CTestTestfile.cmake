# CMake generated Testfile for 
# Source directory: F:/project/audio_vioce/avplayer/tests
# Build directory: F:/project/audio_vioce/avplayer/bbb/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("av_tests" "F:/project/audio_vioce/avplayer/bbb/tests/Debug/av_tests.exe")
  set_tests_properties("av_tests" PROPERTIES  _BACKTRACE_TRIPLES "F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;29;add_test;F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("av_tests" "F:/project/audio_vioce/avplayer/bbb/tests/Release/av_tests.exe")
  set_tests_properties("av_tests" PROPERTIES  _BACKTRACE_TRIPLES "F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;29;add_test;F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("av_tests" "F:/project/audio_vioce/avplayer/bbb/tests/MinSizeRel/av_tests.exe")
  set_tests_properties("av_tests" PROPERTIES  _BACKTRACE_TRIPLES "F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;29;add_test;F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("av_tests" "F:/project/audio_vioce/avplayer/bbb/tests/RelWithDebInfo/av_tests.exe")
  set_tests_properties("av_tests" PROPERTIES  _BACKTRACE_TRIPLES "F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;29;add_test;F:/project/audio_vioce/avplayer/tests/CMakeLists.txt;0;")
else()
  add_test("av_tests" NOT_AVAILABLE)
endif()
