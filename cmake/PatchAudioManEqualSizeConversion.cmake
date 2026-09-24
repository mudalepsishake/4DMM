if(NOT DEFINED AUDIOMAN_SOURCE_DIR)
    message(FATAL_ERROR "AUDIOMAN_SOURCE_DIR was not supplied")
endif()

set(AUDIOMAN_FILTER "${AUDIOMAN_SOURCE_DIR}/src/sndcnvt.cpp")
if(NOT EXISTS "${AUDIOMAN_FILTER}")
    message(FATAL_ERROR "Expected AudioMan converter source was not found: ${AUDIOMAN_FILTER}")
endif()

# CAMConvertFilter needs a private source buffer whenever ACM conversion can
# write into the same-sized destination buffer.  The upstream condition skips
# that allocation when the byte counts are equal, which lets ConvertPCMGeneric
# receive the same pointer for input and output and hit assert(cbDst > cbSrc).
#
# Fix the ownership/call-site problem here.  Do not patch convert.cpp itself:
# once this buffer is allocated, source and destination are already distinct.
file(READ "${AUDIOMAN_FILTER}" FILTER_TEXT)

# ConvertPCMGeneric cannot safely use the same source and destination pointer
# when an ACM chunk happens to have equal source/destination byte counts.
# The relationship is block-alignment dependent and can change between chunks,
# so testing one representative size during Init is insufficient.  Whenever
# AudioMan selected ACM, always use its private source buffer.  Preserve the
# original shrinking-conversion behavior for non-ACM converters.
string(REGEX MATCH
    "if[ \t\r\n]*[(][ \t\r\n]*m_ConversionData[.]UseACM[ \t\r\n]*[|][|][ \t\r\n]*dwSize[ \t\r\n]*<[ \t\r\n]*m_cbConversionBuffer[ \t\r\n]*[)]"
    FILTER_ALREADY_PATCHED
    "${FILTER_TEXT}")

if(FILTER_ALREADY_PATCHED)
    message(STATUS "AudioMan ACM private conversion-buffer fix already present")
    return()
endif()

# Accept either upstream's equal-size exclusion or stability-v3's <= form.
string(REGEX MATCH
    "if[ \t\r\n]*[(][ \t\r\n]*dwSize[ \t\r\n]*<=[ \t\r\n]*m_cbConversionBuffer([ \t\r\n]*&&[ \t\r\n]*m_cbConversionBuffer[ \t\r\n]*!=[ \t\r\n]*dwSize)?[ \t\r\n]*[)]"
    FILTER_OLD_CONDITION
    "${FILTER_TEXT}")

if(NOT FILTER_OLD_CONDITION)
    message(FATAL_ERROR
        "AudioMan sndcnvt.cpp does not contain the expected conversion-buffer "
        "condition or the 4DMM fixed form; refusing a blind patch")
endif()

string(REGEX REPLACE
    "if[ \t\r\n]*[(][ \t\r\n]*dwSize[ \t\r\n]*<=[ \t\r\n]*m_cbConversionBuffer([ \t\r\n]*&&[ \t\r\n]*m_cbConversionBuffer[ \t\r\n]*!=[ \t\r\n]*dwSize)?[ \t\r\n]*[)]"
    "if (m_ConversionData.UseACM || dwSize < m_cbConversionBuffer)"
    FILTER_TEXT
    "${FILTER_TEXT}")

file(WRITE "${AUDIOMAN_FILTER}" "${FILTER_TEXT}")
message(STATUS "Patched AudioMan to use a private source buffer for every ACM conversion")
