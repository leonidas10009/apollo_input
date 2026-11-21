add_executable(apollo-input-core-tests core_tests.cpp)
target_link_libraries(apollo-input-core-tests PRIVATE apollo::input)
add_test(NAME apollo-input-core COMMAND apollo-input-core-tests)

if(WIN32 AND TARGET ViGEmClient::ViGEmClient)
    add_executable(apollo-input-xinput-loopback windows/xinput_loopback.cpp)
    target_link_libraries(apollo-input-xinput-loopback PRIVATE apollo::input)
    add_test(NAME apollo-input-vigem-xinput COMMAND apollo-input-xinput-loopback)
    set_tests_properties(apollo-input-vigem-xinput PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS integration
        TIMEOUT 20
    )
endif()
