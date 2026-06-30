if(NOT WIN32)
	return()
endif()

# OS libraries
target_link_libraries(bsf PRIVATE Winmm dinput8 xinput9_1_0 dxguid.lib)
target_link_libraries(bsf PRIVATE DbgHelp IPHLPAPI Rpcrt4 User32 Shcore)
