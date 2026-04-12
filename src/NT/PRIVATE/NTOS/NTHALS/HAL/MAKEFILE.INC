obj\i386\hal.def: hal.src
    cl386 /EP -Di386 $(C_DEFINES) hal.src > obj\i386\hal.def

$(BASEDIR)\public\sdk\lib\$(TARGET_DIRECTORY)\hal.lib: obj\$(TARGET_DIRECTORY)\hal.def
    -lib -def:obj\$(TARGET_DIRECTORY)\hal.def -out:$(@D)\hal.lib -machine:$(TARGET_DIRECTORY) obj\$(TARGET_DIRECTORY)\*.obj
