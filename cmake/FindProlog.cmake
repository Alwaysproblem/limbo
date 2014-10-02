FIND_PROGRAM (PROLOG_INTERPRETER swipl)
IF (PROLOG_INTERPRETER)
	SET(PROLOG_INTERPRETER_LOAD "-f")
	SET(PROLOG_INTERPRETER_QUERY "-g")
ELSE()
#	ECLiPSe-CLP doesn't work because it doesn't support attributes
#	(theirs are different from SWI's)
#	FIND_PROGRAM(PROLOG_INTERPRETER eclipse-clp)
#	IF (PROLOG_INTERPRETER)
#		SET(PROLOG_INTERPRETER_LOAD "-L swi -f")
#		SET(PROLOG_INTERPRETER_QUERY "-e")
#	ENDIF()
ENDIF()

IF (PROLOG_INTERPRETER)
	MESSAGE(STATUS "Searching Prolog interpreter - found")
	MESSAGE(STATUS ${PROLOG_INTERPRETER})
ELSE()
	MESSAGE(STATUS "Searching Prolog interpreter - not found")
ENDIF()

