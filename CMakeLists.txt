cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(motor_controller)

target_include_directories(app PRIVATE inc inc/menu)

target_sources(app PRIVATE
    src/lcd_test.c
    src/main.c
    src/menu/menu.c
    src/menu/pannel.c
    # src/motor/adc.c
    # src/motor/speed.c
    # src/motor/motor.c
    # src/motor/svpwm.c
    # src/motor/controller.c
)

if(CONFIG_FONT_8X8)
target_sources(app PRIVATE
    src/menu/font_8x8.c
)
endif()

if(CONFIG_FONT_16X16)
target_sources(app PRIVATE
    src/menu/font_16x16.c
)
endif()

# 链接数学库
target_link_libraries(app PRIVATE m)