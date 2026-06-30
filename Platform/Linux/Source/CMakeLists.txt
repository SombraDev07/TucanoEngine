if(NOT LINUX)
	return()
endif()

# Platform packages
find_package(X11 REQUIRED)
find_package(LibUUID REQUIRED)
find_package(LibICU REQUIRED)
find_package(LibUring REQUIRED)

if(NOT X11_Xcursor_FOUND)
	message(FATAL_ERROR "Could not find XCursor library.")
endif()

if(NOT X11_Xrandr_FOUND)
	message(FATAL_ERROR "Could not find XRandR library.")
endif()

if(NOT X11_Xi_FOUND)
	message(FATAL_ERROR "Could not find Xi (XInput) library.")
endif()

# OS libraries
target_link_libraries(bsf PUBLIC ${X11_LIBRARIES} ${X11_Xcursor_LIB} ${X11_Xrandr_LIB} ${X11_Xi_LIB})
target_link_libraries(bsf PRIVATE ${LibUUID_LIBRARIES})
target_link_libraries(bsf PRIVATE ${LibICU_LIBRARIES})
target_link_libraries(bsf PRIVATE ${LibUring_LIBRARIES})
target_link_libraries(bsf PRIVATE dl pthread)
