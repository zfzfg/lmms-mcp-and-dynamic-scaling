include(CTest)
include("${CMAKE_SOURCE_DIR}/cmake/LmmsTestRuntime.cmake")
lmms_ensure_test_launcher("${CMAKE_SOURCE_DIR}")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/TimelineTest.cpp
	src/core/StabilizationTest.cpp
	src/core/EngineStabilizationTest.cpp
	src/core/SaveTransactionTest.cpp
	src/core/ProjectCodecTest.cpp
	src/core/BundleTransactionTest.cpp
	src/core/RecoveryIdentityTest.cpp
	src/core/ReleaseQueueTest.cpp
	src/core/WorkerDeadlineTest.cpp
	src/tracks/AutomationTrackTest.cpp
	src/gui/SampleThumbnailTest.cpp
	src/gui/DpiRasterTest.cpp
	src/gui/EmbedDpiTest.cpp
	src/gui/DpiCacheTest.cpp
	src/gui/WindowGeometryTest.cpp
	src/gui/DpiWidgetTest.cpp
)

if(WANT_MCP)
	list(APPEND LMMS_TESTS
		src/ai/AiObjectRegistryTest.cpp
		src/ai/AiProjectRevisionTest.cpp
		src/ai/AiCommandDispatcherTest.cpp
		src/ai/AiEditTransactionTest.cpp
		src/ai/AiFilePolicyTest.cpp
		src/ai/AiWavFileTest.cpp
		src/ai/AiSnapshotResourcesTest.cpp
		src/ai/AiJobManagerTest.cpp
		src/ai/AiChangePlanValidationTest.cpp
		src/ai/AiDomainContractTest.cpp
	)
endif()

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	lmms_add_qtest(${LMMS_TEST_NAME} ${LMMS_TEST_NAME} -o -,txt)
	if(WANT_MCP AND TARGET mcp-companion AND LMMS_TEST_NAME STREQUAL "AiCommandDispatcherTest")
		add_dependencies(${LMMS_TEST_NAME} mcp-companion)
	endif()
	if(LMMS_TEST_SRC MATCHES "src/gui/")
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
		foreach(DPI_SCALE 1 1.25 1.5 1.5625 1.75 2)
			lmms_add_qtest(${LMMS_TEST_NAME}_${DPI_SCALE} ${LMMS_TEST_NAME} -o -,txt)
			set_tests_properties(${LMMS_TEST_NAME}_${DPI_SCALE} PROPERTIES
				ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${DPI_SCALE};QT_AUTO_SCREEN_SCALE_FACTOR=0;QT_ENABLE_HIGHDPI_SCALING=1;QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough")
		endforeach()
	endif()

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
	if(LMMS_TEST_NAME MATCHES "^(StabilizationTest|EngineStabilizationTest)$")
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES TIMEOUT 180 LABELS "lmms-integration")
	else()
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES TIMEOUT 60 LABELS "lmms-small")
	endif()
	if(LMMS_TEST_SRC MATCHES "src/gui/")
		foreach(DPI_SCALE 1 1.25 1.5 1.5625 1.75 2)
			set_tests_properties(${LMMS_TEST_NAME}_${DPI_SCALE} PROPERTIES LABELS "lmms-small")
		endforeach()
	endif()
endforeach()

if(WIN32)
	add_library(lmms_missing_dep SHARED runtime/missing_dep.cpp)
	add_executable(lmms_missing_dll_child runtime/missing_child.cpp)
	target_link_libraries(lmms_missing_dll_child PRIVATE lmms_missing_dep)
	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/loader-paths/$<CONFIG>/loader_paths.h" CONTENT
		"#define LMMS_LAUNCHER_PATH R\"($<TARGET_FILE:lmms_test_launcher>)\"\n#define LMMS_CHILD_PATH R\"($<TARGET_FILE:lmms_missing_dll_child>)\"\n#define LMMS_DEP_DLL R\"($<TARGET_FILE:lmms_missing_dep>)\"\n")
	add_executable(WindowsLoaderTest src/core/WindowsLoaderTest.cpp)
	target_include_directories(WindowsLoaderTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/loader-paths/$<CONFIG>")
	target_link_libraries(WindowsLoaderTest PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
	target_compile_features(WindowsLoaderTest PRIVATE cxx_std_20)
	add_dependencies(WindowsLoaderTest lmms_test_launcher lmms_missing_dll_child lmms_missing_dep)
	lmms_add_qtest(WindowsLoaderTest WindowsLoaderTest -o -,txt)
	set_tests_properties(WindowsLoaderTest PROPERTIES TIMEOUT 30 LABELS "lmms-windows")
endif()
