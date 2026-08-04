# ---------------------------------------------------------------------------
# source_guard.cmake — configure-time guard for explicit source lists
#
# The CMake builds list their sources explicitly, one file per line, because an
# explicit list is deterministic and reviewable. The cost of that choice is a
# silent failure mode: a .cpp that exists on disk but is absent from the list is
# never compiled, and the build still reports success.
#
# That is not a theoretical risk. It happened on 2026-07-29 (issue #72): a
# partial source sync left the Linux gate box with a CMakeLists.txt that did not
# list CmdDropOdds.cpp. The build was green and the binary answered
# "Unknown command: 'dropodds'". The same shape catches any rebase that drops a
# CMakeLists hunk, and any file added on Windows — where Server.vcxproj is the
# source of truth — without the CMake list being updated to match.
#
# hb_guard_source_list() globs the directory FOR VERIFICATION ONLY. The explicit
# list stays authoritative; the glob result is compared against it and thrown
# away. An omission becomes a FATAL_ERROR at configure time instead of a green
# build that is missing a feature.
#
# The check is deliberately one-directional. A listed file that is missing from
# disk already fails loudly at add_executable() ("Cannot find source file"), so
# it needs no guard here.
#
# ---------------------------------------------------------------------------
# hb_guard_source_list(
#     LIST    <variable-name>    # name of the variable holding the explicit list
#     DIR     <directory>        # directory to verify, relative to the caller
#     LABEL   <text>             # human-readable name used in the error message
#     [RECURSE]                  # also verify subdirectories
#     [EXCLUDE <file> ...]       # named opt-outs, relative to DIR
# )
#
# LABEL is not derivable from LIST: two CMakeLists both guard a list named
# SHARED_SOURCES, over the same directory, with different exclusions.
#
# Every EXCLUDE entry must exist on disk and must not also appear in the list.
# A stale opt-out therefore fails configure too, so an exclusion cannot quietly
# outlive the file it was written for and re-open the hole it was carved from.
#
# Do not point this at vendored third-party directories. They contain sources
# that the build intentionally does not compile (alternative front-ends, test
# drivers), and the exclusion list would become a second copy of upstream's
# file listing.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

function(hb_guard_source_list)
	cmake_parse_arguments(_g "RECURSE" "LIST;DIR;LABEL" "EXCLUDE" ${ARGN})

	if(NOT _g_LIST OR NOT _g_DIR OR NOT _g_LABEL)
		message(FATAL_ERROR "hb_guard_source_list: LIST, DIR and LABEL are required")
	endif()

	# A misspelled keyword would otherwise be swallowed as a value and disarm the
	# guard silently, which is the failure this whole module exists to remove.
	if(_g_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "hb_guard_source_list: unrecognised arguments: ${_g_UNPARSED_ARGUMENTS}")
	endif()

	get_filename_component(_dir "${_g_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
	if(NOT IS_DIRECTORY "${_dir}")
		message(FATAL_ERROR "hb_guard_source_list: ${_g_LABEL}: no such directory: ${_dir}")
	endif()

	# CONFIGURE_DEPENDS is the point of the whole guard on an incremental build.
	# A partial source sync adds a .cpp without touching any CMakeLists, so
	# nothing would otherwise trigger a re-configure and the guard would not run
	# until the next clean build — which is exactly the case it exists to catch.
	# With CONFIGURE_DEPENDS the generated build system re-checks the glob on
	# every build and re-configures when the set of files changes.
	if(_g_RECURSE)
		file(GLOB_RECURSE _on_disk CONFIGURE_DEPENDS "${_dir}/*.cpp")
	else()
		file(GLOB _on_disk CONFIGURE_DEPENDS "${_dir}/*.cpp")
	endif()

	# Normalise the declared entries to absolute paths. They are written however
	# each CMakeLists finds clearest — bare filenames, ../ relative paths, or
	# ${VAR}-rooted absolute paths — and CMake itself resolves relative entries
	# against the calling CMakeLists directory, so resolve them the same way.
	#
	# Comparison is case-insensitive. A listed file whose case does not match
	# disk already fails loudly at add_executable() on a case-sensitive
	# filesystem, so nothing is hidden by folding case here; it only stops a
	# case-insensitive filesystem from reporting a listed file as unlisted.
	set(_declared "")
	foreach(_entry IN LISTS ${_g_LIST})
		get_filename_component(_abs "${_entry}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
		string(TOLOWER "${_abs}" _abs)
		list(APPEND _declared "${_abs}")
	endforeach()

	# Named opt-outs. Each one must still exist and must not also be declared,
	# so an exclusion cannot silently cover a file that was renamed or deleted.
	set(_excluded "")
	foreach(_entry IN LISTS _g_EXCLUDE)
		get_filename_component(_abs "${_entry}" ABSOLUTE BASE_DIR "${_dir}")
		if(NOT EXISTS "${_abs}")
			message(FATAL_ERROR
				"${_g_LABEL}: stale exclusion — '${_entry}' is named in the EXCLUDE list "
				"of hb_guard_source_list() but no longer exists on disk.\n"
				"Remove it from the EXCLUDE list in ${CMAKE_CURRENT_LIST_FILE}.")
		endif()
		string(TOLOWER "${_abs}" _abs)
		if(_abs IN_LIST _declared)
			message(FATAL_ERROR
				"${_g_LABEL}: '${_entry}' is both compiled (it is in ${_g_LIST}) and "
				"named as an EXCLUDE opt-out. Remove it from one of the two.")
		endif()
		list(APPEND _excluded "${_abs}")
	endforeach()

	set(_unlisted "")
	foreach(_file IN LISTS _on_disk)
		string(TOLOWER "${_file}" _key)
		if(NOT _key IN_LIST _declared AND NOT _key IN_LIST _excluded)
			file(RELATIVE_PATH _rel "${_dir}" "${_file}")
			list(APPEND _unlisted "${_rel}")
		endif()
	endforeach()

	if(_unlisted)
		list(SORT _unlisted)
		string(REPLACE ";" "\n    " _report "${_unlisted}")
		message(FATAL_ERROR
			"${_g_LABEL}: source file(s) present on disk but not compiled:\n"
			"    ${_report}\n"
			"These files exist in ${_dir} but are absent from the ${_g_LIST} list in "
			"${CMAKE_CURRENT_LIST_FILE}, so the build would have succeeded without "
			"compiling them.\n"
			"Add each file to ${_g_LIST}, or — if it is deliberately not compiled — "
			"name it in the EXCLUDE argument of the matching hb_guard_source_list() "
			"call with a comment saying why.")
	endif()
endfunction()
