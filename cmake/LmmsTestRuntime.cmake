# Shared Windows test-runtime helper.
# CMake's ENVIRONMENT_MODIFICATION needs 3.22. This repository still allows 3.13,
# so Windows tests go through a launcher that prepends the Qt bin directory.
# The launcher does not replace an existing test environment such as QT_QPA_PLATFORM.

function(lmms_ensure_test_launcher lmms_root)
	if(NOT WIN32)
		return()
	endif()
	if(TARGET lmms_test_launcher)
		return()
	endif()
	add_executable(lmms_test_launcher "${lmms_root}/tests/runtime/lmms_test_launcher.cpp")
	target_compile_features(lmms_test_launcher PRIVATE cxx_std_17)
endfunction()

function(lmms_qt_core_target out_var)
	if(DEFINED QT_VERSION_MAJOR AND TARGET Qt${QT_VERSION_MAJOR}::Core)
		set(${out_var} Qt${QT_VERSION_MAJOR}::Core PARENT_SCOPE)
	elseif(TARGET Qt6::Core)
		set(${out_var} Qt6::Core PARENT_SCOPE)
	elseif(TARGET Qt5::Core)
		set(${out_var} Qt5::Core PARENT_SCOPE)
	else()
		set(${out_var} "" PARENT_SCOPE)
	endif()
endfunction()

# Add a Qt test. On Windows the launcher prepends the Qt bin directory before the
# executable is loaded, so a missing DLL cannot open a blocking dialog first.
function(lmms_add_qtest test_name target_name)
    if(test_name MATCHES "^(StabilizationTest|EngineStabilizationTest)$")
        set(_launcher_timeout 170)
    else()
        set(_launcher_timeout 20)
    endif()
    if(TARGET lmms_test_launcher)
        add_dependencies(${target_name} lmms_test_launcher)
    endif()
	if(WIN32 AND TARGET lmms_test_launcher)
		lmms_qt_core_target(_qt_core)
		if(NOT _qt_core STREQUAL "")
			add_test(NAME ${test_name} COMMAND
				"$<TARGET_FILE:lmms_test_launcher>"
				--prefix "$<TARGET_FILE_DIR:${_qt_core}>"
				--plugins "$<TARGET_FILE_DIR:${_qt_core}>/../plugins"
				--timeout ${_launcher_timeout}
				--
				"$<TARGET_FILE:${target_name}>"
				${ARGN})
			return()
		endif()
	endif()
	add_test(NAME ${test_name} COMMAND "$<TARGET_FILE:${target_name}>" ${ARGN})
endfunction()

# Kept so older call sites still compile. Prefer lmms_add_qtest when adding the test.
function(lmms_wrap_windows_test test_name)
	# Command replacement after add_test does not stick on multi-config generators.
endfunction()
