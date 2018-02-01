# Check if an int can be read from and written to an odd address
message(STATUS "Testing if an int can be read from and written to an odd address")
try_run(odd_address_io_int_runs odd_address_io_int_compiles
        ${PROJECT_BINARY_DIR}/config
        ${PROJECT_SOURCE_DIR}/config/odd_address_io_int.c)
if(odd_address_io_int_runs EQUAL 0)
   set(ODD_ADDRESS_IO_INT 1)
   message(STATUS "Testing if an int can be read from and written to an odd address -- Yes")
else()
   set(ODD_ADDRESS_IO_INT 0)
   message(STATUS "Testing if an int can be read from and written to an odd address -- No")
endif()
