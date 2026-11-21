add_executable(apollo-input-controller-tester
    controller-tester/main.cpp
)

target_link_libraries(apollo-input-controller-tester
    PRIVATE
        apollo::input
        dinput8
        dxguid
        hid
        xinput1_4
        ole32
)

# ViGEmClient is a static library in the standalone build. Keep SetupAPI after
# it so the linker resolves its HID/device-interface imports.
if(TARGET ViGEmClient::ViGEmClient)
    target_link_libraries(apollo-input-controller-tester PRIVATE ViGEmClient::ViGEmClient setupapi)
else()
    target_link_libraries(apollo-input-controller-tester PRIVATE setupapi)
endif()

if(MINGW)
    # Keep the diagnostic tool runnable directly from Explorer or cmd.exe
    # without an MSYS2 shell on PATH.
    target_link_options(apollo-input-controller-tester PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

set_target_properties(apollo-input-controller-tester PROPERTIES
    OUTPUT_NAME "apollo-input-controller-tester"
)
