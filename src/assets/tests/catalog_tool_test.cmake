# crpp_catalog against this repository: it writes a catalog that lists the assets, the Skin
# Packs and the third-party files, each with its second source. It checks its own output with
# the parser before writing, so a catalog on disk is one the program accepts.
file(REMOVE_RECURSE ${OUT})
execute_process(COMMAND ${TOOL} ${OUT} --remote ${SOURCE}/cmake/remote_assets.txt --assets ${SOURCE}/assets
                        --skins ${SOURCE}/skins --ref v0.0.0 --default-skin cloud
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "crpp_catalog failed (${result}): ${error}")
endif()
file(READ ${OUT}/catalog.txt catalog)
foreach(expected
    "\nsource crpp https://raw.githubusercontent.com/kawalain/crpp/v0.0.0/\n"
    "\nskin cloud 1\n"
    "\nskin retro 1\n"
    "\nskin-version cloud 0.1.0\n"
    "\nskin-version retro 0.1.0\n"
    "\nskin-name cloud ko 구름\n"
    "\ntheme cloud cumulus dark\n"
    "\ntheme cloud pastel\n"
    "\ntheme-name retro luna_blue en Luna Blue\n"
    "\ndefault-skin cloud\n"
    "\nsound plastic\nsound-name plastic ko 플라스틱\nsound-name plastic en Plastic\n"
    "\nsound paper\n"
    "\nfile skin:cloud fonts/PretendardGOVVariable.ttf 13386352 "
    "\nfile skin:cloud skins/cloud/pack.cskin "
    "\nfile shared words/animals.txt "
    " from crpp assets/words/animals.txt\n"
    " from twemoji 1f507.svg nopick\n"
    "\nfile skin:cloud licenses/pretendard.txt 4418 "
    " from twemoji-root LICENSE-GRAPHICS\n")
  string(FIND "${catalog}" "${expected}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "the catalog lacks '${expected}'")
  endif()
endforeach()
foreach(absent "README.md" "signature ")
  string(FIND "${catalog}" "${absent}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "the catalog lists '${absent}'")
  endif()
endforeach()
# The digest a pack pins its parent by: a SHA-256 of the parent's own files.
execute_process(COMMAND ${TOOL} --digest ${OUT}/catalog.txt retro RESULT_VARIABLE result OUTPUT_VARIABLE digest ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT digest MATCHES "^[0-9a-f]+\n$")
  message(FATAL_ERROR "crpp_catalog --digest failed (${result}): ${digest}${error}")
endif()
string(LENGTH "${digest}" length)
if(NOT length EQUAL 65)
  message(FATAL_ERROR "crpp_catalog --digest printed '${digest}'")
endif()
execute_process(COMMAND ${TOOL} --digest ${OUT}/catalog.txt nothing RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
  message(FATAL_ERROR "crpp_catalog --digest found a skin that is not listed")
endif()
# What the tool wrote is not signed yet, so checking it as the program does fails.
execute_process(COMMAND ${TOOL} --verify ${OUT}/catalog.txt RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
  message(FATAL_ERROR "crpp_catalog --verify accepted an unsigned catalog")
endif()
file(REMOVE_RECURSE ${OUT})
