# Make sure CMake received both the source seed file
# and the destination runtime file.
if(NOT DEFINED SOURCE_FILE OR NOT DEFINED DEST_FILE)
    message(FATAL_ERROR
        "CopySeedData.cmake requires SOURCE_FILE and DEST_FILE."
    )
endif()

# Get the runtime data folder from the destination path.
get_filename_component(
    DEST_DIR
    "${DEST_FILE}"
    DIRECTORY
)

# Create the runtime data folder if it does not exist yet.
file(MAKE_DIRECTORY "${DEST_DIR}")

# Only copy the original players.txt when the runtime database
# does not already exist.
#
# This prevents a rebuild from deleting teams that were added
# to the runtime database through the VLR scraper.
if(NOT EXISTS "${DEST_FILE}")
    configure_file(
        "${SOURCE_FILE}"
        "${DEST_FILE}"
        COPYONLY
    )

    message(STATUS
        "Created runtime player database from seed file."
    )
else()
    # The runtime database may contain teams added after startup,
    # so leave the existing file completely unchanged.
    message(STATUS
        "Runtime player database already exists; keeping it unchanged."
    )
endif()